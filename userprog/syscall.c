#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "user/syscall.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
// struct page *check_add (void *);
void check_buff (void * , unsigned , void *, bool );

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR         0xc0000081 /* Segment selector msr */
#define MSR_LSTAR        0xc0000082 /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define STDIN_FILENO     0
#define STDOUT_FILENO    1

void
syscall_init (void) {
  write_msr (MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
                           ((uint64_t) SEL_KCSEG) << 32);
  write_msr (MSR_LSTAR, (uint64_t) syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr (MSR_SYSCALL_MASK,
             FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

  lock_init (&filesys_lock);   // write 시에 다른 접근이 오는걸 막기위해 lock을 사용
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
  // TODO: Your implementation goes here.
  thread_current()->rsp_stack = f->rsp;


  int syscall_no = f->R.rax;   // syscall numbef를 가지고 있음

  uint64_t a1 = f->R.rdi;   // 첫번째 인자 - file name(인자가 1개~2개),
                            // fd(인자가 3개 이상)
  uint64_t a2 = f->R.rsi;   // 두번째 인자 - size (인자가 2개), buffer(인자가 3개 이상)
  uint64_t a3 = f->R.rdx;   // 세번째 인자 - size
  uint64_t a4 = f->R.r10;
  uint64_t a5 = f->R.r8;
  uint64_t a6 = f->R.r9;

  // SCW_dump_frame (f); // interrupt fram의 정보를 보기 위한 print dump
  switch (syscall_no) {   // return 을 시켜주는 handler들은 f->R.rax로 return을 시켜줘야함 (다음 syscall_no을 동작시켜야함)
    case SYS_HALT:   // poawer off system call
      halt_handler ();
      break;
    case SYS_EXIT:   // exit system call
      exit_handler (a1);
      break;
    case SYS_FORK:   // fork system call - 자식 process를 만든다
      f->R.rax = fork_handler (a1, f);
      break;
    case SYS_EXEC:   // exec system call
      f->R.rax = exec_handler (a1);
      break;
    case SYS_WAIT:
      f->R.rax = wait_handler (a1);
      break;
    case SYS_CREATE:
      f->R.rax = create_handler (a1, a2);
      break;
    case SYS_REMOVE:
      f->R.rax = remove_handler (a1);
      break;
    case SYS_OPEN:
      f->R.rax = open_handler (a1);
      break;
    case SYS_FILESIZE:
      f->R.rax = file_size_handler (a1);
      break;
    case SYS_READ:
      check_buff(f->R.rsi, f->R.rdx, f->rsp, 1);
      f->R.rax = read_handler (a1, a2, a3);
      break;
    case SYS_WRITE:
      check_buff(f->R.rsi, f->R.rdx, f->rsp, 0);
      f->R.rax = write_handler (a1, a2, a3);
      break;
    case SYS_SEEK:
      seek_handler (a1, a2);
      break;
    case SYS_TELL:
      f->R.rax = tell_handler (a1);
      break;
    case SYS_CLOSE:
      close_handler (a1);
      break;
    case SYS_MMAP:
      f->R.rax = mmap(a1, a2, a3, a4, a5);
      break;
    case SYS_MUNMAP:
      munmap(a1);
      break;

    default:
      exit_handler (-1);
      break;
  }
}

/* 포인터가 가리키는 주소가 user영역에 유요한 주소인지 확인*/
/*
is_user_vaddr : 유저 가상주소 체크
add == NULL : 들어온 주소가 NULL 인지 확인
pml4_get_page : 들어온 주소가 유자 가상주소 안에 할당된 페이지를 가리키고있는지
확인
-->유저 영역 내이면서도 그 안에 할당된 페이지 안에 있어야 한다
*/
// void
// check_add (void *add) {
//   struct thread *cur = thread_current ();
//   if (!is_user_vaddr (add) || add == NULL ||
//       pml4_get_page (cur->pml4, add) == NULL) {
//     exit_handler (-1);
//   }
// }

struct page *check_add (void *add) {
  if (is_kernel_vaddr (add) || add == NULL || spt_find_page(&thread_current()->spt,add) == NULL || !(&thread_current()->pml4)) 
  {
    exit_handler(-1);
  }
  return spt_find_page(&thread_current()->spt, add);
}

void
check_buff (void * buffer, unsigned size, void *rsp, bool to_write){
  for(int i=0; i<size; i++){
    struct page *page = check_add(buffer + i);
    if(page ==NULL)
      exit_handler(-1);
    if(to_write == true && page->writable == false)
      exit_handler(-1);
  }
}


static struct file *
find_file_using_fd (int fd) {   // fd를 가지고 file을 찾기 위한 함수 file은
                                // process에 저장되어있으니 찾는거 가능
  struct thread *cur = thread_current ();

  if (fd < 0 || fd >= FD_COUNT_LIMT)
    return NULL;

  return cur->fd_table[fd];   // 해당 file을 return 함
}

void
halt_handler (void) {   // Pintos를 종료시킴
  power_off ();
}

void
exit_handler (int status) {   // 현재 동작중인 program을 종료함
  struct thread *cur = thread_current ();
  cur->exit_status = status;   // exit status를 저장해주고 종료
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

tid_t
fork_handler (const char *thread_name, struct intr_frame *f) {
  return process_fork (thread_name, f);
}

int
exec_handler (
    const char *file) {   // exec handler가 정상 동작을 한다면 아무것도
                          // return하지 않고 실패한다면 -1 을 return 한다
  check_add (file);
  char *file_name_copy = palloc_get_page (PAL_ZERO);

  if (file_name_copy == NULL)   // page를 정상적으로 할당 받지 못했다면
    exit_handler (-1);

  strlcpy (file_name_copy, file,
           strlen (file) + 1);   // +1은 NULL 문자를 위해서

  if (process_exec (file_name_copy) ==
      -1)   // process_exec에 성공하지 못하면 -1을 return 함
    return -1;

  // NOT_REACHED ();
  // return 0; // 정상동작하면 0을 return
}

int
wait_handler (tid_t pid) {   // 자식 process를 wait 함 (exit status가 올때까지)
  return process_wait (pid);
}

bool   // create 성공하면 true 실패하면 false, ++ open 이랑은 다른것, 혼란스러워하면 안됨
create_handler (const char *file, unsigned initial_size) {
  check_add (file);
  if (file)
  {
    return filesys_create (file, initial_size);
  }
  else{
    exit_handler(-1);
    }
}

bool   // remove 성공하면 true 실패하면 false, ++ file이 remove되는 것과 close는 별개임 (즉, 제거되더라도 open되어있을 수도 있다는 말)
remove_handler (const char *file) {
  check_add (file);
  return (filesys_remove (file));
}

int   // open 하는데 성공하면 0 이상의 정수를 반환함 실패하면 음수를 반환
open_handler (const char *file) {
  check_add (file);

  if(file ==NULL){
    return -1;
  }
  struct file *file_st = filesys_open (file);   // 일단 파일을 open하고
  if (file_st == NULL) {   // open 한게 Null이 아니면 if문을 통과
    return -1;
  }

  int fd_idx = add_file_to_FDT (file_st);   // open한 file 을 table로 관리함
  // 현재 thread가 가진 구조체 내부 fd table에 빈공간이 있으면
  // 추가 후 해당 위치를 return 하고 추가하지 못했으면 -1을 return함

  if (fd_idx == -1) {   // 추가하지 못했다면
    file_close (file_st);   // 열린걸 다시 닫아줘야함 (열린 상태로 두면 안됨)
  }

  return fd_idx;   // 파일이 잘 열렸다면 열린 파일의 fd table에서의 index를
                   // 반환함 (아마도 열린 파일의 개수랑 동일할 것으로 보임)
}

int
add_file_to_FDT (struct file *file) {   // 각 process에서 open한 부분을 기억하는 부분
  struct thread *cur = thread_current ();
  struct file **fdt = cur->fd_table;
  int fd_index = cur->fd_idx;

  while (fdt[fd_index] != NULL && fd_index < FD_COUNT_LIMT) {   // 현재 fd_table의 빈공간을 찾는 작업
    fd_index++;
  }

  if (fd_index >= FD_COUNT_LIMT) {   // fd_index가 FD_count_limit 보다 크다는건 공간이 없다는 의미
    return -1;
  }

  cur->fd_idx = fd_index;   // fd_index를 thread에 저장해주고
  fdt[fd_index] = file;     // fd table에 file을 저장
  return fd_index;
}

int
file_size_handler (int fd) {   // input된 fd를 이용하여 file의 size를 찾아주는 handler
  struct file *file_ = find_file_using_fd (fd);   // 들어온 fd에 맞는 file을 찾아주고

  if (file_ == NULL)   // 만일 적합한 file을 찾지 못했다면 -1 을 return 함
    return -1;

  return file_length (file_);   // 미리 구현되어있는 file_length 함수를 사용하여 file_의 크기를 찾아서 return 해줌
}

int
read_handler ( int fd, const void *buffer, unsigned size) {   // open 된 file을 읽고 + 읽은 file의 크기를 반환
  check_add (buffer);
  int read_result;   // file을 읽고 난 후 크기를 저장하기 위한 선언
  struct file *file_obj = find_file_using_fd (fd);   // 받은 식별자에 해당하는 file을 찾음

  if (file_obj == NULL)
    return -1;

  if (fd == STDIN_FILENO) {   // fd == STDIN_FILENO 는 표준 입력을 얘기한다 == 키보드 등으로 input 받음
    char word;   // 키보드로 입력을 한글자씩 받기위한 공간
    for (read_result = 0; read_result < size; read_result++) {   // read_result는 0부터 input들어온 개수 또는 처음 들어온 size까지 받아서 출력함
      word = input_getc ();
      if (word == "\0")   // 중간에 NULL 문자가 들어오면 break
        break;
    }
  } else if (fd == STDOUT_FILENO) {   // 표준 출력이면 출력부인데 읽을수 없으니 실패를 의미하는 -1을 return 함
    return -1;
  } else {
    lock_acquire (&filesys_lock);   // file_read를 사용하기 전에 lock을 걸어야함 - 내가 읽는 동안에 file이 수정되거나 간섭받으면 안되니깐
    read_result = file_read (file_obj, buffer, size);   // file_read는 구현이 되어있는 함수임
    lock_release (&filesys_lock);   // lock을 release해줌
  }
  return read_result;   // 읽은 크기를 return함
}

int
write_handler (int fd, const void *buffer, unsigned size) {
  check_add (buffer);
  struct file *file_obj = find_file_using_fd (fd);
  if (fd == STDIN_FILENO)   // fd == STDIN이면 표준 입력이라서 write가 불가함 따라서 return 0
    return 0;

  if (fd == STDOUT_FILENO) {   // fd == STDOUT이면 표준 출력 따라서
    putbuf (buffer, size);   // putbuf (사전에 구현된 함수) 함수릍 통해서 buffer와 size를 출력함
    return size;   // 그리고 결과로 크기만큼 return 함
  } else {
    if (file_obj == NULL)   // file_obj가 NULL이면 return 0;
      return 0;
    // lock_acquire (&filesys_lock);   // file write를 하기 전에 lock을 걸고
    off_t write_result = file_write (file_obj, buffer, size);   // file에 buffer를 size만큼 쓰고
    // lock_release (&filesys_lock);              // lock 을 풀어줌
    return write_result;   // 결과로 write 크기 (buffer에 적힌 크기) 를 return 함
  }
}

void
seek_handler (int fd, unsigned position) {   // file 내부의 커서를 이동한다 --> 첫 시작은 position이 0이고 끝까지 가면 file의 크기와 같은 byte를 가짐
  struct file *file_obj = find_file_using_fd (fd);

  file_seek (file_obj, position);   // 만약 position이 파일의 끝을 가리키고 있는데 더 position을
                                    // 찾는걸 요청한다면 커서는 파일 끝을 가리키지만 byte는 읽은
                                    // 바이트가 없기 때문에 0을 나타냄
}

unsigned
tell_handler (int fd) {   // file의 read or write를 하기 위한 위치를 반환함 - byte로
  if (fd <= 2)
    return;
  struct file *file_obj = find_file_using_fd (fd);
  check_add (file_obj);
  if (file_obj == NULL)
    return;

  return file_tell (file_obj);
}

void
close_handler (int fd) {   // fd를 이용하여 열려 있는 file을 닫음
  struct file *file_obj = find_file_using_fd (fd);
  if (file_obj == NULL)
    return;

  if (fd < 0 || fd >= FD_COUNT_LIMT)
    return;
  thread_current ()->fd_table[fd] = NULL;   // 열린 파일이 있던 위치를 NULL로 바꾸고

  // lock_acquire (&filesys_lock);
  file_close (file_obj);
  // lock_release (&filesys_lock);
}

void
*mmap (void *addr, size_t length, int writable, int fd, off_t offset){
  if(offset % PGSIZE != 0 )
    return NULL;

  if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <=0)
    return NULL;
  
  if (fd == 0 || fd == 1)
    exit_handler(-1);
  
  if(spt_find_page(&thread_current()->spt, addr))
    return NULL;
  
  struct file * target = find_file_using_fd(fd);

  if(target == NULL)
    return NULL;
  
  void *ret = do_mmap(addr, length, writable, target, offset);

  return ret;
}

void 
munmap (void *addr){
  do_munmap(addr);
}