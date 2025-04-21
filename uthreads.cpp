#include "uthreads.h"
#include <iostream>
#include <array>
#include <queue>
#include <deque>
#include <signal.h>
#include <sys/time.h>
#include <map>
#include <csetjmp>
#include <setjmp.h>
#include <algorithm>

#define INIT_ERR "thread library error: non-positive number of quantum_usecs"
#define SIGACTION_ERR "system error: sigaction error"
#define SETITIMER_ERR "system error: setitimer error"
#define SPAWN_ERR "thread library error: null _entry_point"
#define LIMIT_NUM_OF_TRD_ERR "thread library error: maximum number of _threads"
#define TID_NOT_EXISTS_ERR "thread library error: tid not exists"
#define BLOCK_MAIN_THREAD_ERR "thread library error: trying to block main thread"
#define BLOCK_TID_NOT_EXISTS_ERR "thread library error: trying to block non-existing thread"
#define RESUME_NOT_EXISTS_TID_ERR "thread library error: trying to resume non-existing thread"
#define MAIN_THREAD_CALL_SLEEP_ERR "thread library error: trying to sleep the main thread"
#define NOT_VALID_QUANTUM_NUM "thread library error: number of quantums is not valid"
#define MEMORY_ALOC_ERR "system error: memory allocation failed\n"

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

/* States (ENUM) */

enum State
{
    RUNNING, READY, BLOCKED
};

void timer_handle (int sig);

/* THE THREAD */

class Thread
{
 private:
  int _tid;
  thread_entry_point _entry_point;
  char *_stack;
  int _running_quantum_counter;
  address_t _sp;
  address_t _pc;
  State _state;
  bool _is_sleeping;
  int _sleeping_counter;

 public:
  Thread (int tid, thread_entry_point entry_point)
  {
    _tid = tid;
    entry_point = entry_point;

    _stack = new (std::nothrow) char[STACK_SIZE];
    if (_stack == nullptr) {
      fprintf(stderr, MEMORY_ALOC_ERR);
      exit(1);
    }

    _running_quantum_counter = 1;
    _sp = (address_t) _stack + STACK_SIZE - sizeof (address_t);
    _pc = (address_t) entry_point;
    _state = READY;
    _is_sleeping = false;
    _sleeping_counter = 0;
  }

  ~Thread ()
  {
    delete[] _stack;
  }

  address_t get_sp () const
  {
    return _sp;
  }

  address_t get_pc () const
  {
    return _pc;
  }

  int get_quantum_counter () const
  {
    return _running_quantum_counter;
  }

  void inc_quantum_counter ()
  {
    _running_quantum_counter++;
  }

  State get_state ()
  {
    return _state;
  }

  void set_state (State state)
  {
    _state = state;
  }

  bool get_is_sleeping () const
  {
    return _is_sleeping;
  }
  void set_sleeping_counter (int sleeping_counter)
  {
    _sleeping_counter = sleeping_counter;
    _is_sleeping = true;
  }
  void dec_sleeping_counter ()
  {
    _sleeping_counter--;
    if (_sleeping_counter == 0)
    {
      _is_sleeping = false;
    }
  }
};

/* Inner Class */


class ThreadManager
{
 private:
  std::map<int, Thread *> _threads{};
  std::map<int, sigjmp_buf> _env{};
  std::deque<int> _ready_queue{};
  std::array<int, MAX_THREAD_NUM> _free_tids = {};
  int _running_thread{};
  int _time_per_thread{};
  int _quantum_counter{};
  struct sigaction _sa = {0};
  struct itimerval _timer{};
  int _main_thread_quantums;

  int next_free_tid ()
  {
    // TODO - is the 0 thread is part of 100 _threads?
    for (int i = 0; i < MAX_THREAD_NUM; i++)
    {
      if (_free_tids[i] == 0)
      {
        _free_tids[i] = 1;
        return i;
      }
    }
    return -1;
  }

 public:
  ThreadManager ()
  = default;

  void init (int quantum_usecs)
  {
    _main_thread_quantums = 1;
    _time_per_thread = quantum_usecs;
    _running_thread = 0;
    _quantum_counter = 1;
    _free_tids[0] = 1;
    setup_thread (0);
  }

  void start_timer ()
  {
    _sa.sa_handler = &timer_handle;
    if (sigaction (SIGVTALRM, &_sa, nullptr) < 0)
    {
      fprintf (stderr, SIGACTION_ERR);
      exit (1);
    }

    _timer.it_value.tv_sec = 0;
    _timer.it_value.tv_usec = _time_per_thread;
    _timer.it_interval.tv_sec = 0;
    _timer.it_interval.tv_usec = _time_per_thread;

    if (setitimer (ITIMER_VIRTUAL, &_timer, nullptr))
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
    //Thread thread = Thread (tid, entry_point);
    Thread* thread = new (std::nothrow) Thread(tid, entry_point);
    if (thread == nullptr) {
      fprintf(stderr, MEMORY_ALOC_ERR);
      _free_tids[tid] = 0;
      exit(1);
    }
    _threads[tid] = thread;
    _ready_queue.push_back (tid);
    setup_thread (tid);
    return tid;
  }

  void remove_thread (int tid)
  {
    delete &_threads[tid];
    _threads.erase (tid);
    _env.erase (tid);
    _free_tids[tid] = 0;
    erase_tid_from_queue (tid);
  }

  void remove_all ()
  {
    for (int i = 1; i < MAX_THREAD_NUM; i++)
    {
      if (_free_tids[i] == 1)
      {
        remove_thread (i + 1);
      }
    }
    _env.erase (0);
  }

  void erase_tid_from_queue (int tid)
  {
    _ready_queue.erase (std::remove (_ready_queue.begin (), _ready_queue.end (), tid),
                        _ready_queue.end ());
  }

  bool is_tid_exists (int tid)
  {
    if (tid < 0 || tid >= MAX_THREAD_NUM)
    {
      return false;
    }
    return _free_tids[tid] == 1;
  }

  int get_running_tid () const
  {
    return _running_thread;
  }

  int setup_thread (int tid)
  {
    sigsetjmp(_env[tid], 1);
    if (tid != 0)
    {
      (_env[tid]->__jmpbuf)[JB_SP] = translate_address (_threads[tid]->get_sp ());
      (_env[tid]->__jmpbuf)[JB_PC] = translate_address (_threads[tid]->get_pc ());
    }
    sigemptyset (&_env[tid]->__saved_mask);
  }

  int get_next_ready_tid ()
  {
    int cur_tid;
    while (not _ready_queue.empty ())
    {
      cur_tid = _ready_queue.front ();
      if (_free_tids[cur_tid] == 1)
      {
        return cur_tid;
      }
      _ready_queue.pop_front ();
    }
    return 0;
  }

  void switch_thread (int is_cur_terminated = 0)
  {
    int cur_tid = _running_thread;
    int next_tid = get_next_ready_tid ();
    if (next_tid != 0)
    {
      _ready_queue.pop_front ();
      _threads[next_tid]->set_state (RUNNING);
    }

    // save the current thread(if flag=0)
    int ret_val = 0;
    if (is_cur_terminated == 0)
    {
      ret_val = sigsetjmp(_env[cur_tid], 1);
    }

    // TODO - if cur_tid = 0 , do we need to push is to the queue?
    start_timer ();
    if (ret_val == 0)
    {
      if (!(_ready_queue.empty ()) && cur_tid != 0 && is_cur_terminated == 0
          && _threads[cur_tid]->get_state () == RUNNING)
      {
        if (not _threads[cur_tid]->get_is_sleeping ())
        {
          _ready_queue.push_back (cur_tid);
        }
        _threads[cur_tid]->set_state (READY);
      }

      //update cur thread
      _running_thread = next_tid;
      _quantum_counter++;
      if (next_tid == 0){
        _main_thread_quantums++;
      }
      else{
        _threads[next_tid]->inc_quantum_counter ();
      }
      manage_sleepers ();


      // jump to the next thread
      siglongjmp (_env[_running_thread], 1);
    }
  }

  int get_quantum_counter_of_tid (int tid)
  {
    if (tid == 0){
      return _main_thread_quantums;
    }
    else{
      return _threads[tid]->get_quantum_counter ();
    }
  }

  int get_total_quantum_counter () const
  {
    return _quantum_counter;
  }

  void block_thread (int tid)
  {
    _threads[tid]->set_state (BLOCKED);
    if (tid == _running_thread)
    {
      switch_thread ();
    }
    else
    {
      erase_tid_from_queue (tid);
    }
  }

  void resume_thread (int tid)
  {
    if (tid != 0 && _threads[tid]->get_state () == BLOCKED)
    {
      _threads[tid]->set_state (READY);
      _ready_queue.push_back (tid);
    }
  }

  void sleep_running_thread (int num_quantums)
  {
    _threads[_running_thread]->set_sleeping_counter (num_quantums + 1);
    switch_thread ();
  }

  void manage_sleepers ()
  {
    for (auto pair: _threads)
    {
      if (pair.second->get_is_sleeping ())
      {
        pair.second->dec_sleeping_counter ();
        if (pair.second->get_is_sleeping () && pair.second->get_state () !=
                                               BLOCKED)
        {
          _ready_queue.push_back (pair.first);
        }
      }
    }
  }
};

ThreadManager manager;

void timer_handle (int sig)
{
  manager.switch_thread (0);
}
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

int uthread_terminate (int tid)
{
  if (not manager.is_tid_exists (tid))
  {
    fprintf (stderr, TID_NOT_EXISTS_ERR);
    return -1;
  }
  if (tid == 0)
  {
    manager.remove_all ();
    exit (0);
  }
  manager.remove_thread (tid);
  if (manager.get_running_tid () == tid)
  {
    manager.switch_thread (1);
  }
  else
  {
    return 0;
  }
}

int uthread_block (int tid)
{
  if (tid == 0)
  {
    fprintf (stderr, BLOCK_MAIN_THREAD_ERR);
    return -1;
  }
  if (not manager.is_tid_exists (tid))
  {
    fprintf (stderr, BLOCK_TID_NOT_EXISTS_ERR);
    return -1;
  }

  manager.block_thread (tid);
  return 0;
}

int uthread_resume (int tid)
{
  if (not manager.is_tid_exists (tid))
  {
    fprintf (stderr, RESUME_NOT_EXISTS_TID_ERR);
    return -1;
  }
  manager.resume_thread (tid);
  return 0;
}

int uthread_sleep (int num_quantums)
{
  if (manager.get_running_tid () == 0)
  {
    fprintf (stderr, MAIN_THREAD_CALL_SLEEP_ERR);
    return -1;
  }
  if (num_quantums < 0)
  {
    fprintf (stderr, NOT_VALID_QUANTUM_NUM);
    return -1;
  }
  manager.sleep_running_thread (num_quantums);
  return 0;
}

int uthread_get_tid ()
{
  return manager.get_running_tid ();
}

int uthread_get_total_quantums ()
{
  return manager.get_total_quantum_counter ();
}

int uthread_get_quantums (int tid)
{
  if (not manager.is_tid_exists (tid))
  {
    fprintf (stderr, TID_NOT_EXISTS_ERR);
    return -1;
  }
  return manager.get_quantum_counter_of_tid (tid);
}

