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
  
  SCW_dump_frame(f);

  switch (syscall_no) {
  case SYS_HALT:
    halt ();
    break;
  case SYS_EXIT:
    exit (a1);
    break;
  // case SYS_FORK:
  // 	fork(f->R.rdi, f);
  case SYS_EXEC:
    exec (a1);
    break;
  // case SYS_WAIT:
  // 	process_wait();
  case SYS_CREATE:
    create (a1, a2);
    break;
  case SYS_REMOVE:
    remove (a1);
    break;
  // case SYS_OPEN:
  // 	open(f->R.rdi);
  // case SYS_FILESIZE:
  // /* code */
  // case SYS_READ:
  // /* code */
  case SYS_WRITE:
    printf ("%s", a2);
    break;
    // case SYS_SEEK:
    // /* code */
    // case SYS_TELL:
    // /* code */
    // case SYS_CLOSE:
    // /* code */

  default:
    break;
  }
  // printf ("system call!\n");
  // thread_exit ();
}

void
check_add (void *add) {
  struct thread *cur = thread_current ();
  if (!is_user_vaddr (add) || add == NULL ||
      pml4_get_page (cur->pml4, add) == NULL) {
    exit (-1);
  }
}

void
halt (void) {
  power_off ();
}

void
exit (int status) {
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

// tid_t
// fork (const char *thread_name, struct intr_frame *f){
// 	return process_fork(thread_name, f);
// }

int
exec (const char *file) {
  check_add (file);
  char *file_name_copy = palloc_get_page (PAL_ZERO);
  if (file_name_copy == NULL)
    return -1;
  strlcpy (file_name_copy, file, strlen (file) + 1);

  if (process_exec (file_name_copy) == -1)
    return -1;

  NOT_REACHED ();
  return 0;
}

bool
create (const char *file, unsigned initial_size) {
  check_add (file);
  if (filesys_create (file, initial_size))
    return true;
  else
    return false;
}

bool
remove (const char *file) {
  check_add (file);
  if (filesys_remove (file))
    return true;
  else
    return false;
}

int
open (const char *file) {
  struct file *open_file = filesys_open (file);
  if (open_file == NULL) {
    return -1;
  }

  return filesys_open (file);
}

int
write (int fd, const void *buffer, unsigned size) {
  if (fd == STDOUT_FILENO) {
    putbuf (buffer, size);
  }
  return size;
}