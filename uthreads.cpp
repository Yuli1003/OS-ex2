#include "uthreads.h"
#include <iostream>
#include <array>
#include <queue>
#include <signal.h>
#include <sys/time.h>
#include <map>
#include <csetjmp>

#define INIT_ERR "thread library error: non-positive number of quantum_usecs"
#define SIGACTION_ERR "system error: sigaction error"
#define SETITIMER_ERR "system error: setitimer error"
#define SPAWN_ERR "thread library error: null _entry_point"
#define LIMIT_NUM_OF_TRD_ERR "thread library error: maximum number of _threads"

/* translate address */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

address_t translate_address (address_t addr)
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
 private:
  int _tid{};
  thread_entry_point _entry_point{};
  char *_stack;
  int _running_quantum_counter;
  address_t _sp;
  address_t _pc;

 public:
  Thread (int tid, thread_entry_point entry_point)
  {
    tid = tid;
    entry_point = entry_point;
    _stack = new char[STACK_SIZE];
    _running_quantum_counter = 0;
    _sp = (address_t) _stack + STACK_SIZE - sizeof(address_t);
    _pc = (address_t) entry_point;
  }

  address_t get_sp() {
    return _sp;
  }

  address_t get_pc() {
    return _pc;
  }
};

/* Inner Class */
void timer_handler (int sig)
{
  // TODO!!!
}

class ThreadManager
{
 public:
  ThreadManager ()
  {};

  void init (int quantum_usecs)
  {
    _time_per_thread = quantum_usecs;
    _running_thread = 0;
    _quantum_counter = 1;
  }

  void start_timer ()
  {
    _sa.sa_handler = &timer_handler;
    if (sigaction (SIGVTALRM, &_sa, NULL) < 0)
    {
      fprintf (stderr, SIGACTION_ERR);
      exit (1);
    }

    _timer.it_value.tv_sec = 0;
    _timer.it_value.tv_usec = _time_per_thread;
    _timer.it_interval.tv_sec = 0;
    _timer.it_interval.tv_usec = _time_per_thread;

    if (setitimer (ITIMER_VIRTUAL, &_timer, NULL))
    {
      fprintf (stderr, SETITIMER_ERR);
      exit (1);
    }
  }

  int add_thread (thread_entry_point entry_point)
  {
    int tid = next_free_tid ();
    if (tid == -1)
    {
      fprintf (stderr, LIMIT_NUM_OF_TRD_ERR);
      return -1;
    }
    Thread thread = Thread (tid, entry_point);
    _threads[tid] = &thread;
    _ready_queue.push(tid);
    return tid;
  }

  int setup_thread(tid) {

  }

 private:
  std::map<int, Thread *> _threads{};
  std::map<int, sigjmp_buf *> _env{};
  std::queue<int> _ready_queue{};
  int _free_tids[MAX_THREAD_NUM] = {0};
  int _running_thread;
  int _time_per_thread;
  int _quantum_counter;
  struct sigaction _sa = {0};
  struct itimerval _timer;

  int next_free_tid ()
  {
    // TODO - is the 0 thread is part of 100 _threads?
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
      if (_free_tids[i] == 0)
      {
        _free_tids[i] = 1;
        return i + 1;
      }
    }
    return -1;
  }
};

ThreadManager manager;

/* External interface */

int uthread_init (int quantum_usecs)
{
  if (quantum_usecs <= 0)
  {
    fprintf (stderr, INIT_ERR);
    return -1;
  }
  manager.init (quantum_usecs);
  manager.start_timer ();
  return 0;
}

int uthread_spawn (thread_entry_point entry_point)
{
  if (not entry_point)
  {
    fprintf (stderr, SPAWN_ERR);
    return -1;
  }

  return manager.add_thread (entry_point);
}

int uthread_terminate (int tid);

int uthread_block (int tid);

int uthread_resume (int tid);

int uthread_sleep (int num_quantums);

int uthread_get_tid ();

int uthread_get_total_quantums ();

int uthread_get_quantums (int tid);

