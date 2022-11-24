#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "lib/stdio.h"
#include "lib/kernel/stdio.h"
#include "threads/palloc.h"
#include "filesys/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

  lock_init (&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
  // TODO: Your implementation goes here.

  int syscall_no = f->R.rax;

  uint64_t a1 = f->R.rdi;
  uint64_t a2 = f->R.rsi;
  uint64_t a3 = f->R.rdx;
  uint64_t a4 = f->R.r10;
  uint64_t a5 = f->R.r8;
  uint64_t a6 = f->R.r9;

  // SCW_dump_frame(f);

  switch (syscall_no) {
  case SYS_HALT:
    halt_handler ();
    break;
  case SYS_EXIT:
    exit_handler (a1);
    break;
  case SYS_FORK:
  	f->R.rax = fork_handler(a1, f);
  case SYS_EXEC:
    f->R.rax = exec_handler (a1);
    break;
  case SYS_WAIT:
  	f->R.rax = wait_handler(a1);
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
    f->R.rax = read_handler (a1, a2, a3);
    break;
  case SYS_WRITE:
    f->R.rax = write_handler (a1, a2, a3);
    break;
  case SYS_SEEK:
    seek_handler (a1, a2);
    break;
  case SYS_TELL:
    f->R.rax = tell_handler (a1);
    break;
  case SYS_CLOSE:
    close(a1);

  default:
    break;
  }
  // printf ("system call!\n");
  // thread_exit ();
}

/* 포인터가 가리키는 주소가 user영역에 유요한 주소인지 확인*/
/*
is_user_vaddr : 유저 가상주소 체크
add == NULL : 들어온 주소가 NULL 인지 확인
pml4_get_page : 들어온 주소가 유자 가상주소 안에 할당된 페이지를 가리키고있는지
확인
-->유저 영역 내이면서도 그 안에 할당된 페이지 안에 있어야 한다
*/
void
check_add (void *add) {
  struct thread *cur = thread_current ();
  if (!is_user_vaddr (add) || add == NULL ||
      pml4_get_page (cur->pml4, add) == NULL) {
    // printf ("나는 여길 탄다\n");
    exit_handler (-1);
  }
  // printf ("아니지 여길 타지 \n");
}

struct file *
find_file_using_fd (int fd) {
  if (fd < 0 || fd > FD_COUNT_LIMT)
    return NULL;

  struct file *file = thread_current ()->fd_table[fd];

  return file;
}

void
halt_handler (void) {
  power_off ();
}

void
exit_handler (int status) {
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

tid_t
fork_handler (const char *thread_name, struct intr_frame *f){
	return process_fork(thread_name, f);
}

int
exec_handler (const char *file) {
  check_add (file);
  char *file_name_copy = palloc_get_page (PAL_ZERO);

  if (file_name_copy == NULL)
    exit_handler (-1);

  strlcpy (file_name_copy, file, strlen (file) + 1);

  if (process_exec (file_name_copy) == -1)
    return -1;

  NOT_REACHED ();
  return 0;
}

int
wait_handler (pid_t pid) {
	process_wait(pid);
}

bool
create_handler (const char *file, unsigned initial_size) {

  check_add (file);
  return filesys_create (file, initial_size);
}

bool
remove_handler (const char *file) {
  check_add (file);
  return (filesys_remove (file));
}

int
open_handler (const char *file) {
  check_add (file);
  struct file *file_st = filesys_open (file);
  if (file_st == NULL) {
    return -1;
  }

  int fd_idx = add_file_to_FDT (file_st);

  if (fd_idx == -1) {
    file_close (file_st);
  }

  return fd_idx;
}

int
add_file_to_FDT (struct file *file) {
  struct thread *cur = thread_current ();
  struct file **fdt = cur->fd_table;
  int fd_index = cur->fd_idx;

  while (fdt[fd_index] != NULL && fd_index < FD_COUNT_LIMT) {
    fd_index++;
  }

  if (fd_index >= FD_COUNT_LIMT) {
    return -1;
  }

  cur->fd_idx = fd_index;
  fdt[fd_index] = file;
  return fd_index;
}

int
file_size_handler (int fd) {
  struct file *file_ = find_file_using_fd (fd);

  if (file_ == NULL)
    return -1;

  return file_length (file_);
}

int
read_handler (int fd, const void *buffer, unsigned size) {
  check_add (buffer);
  int read_result;
  struct file *file_obj = find_file_using_fd (fd);
  if (fd == 0) {
    *(char *) buffer = input_getc ();
    read_result = size;
  } else {
    if (file_obj == NULL)
      return -1;
    else {
      lock_acquire (&filesys_lock);
      read_result = file_read (file_obj, buffer, size);
      lock_release (&filesys_lock);
    }
  }
  return read_result;
}

int
write_handler (int fd, const void *buffer, unsigned size) {
  if (fd == STDOUT_FILENO) {
    putbuf (buffer, size);
  }
  return size;
}

void
seek_handler (int fd, unsigned position) {
  struct file *file_obj = find_file_using_fd (fd);

  if (fd <= 2)
    return;

  if (file_obj == NULL)
    return;

  file_obj->pos = position;
}

unsigned
tell_handler (int fd) {
  if (fd <= 2)
    return;
  struct file *file_obj = find_file_using_fd (fd);
  check_add (file_obj);

  return file_tell (file_obj);
}

void
close (int fd) {
  struct file *file_obj = find_file_using_fd (fd);
  if (file_obj == NULL)
    return;

  file_close (file_obj);
}