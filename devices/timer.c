#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;
struct semaphore sema;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/*function for alarm - sleep*/
static void sema_sleep (struct semaphore *sema);
static void sema_awake (struct semaphore *sema, int64_t ticks);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
  /* 8254 input frequency divided by TIMER_FREQ, rounded to
     nearest. */
  uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

  outb (0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
  outb (0x40, count & 0xff);
  outb (0x40, count >> 8);

  sema = *(struct semaphore *) malloc (sizeof (struct semaphore));
  sema.waiters = *(struct list *) malloc (sizeof (struct list));
  sema_init (&sema, 0);

  intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) {
    loops_per_tick <<= 1;
    ASSERT (loops_per_tick != 0);
  }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'" PRIu64 " loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  barrier ();
  return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
  return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
  int64_t start = timer_ticks ();   //현재 ticks을 받아와 start에 저장

  ASSERT (intr_get_level () == INTR_ON);

  thread_current ()->tick_s = start + ticks;
  // 현재 thread의 tick_s에 OS시작부터 자야할 시간까지를
  // 포함한 ticks를 저장
  while (timer_elapsed (start) < ticks) {
    // TODO: sema_down과 큰 차이가 없으므로, 추후에도 문제가 없다면 대체할 것
    sema_sleep (&sema);   // 현재 thread를 semaphore에 넣고 재운다
  }
}

static void
sema_sleep (struct semaphore *sema) {
  enum intr_level old_level;     // interrupt를 막기위한 변수
  old_level = intr_disable ();   // interrupt 막기 시작
  while (sema->value == 0) {
    // value가 0이라면 이미 다른 누군가가 접근해있는 상태이니
    list_push_back (&sema->waiters, &thread_current ()->elem);
    // 대기 리스트에 넣고
    thread_block ();   // 현재 thread의 상태를 block으로 전환
  }
  sema->value--;   // while문에 걸렸거나 혹은 탈출했다면 해당 프로세스에
                   // 진입한거니 진입 checke
  intr_set_level (old_level);   // interrupt를 이전 상태로 돌림
}

void
sema_awake(struct semaphore *sema, int64_t ticks){ // sema가 깨는 타이밍을 잡는 함수
	struct thread *temp; // 임시 thread를 선언
	// enum intr_level old_level;
	size_t waiter_size = list_size(&sema->waiters);
	
		for (int i=0 ; i<waiter_size ; i++)	{ // sema->watier 가 비어있지 않다면 즉, 대기하는 애들이 있다면
			temp = list_entry (list_pop_front (&sema->waiters), struct thread, elem); // 제일 앞에 있는 애를 pop하고 임시 thread로 확장
			if (temp->tick_s <= ticks){ // 임시 thread의 tick_s이 time_interrupt의 ticks(OS ticks)보다 작다면 이제 깨어나야하니깐
				thread_unblock(temp); // 깨워주고
				sema->value++; // 프로세스에서 탈출하니깐 value++ 해줌
			}
			else
				list_push_back(&sema->waiters, &temp->elem);	// ticks의 시간이 안되었다면 다시 list 제일 뒤에 넣어줌
		}
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
  real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
  real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
  printf ("Timer: %" PRId64 " ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
  ticks++;
  thread_tick ();

  // timer_interrupt는 tick이 절대적으로 흐르니깐 해당
  // tick을 이용하여 sema를 깨운다
  sema_awake (&sema, ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
  while (loops-- > 0)
    barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.

     (NUM / DENOM) s
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
     1 s / TIMER_FREQ ticks
     */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0) {
    /* We're waiting for at least one full timer tick.  Use
       timer_sleep() because it will yield the CPU to other
       processes. */
    timer_sleep (ticks);
  } else {
    /* Otherwise, use a busy-wait loop for more accurate
       sub-tick timing.  We scale the numerator and denominator
       down by 1000 to avoid the possibility of overflow. */
    ASSERT (denom % 1000 == 0);
    busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
  }
}
