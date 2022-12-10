#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN     0  /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX     63 /* Highest priority. */
#define FD_COUNT_LIMT 1<<9 /*page 하나의 크기가 1<<12인데 그중 3칸은 페이지 주소를 위해 할당됨 따라서 쓸수있는 크기는 1<<9 까지임*/
#define FD_PAGES 3

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  int priority;              /* Priority. */
  int64_t tick_s;            /* tick info for time check*/

  /* for project 1 -- start */
  int init_pri;
  struct lock *waitLock;
  struct list dona;
  struct list_elem dona_elem;
  /* for project 1 -- end */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

  /* for project 2 -- start */
  int exit_status; // 현재 파일의 status를 확인하기 위해서
  struct file **fd_table; // 프로세서는 파일 디스크립터를 관리하는 테이블이 필요함
  int fd_idx; // 그리고 그 파일 디스크립터 테이블에 들어가는 파일 디스크립터의 인덱스를 저장

  struct list child_s; // 부모가 자식 process의 정보를 기억할 공간
  struct list_elem child_elem; // child list elem
  
  struct intr_frame parent_if; // 부모의 intr_frame 정보를 기억할 공간
  struct semaphore fork_sema; // 자식이 fork가 완료 될때까지 기다리는 sema
  struct semaphore wait_sema; // process 가 동작중일땐 wait에서 대기를 해야만함 --> exit가 들어오기 전까지는 계속 대기
  struct semaphore exit_sema; // exit handler를 위한 sema --> 종료가 되기전에 wait와 fork 쪽이 먼저 정리가 되어야하니깐

  struct file *running; // rox를 위해 사용할 공간
  /* for project 2 -- end */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint64_t *pml4; /* Page map level 4 */

#endif
#ifdef VM
  /* Table for whole virtual memory owned by thread. */
  struct supplemental_page_table spt;
  void *stack_bottom;
  void *rsp_stack;
#endif

  /* Owned by thread.c. */
  struct intr_frame tf; /* Information for switching */
  unsigned magic;       /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
void do_iret (struct intr_frame *tf);

void test_max_priority (void);
bool compare_priority (const struct list_elem *input,
                       const struct list_elem *prev, void *aux UNUSED);
bool compare_dona_priority(const struct list_elem *input, const struct list_elem *prev, void *aux UNUSED);

/* Implement for Priority Donation */
void dona_priority (void);
void remove_lock (struct lock *lock);
void refresh_pri (void);

#endif /* threads/thread.h */
