#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "userprog/syscall.h"
#include "kernel/list.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

struct argv {   // process fork를 할때 현재 Thread의 정보와 interrupt frame의
                // 정보를 저장하기 위한 공간
  struct thread *fork_thread;
  struct intr_frame *fork_if;
};

/* General process initializer for initd and other process. */
static void
process_init (void) { // process를 초기화하는 부분 - project 2까지는 크게 사용하지 않지만 앞으로 사용하게 될 예정
  struct thread *current = thread_current ();
}

struct thread *
get_child (int pid) {   // child process를 찾는 함수 -- child list를 관리하니깐
                        // 거기서 찾으면됨
  struct thread *cur = thread_current ();
  struct list *child_list = &cur->child_s; // 현재 process의 자식 process list

  for (struct list_elem *temp = list_begin (child_list); temp != list_end (child_list); temp = list_next (temp)) { // 자식 process의 list를 순회하면서 자식는 부분
    struct thread *temp_thread = list_entry (temp, struct thread, child_elem);

    if (temp_thread->tid == pid) {
      return temp_thread;
    }
  }
  return NULL;   // 만일 적합한 child를 찾지 못했다면 NULL 을 return함
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) { // 얘는 처음 USER Program이 시작될때 한번만 동작함
  char *fn_copy;
  tid_t tid;
  /* Make a copy of FILE_NAME.
   * Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0); // palloc get page를 사용하여 page를 Flag 0을 이용해서 할당 받음
  if (fn_copy == NULL) // 할당에 실패하면 return Error
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE); // 들어온 file name을 fn_copy에 복사해넣음 PGSIZE 만큼 (file_name이라고 명명했지만 실제는 Task 전체가 들어옴)

  /* for project 2 - start */

  char *save; // task에서 file_name 부분만 잘라내기 위한 부분
  strtok_r (file_name, " ", &save); // file_name 부분을 strtok_r 함수를 이용하여 자름

  /* for project 2 - end */

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy); // fn_copy에 저장된 전체를 사용하여 initd 함수가 실행됨

  if (tid == TID_ERROR) // 만일 위에서 thread_creat가 실패했다면 TID_ERROR를 반환받을 것임
    palloc_free_page (fn_copy); // 그에 따라서 위에서 할당받은 공간을 free 해줌 --> 안그러면 메모리 누수가 발생할 것임

  return tid; // 위 if를 타지 않았다면 tid를 return 함
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
  supplemental_page_table_init (&thread_current ()->spt);
#endif

  process_init ();   // 프로세스를 초기화할 거지만 현재 project 2까지는 크게
                     // 중요하지 않음

  if (process_exec (f_name) < 0)   // process_exec 을 통해서 process가 실행가능한 상태로 변경됨
    PANIC ("Fail to launch initd\n");

  NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_) {
  /* Clone current thread to new thread.*/
  struct thread *parent = thread_current ();

  struct argv *fork_argv = (struct argv *) malloc (sizeof (struct argv)); // thread와 intr_fram을 기억하기 위한 공간을 만들어줌
  fork_argv->fork_if = if_; // 각 구조안에 현재 정보를 넣어줌
  fork_argv->fork_thread = thread_current ();

  tid_t tid = thread_create (name, PRI_DEFAULT, __do_fork, fork_argv); // thread가 create되면서 fork에 성공했다면 tid를 return 하지만 실패했다면 TID_ERROR를 return 함
  if (tid == TID_ERROR)
    return TID_ERROR;

  struct thread *child = get_child (tid); // child list에서 tid가 일치하는 child를 찾아주고 (thread가 create 되면서 추가가 됐을거니깐)

  sema_down (&child->fork_sema); // 해당 child의 fork를 sema_down 함 --> fork가 완료되기전에 process가 종료되지 않게 방지함
  
  return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
  struct thread *current = thread_current ();
  struct argv *argv_fork = (struct argv *) aux;
  // struct thread *parent = (struct thread *) aux;
  struct thread *parent = argv_fork->fork_thread;
  void *parent_page;
  void *newpage;
  bool writable;

  /* 1. TODO: If the parent_page is kernel page, then return immediately. */
  /*만약 va가 kernel address 라면 --> true를 return */
  if (is_kernel_vaddr (va))
    return true;
  /* 2. Resolve VA from the parent's page map level 4. */
  parent_page = pml4_get_page (parent->pml4, va);
  if (parent_page == NULL)
    return false;

  /* 3. TODO: Allocate new PAL_USER page for the child and set result to
   *    TODO: NEWPAGE. */
  newpage = palloc_get_page (PAL_USER);
  if (newpage == NULL) {
    printf ("duplicate_pte page fault\n");
    return false;
  }

  /* 4. TODO: Duplicate parent's page to the new page and
   *    TODO: check whether parent's page is writable or not (set WRITABLE
   *    TODO: according to the result). */
  /*input 으로 들어온 parent -> pml4 와 is_writable 함수를 이용하여 쓰기가 가능한지 아닌지 판단한다*/
  memcpy (newpage, parent_page, PGSIZE);
  writable = is_writable (pte);

  /* 5. Add new page to child's page table at address VA with WRITABLE
   *    permission. */
  /* fork 하려는 것이 쓰기가 가능하면 read/write가 다되는걸 return 하고
    read만 가능하면 read 전용으로 return함

    위 두가지 모두 true를 return 함
    하지만 메모리를 할당하지 못했다면 false를 return 함
   */
  if (!pml4_set_page (current->pml4, va, newpage, writable)) {
    /* 6. TODO: if fail to insert page, do error handling. */
    printf (" duplicate_pte pmlt_set_page fault \n");
    return false;
  }
  return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
  struct intr_frame if_;
  // struct thread *parent = (struct thread *) aux;

  struct argv *fork_argv = (struct argv *) aux;
  struct thread *parent = fork_argv->fork_thread;
  struct thread *current = thread_current ();
  /* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
  struct intr_frame *parent_if;
  parent_if = fork_argv->fork_if;
  bool succ = true;

  /* 1. Read the cpu context to local stack. */
  memcpy (&if_, parent_if, sizeof (struct intr_frame));
  if_.R.rax = 0; // 자식 process는 항상 0을 return 하기 때문에 if_R.rax = 0을 넣어줌 --> git book 내용과 함께 작성

  /* 2. Duplicate PT */
  current->pml4 = pml4_create ();
  if (current->pml4 == NULL)
    goto error;

  process_activate (current);
#ifdef VM
  supplemental_page_table_init (&current->spt);
  if (!supplemental_page_table_copy (&current->spt, &parent->spt))
    goto error;
#else
  if (!pml4_for_each (parent->pml4, duplicate_pte, fork_argv)) // pte_entry table에 duplicate_pte 함수를 적용함 --> 실패하면 error로 가고 아니면 진행
    goto error;
#endif

  /* TODO: Your code goes here.
   * TODO: Hint) To duplicate the file object, use `file_duplicate`
   * TODO:       in include/filesys/file.h. Note that parent should not return
   * TODO:       from the fork() until this function successfully duplicates
   * TODO:       the resources of parent.*/
  if (parent->fd_idx >= FD_COUNT_LIMT) // 만일 할당한 fd_idx가 FD_COUNT_LIMIT 보다 크다면 error로 감 (공간 초과)
    goto error;


  // 부모 process가 가지고 있는 열린 파일의 정보는 자식 process에게 상속됨
  current->fd_table[0] = parent->fd_table[0]; // 처음 0과 1은 정해져있으니 그 둘을 그대로 복사해옴
  current->fd_table[1] = parent->fd_table[1];

  for (int i = 2; i < FD_COUNT_LIMT; i++) { // 남은 부분들도 복사
    struct file *temp_file = parent->fd_table[i];
    if (temp_file == NULL)
      continue;
    current->fd_table[i] = file_duplicate (temp_file); // temp file의 속성을 포함한 개체를 복사하고 File과 동일한 inode에 대한 새 파일을 반환 --> 실패하면 NULL을 반환
  }
  
  current->fd_idx = parent->fd_idx; // 위 정보를 상속 받기 때문에 열린 파일의 개수도 같이 넘겨줌

  // sema_up (&current->fork_sema); // 그리고 sema_up으로 fork_sema는 풀어줌

  // process_init (); // process를 초기화

  /* Finally, switch to the newly created process. */
  free (fork_argv); // fork_argv의 역할은 끝났으니깐 free 해줌

  /*
  do_iret을 통해서 interrupt frame에 있는 정보를 register로 보내줌
  if_.rip 의 시작주소를 따로 저장해주는 작업이 없는 이유는 (process_exe의 load에서 하는 작업)
  duplicate_pte 와 file_duplicate 등을 통해서 interrupt frame의 모든 정보를 자식 process에 복사를 해주었기 때문임
  그래서 그냥 register에 올려주기만 하면됨
  */
  if (succ)
    do_iret (&if_);

error:
  current->exit_status = TID_ERROR;
  sema_up(&current->fork_sema);
  free (fork_argv);
  exit_handler (TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
  char *file_name = f_name;
  bool success;

  /* We cannot use the intr_frame in the thread structure.
   * This is because when current thread rescheduled,
   * it stores the execution information to the member. */
  struct intr_frame _if;
  _if.ds = _if.es = _if.ss = SEL_UDSEG;
  _if.cs = SEL_UCSEG;
  _if.eflags = FLAG_IF | FLAG_MBS;

  /* We first kill the current context */
  process_cleanup ();

   /* And then load the binary */
  success = load (file_name, &_if);   // _if에 file name을 올릴때 palloc이 page를
                                // 할당함 --> load 안에 pml4_create에서 만듦
  // hex_dump (_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

  /* If load failed, quit. */
  palloc_free_page (file_name);   // 그에 따라서 아래에서 free를 해줌
  if (!success)
    return -1;

  /* Start switched process. */
  do_iret (&_if);   // load가 완료되었다면 context switching을 진행
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
  /* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
   * XXX:       to add infinite loop here before
   * XXX:       implementing the process_wait. */

  struct thread *child = get_child (child_tid); // 해당 tid를 갖는 child를 찾고
  if (child == NULL)
    return -1;
  sema_down (&child->wait_sema); // process 의 wait를 sema down함 --> process는 현재 동작 중이고 wait에서 대기중임
  list_remove (&child->child_elem); // 얘를 삭제한다는 것은 process exit 신호가 들어왔음을 의미함 (process_exit에서 sema up을 시켜줌)
  sema_up (&child->exit_sema); // 삭제 후 sema_up으로 child exit_sema도 풀어줌

  return child->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
  struct thread *curr = thread_current ();
  /* TODO: Your code goes here.
   * TODO: Implement process termination message (see
   * TODO: project2/process_termination.html).
   * TODO: We recommend you to implement process resource cleanup here. */

  for (int i = 2; i < FD_COUNT_LIMT; i++) // process가 끝나는 신호가 들어오면 해당 process에서 열려있던 file들을 모두 닫아줌
    close_handler (i); 

  palloc_free_multiple (curr->fd_table, FD_PAGES);   // thread create때 할당 받은 공간을 다시 free해줌
  file_close (curr->running);  // ROX 를 위한 부분 (write deny가 된걸 file close 해주면 해당 함수 안에 allow로 해제 해주는 부분도 존재함)
  process_cleanup ();   // 현재 process의 자원도 해제해줘야함 --> 이게 sema down 밑으로 가면 sema _down에서 동작하지 않아서 multi-oom 이 실패함

  sema_up (&curr->wait_sema); // exit 신호를 받았기 때문에 wait_sema를 올려주고 wait에 걸려있던 process가 종료하게됨
  sema_up (&curr->fork_sema); // fork_sema 도 해제해줌 --> 다 종료해야하니깐 (sema가 걸려있으면 삭제도 종료도 안되니깐)
  sema_down (&curr->exit_sema); // 이후 exit는 wait가 끝난 이후에 종료가 되어야하니깐 exit sema를 down해서 걸어놓음
}

/* Free the current process's resources. */
static void
process_cleanup (void) { // 현재 process의 resource도 resource를 해제해줌
  struct thread *curr = thread_current ();
  
#ifdef VM
  if(!hash_empty(&curr->spt.pages))
    supplemental_page_table_kill (&curr->spt);
#endif

  uint64_t *pml4;
  /* Destroy the current process's page directory and switch back
   * to the kernel-only page directory. */
  pml4 = curr->pml4;
  if (pml4 != NULL) {
    /* Correct ordering here is crucial.  We must set
     * cur->pagedir to NULL before switching page directories,
     * so that a timer interrupt can't switch back to the
     * process page directory.  We must activate the base page
     * directory before destroying the process's page
     * directory, or our active page directory will be one
     * that's been freed (and cleared). */
    curr->pml4 = NULL;
    pml4_activate (NULL);
    pml4_destroy (pml4);
  }
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
  /* Activate thread's page tables. */
  pml4_activate (next->pml4);

  /* Set thread's kernel stack for use in processing interrupts. */
  tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0          /* Ignore. */
#define PT_LOAD    1          /* Loadable segment. */
#define PT_DYNAMIC 2          /* Dynamic linking info. */
#define PT_INTERP  3          /* Name of dynamic loader. */
#define PT_NOTE    4          /* Auxiliary info. */
#define PT_SHLIB   5          /* Reserved. */
#define PT_PHDR    6          /* Program header table. */
#define PT_STACK   0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
  unsigned char e_ident[EI_NIDENT];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

struct ELF64_PHDR {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
};

/* Abbreviations */
#define ELF  ELF64_hdr
#define Phdr ELF64_PHDR

// static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
  struct thread *t = thread_current ();
  struct ELF ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* for project 2 - start*/
  uintptr_t stack_ptr;   // stack pointer가 가리키는 위치 표시
  char *address[64];   // stack pointer가 처음 들어간 주소를 다시 넣으려함

  char *argv[64];   // 인자를 나눠서 저장할 공간 - 0은 file name 그 이후부터는
                    // 인자들 - 2중 포인터 사용 : 2차원 배열로 저장하기 위해서
  int argc = 0;   // 인자 개수

  char *token;           // file name을 token화
  char *remain_string;   // 남은 string을 저장하기 위한 공간

  token = strtok_r (file_name, " ",
                    &remain_string);   // strtok_r 을 사용해서 토큰으로 구분하고
                                       // 나눔 그때마다 argv 배열에 저장
  argv[0] = token;

  while (token != NULL) {
    token = strtok_r (NULL, " ", &remain_string);
    argc++;
    // argv[argc]= "NULL";
    argv[argc] = token;
  }
  /* for project 2 - end */

  /* Allocate and activate page directory. */
  t->pml4 = pml4_create ();   // 해당 작업 진행시 내부에서 palloc이 할당됨
  if (t->pml4 == NULL)
    goto done;
  process_activate (thread_current ());

  /* Open executable file. */
  file = filesys_open (
      argv[0]);   // 위에서 active하였기때문에 이제 실행가능한 파일을 open 함
  if (file == NULL) {
    printf ("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) !=
          sizeof ehdr ||   // ehdr (ELF file) 을 file에 복사해 넣음
      memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7) ||
      ehdr.e_type != 2 || ehdr.e_machine != 0x3E ||   // amd64
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof (struct Phdr) ||
      ehdr.e_phnum > 1024) {
    printf ("load: %s: error loading executable\n", file_name);
    goto done;
  }
  
  /* for project 2 - ROX */
  t->running = file;   // 위 if문에서 ehdr을 file에 복사해 넣었으니 해당 정보를
                       // 가진 file을 running에 넣어줌
  file_deny_write (file);   // file의 권한을 막음 --> 나만 쓸수 있게 
  /* for project 2 - ROX */

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Phdr phdr;   // phdr == Program header

    if (file_ofs < 0 ||
        file_ofs > file_length (
                       file))   // file_ofs 가 file_length보다 크다 == 더이상 쓸
                                // 공간이 없다. 0보다 작다 == file에 문제가 있음
      goto done;
    file_seek (file, file_ofs);   // 커서를 file_ofs까지 이동시켜줌 --> 그
                                  // 이후부터 써야하니깐

    if (file_read (file, &phdr, sizeof phdr) !=
        sizeof phdr)   // phdr을 file에 phdr size만큼 복사한 byte가 sizeof
                       // phdr이랑 다르면 ==  (위에가 잘못된거)
      goto done;
    file_ofs += sizeof phdr;   // file ofs의 위치를 phdr 의 크기만큼 이동해서
                               // 기록을 할 준비
    switch (phdr.p_type) {
    case PT_NULL:
    case PT_NOTE:
    case PT_PHDR:
    case PT_STACK:
    default:
      /* Ignore this segment. */
      break;
    case PT_DYNAMIC:
    case PT_INTERP:
    case PT_SHLIB:
      goto done;
    case PT_LOAD:
      if (validate_segment (&phdr, file)) { // validate_segment --> phdr이 file에서 로드가능한 유요한 세그먼트를 설명한다면 true를 return 아니면 false를 return
        bool writable = (phdr.p_flags & PF_W) != 0;
        uint64_t file_page = phdr.p_offset & ~PGMASK;
        uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
        uint64_t page_offset = phdr.p_vaddr & PGMASK;
        uint32_t read_bytes, zero_bytes;
        if (phdr.p_filesz > 0) {
          /* Normal segment.
           * Read initial part from disk and zero the rest. */
          read_bytes = page_offset + phdr.p_filesz;
          zero_bytes =
              (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
        } else {
          /* Entirely zero.
           * Don't read anything from disk. */
          read_bytes = 0;
          zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
        }
        if (!load_segment (file, file_page, (void *) mem_page, read_bytes, zero_bytes, writable))
          goto done;
      } else
        goto done;
      break;
    }
  }

  /* Set up stack. */
  if (!setup_stack (if_))
    goto done;

  /* Start address. */
  if_->rip = ehdr.e_entry;

  /* TODO: Your code goes here.
   * TODO: Implement argument passing (see project2/argument_passing.html). */
  
  /*==========여기서 부터 argument passing 단계 시작 =============*/

  /* token들을 USER stack에 넣어줌과 동시에 해당 address도 기억을 하기 위하여 address를 사용함 - 1단계 */
  address[0] = if_->rsp;
  for (int i = argc - 1; i > -1; i--) {
    if_->rsp = if_->rsp - (strlen (argv[i]) + 1);
    memcpy (if_->rsp, argv[i], strlen (argv[i]) + 1);
    address[i] = if_->rsp;
  }
  
  /* token들을 stack_ptr (user VM)에 넣어줌 - 2단계 */
  if ((USER_STACK - (if_->rsp)) % 8 != 0) {
    int i = 8 - (USER_STACK - (if_->rsp)) % 8;
    if_->rsp = if_->rsp - i;
    memset (if_->rsp, 0, i);
  }

  /* 1단계 애들의 주소를 넣어준다 - 3 -1 단계 처음엔 0을 넣어줌 */
  if_->rsp = if_->rsp - 8;
  memset (if_->rsp, 0, 8);

  /* 1단계 애들의 주소를 넣어준다 - 3 -2 단계 주소를 넣어줌 */

  if (address != NULL) {
    size_t addr_size = argc * sizeof (address[0]) / sizeof (char);
    if_->rsp = if_->rsp - addr_size;

    memcpy (if_->rsp, address, addr_size);
  }

  /* fake return을 넣어줌 - 4단계*/
  if_->rsp = if_->rsp - 8;
  memset (if_->rsp, 0, 8);

  success = true;
  /* 인자의 개수는 R.rdi에 저장해주고 시작 주소는 R.rsi에 넣어줌 */
  if_->R.rdi = argc;
  if_->R.rsi = if_->rsp + 8; //fake return을 제외한 시작점
done:
  /* We arrive here whether the load is successful or not. */

  return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (uint64_t) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
              uint32_t zero_bytes, bool writable) {
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Do calculate how to fill this page.
     * We will read PAGE_READ_BYTES bytes from FILE
     * and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t *kpage = palloc_get_page (PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
      palloc_free_page (kpage);
      return false;
    }
    memset (kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page (upage, kpage, writable)) {
      printf ("fail\n");
      palloc_free_page (kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
    if (success)
      if_->rsp = USER_STACK;
    else
      palloc_free_page (kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
   * address, then map our page there. */
  return (pml4_get_page (t->pml4, upage) == NULL &&
          pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
bool
install_page (void *upage, void *kpage, bool writable) {
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
   * address, then map our page there. */
  return (pml4_get_page (t->pml4, upage) == NULL &&
          pml4_set_page (t->pml4, upage, kpage, writable));
}

bool
lazy_load_segment (struct page *page, void *aux) {
  /* TODO: Load the segment from the file */
  /* TODO: This called when the first page fault occurs on address VA. */
  /* TODO: VA is available when calling this function. */
  struct file *file = ((struct container *)aux) ->file;
  off_t aux_offset = ((struct container *)aux)->offset;
  size_t aux_read_byte = ((struct container *)aux)->read_byte;

  size_t page_zero_byte = PGSIZE - aux_read_byte;

  file_seek(file, aux_offset);

  if(file_read(file, page->frame->kva, aux_read_byte) != (int)aux_read_byte){
    palloc_free_page(page->frame->kva);
    return false;
  }
  memset(page->frame->kva + aux_read_byte, 0, page_zero_byte);

  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0) {
    /* Do calculate how to fill this page.
     * We will read PAGE_READ_BYTES bytes from FILE
     * and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* project for 3 - start */
    struct container *container = (struct container *)malloc(sizeof(struct container));
    container->file = file;
    container->offset = ofs;
    container->read_byte = page_read_bytes;
    
    /* project for 3 - end */

    /* TODO: Set up aux to pass information to the lazy_load_segment. */
    // void *aux = NULL;
    if (!vm_alloc_page_with_initializer (VM_ANON, upage, writable,
                                         lazy_load_segment, container))
      return false;

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    ofs += page_read_bytes;
  }
  return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
bool
setup_stack (struct intr_frame *if_) {
  bool success = false;
  void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

  /* TODO: Map the stack on stack_bottom and claim the page immediately.
   * TODO: If success, set the rsp accordingly.
   * TODO: You should mark the page is stack. */
  /* TODO: Your code goes here */

if(vm_alloc_page(VM_ANON|VM_MARKER_0, stack_bottom, 1)){
    success = vm_claim_page(stack_bottom);

    if(success){
      if_->rsp = USER_STACK;
      thread_current()->stack_bottom = stack_bottom;
    }
  }

  return success;
}
#endif /* VM */
