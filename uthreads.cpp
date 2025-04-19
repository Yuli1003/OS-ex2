#include "uthreads.h"
#include <iostream>
#include <array>
#include <queue>
#include <signal.h>
#include <sys/time.h>

#define INIT_ERR "system error: non-positive number of quantum_usecs"
#define SIGACTION_ERR "system error: sigaction error"
#define SETITIMER_ERR "system error: setitimer error"
#define SPAWN_ERR "system error: null entry_point"

/* translate address */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

address_t translate_address(address_t addr)
{
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
      : "=g" (ret)
      : "0" (addr));
  return ret;
}

/* THE THREAD */

class Thread
{
  // status - RUNNING/READY/BLOCKED
  // tid
  // env
  // blocked signals
  // entry point
  // stack
  // running_quantum_counter
};

/* Inner Class */
void timer_handler(int sig) {
  // TODO!!!
}

class ThreadManager
{
 public:
  ThreadManager ()
  {};

  void init (int quantum_usecs)
  {
    time_per_thread = quantum_usecs;
    running_thread = 0;
    quantum_counter = 1;
  }

  void start_timer() {
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0)
    {
      fprintf(stderr, SIGACTION_ERR);
      exit (1);
    }

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = time_per_thread;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = time_per_thread;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL))
    {
      fprintf (stderr, SETITIMER_ERR);
      exit (1);
    }
  }

 private:
  std::array<thread_entry_point, MAX_THREAD_NUM> threads{};
  std::queue<int> ready_queue{};
  int running_thread;
  int time_per_thread;
  int quantum_counter;
  struct sigaction sa = {0};
  struct itimerval timer;
};

ThreadManager manager;

/* External interface */

int uthread_init (int quantum_usecs)
{
  if (quantum_usecs <= 0)
  {
    fprintf (stderr, INIT_ERR);
    exit (1);
  }
  manager.init (quantum_usecs);

  exit (0);
}

int uthread_spawn (thread_entry_point entry_point);

int uthread_terminate (int tid);

int uthread_block (int tid);

int uthread_resume (int tid);

int uthread_sleep (int num_quantums);

int uthread_get_tid ();

int uthread_get_total_quantums ();

int uthread_get_quantums (int tid);

