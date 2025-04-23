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

#define INIT_ERR "thread library error: non-positive number of quantum_usecs\n"
#define SIGACTION_ERR "system error: sigaction error\n"
#define SETITIMER_ERR "system error: setitimer error\n"
#define SPAWN_ERR "thread library error: null _entry_point\n"
#define LIMIT_NUM_OF_TRD_ERR "thread library error: maximum number of _threads\n"
#define TID_NOT_EXISTS_ERR "thread library error: tid not exists\n"
#define BLOCK_MAIN_THREAD_ERR "thread library error: trying to block main thread\n"
#define BLOCK_TID_NOT_EXISTS_ERR "thread library error: trying to block non-existing thread\n"
#define RESUME_NOT_EXISTS_TID_ERR "thread library error: trying to resume non-existing thread\n"
#define MAIN_THREAD_CALL_SLEEP_ERR "thread library error: trying to sleep the main thread\n"
#define NOT_VALID_QUANTUM_NUM "thread library error: number of quantums is not valid\n"
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
    _running_quantum_counter = 0;
    _state = READY;
    _is_sleeping = false;
    _sleeping_counter = 0;

    if (tid == 0) {          // ← main thread: אין stack חדש
      _stack = nullptr;
      _sp = 0;
      _pc = 0;
    } else {
      _stack = new (std::nothrow) char[STACK_SIZE];
      if (_stack == nullptr) {
        fprintf (stderr, MEMORY_ALOC_ERR);
        exit (1);
      }
      _sp = (address_t)_stack + STACK_SIZE - sizeof(address_t);
      _pc = (address_t)entry_point;
    }
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
  int _pending_delete = -1;

  int next_free_tid ()
  {
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
    _time_per_thread = quantum_usecs;
    _running_thread = 0;
    _quantum_counter = 1;

    _threads[0] = new Thread(0, nullptr);
    _threads[0]->set_state(RUNNING);
    _threads[0]->inc_quantum_counter();
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
    if (not entry_point)
    {
      fprintf (stderr, SPAWN_ERR);
      return -1;
    }
    int tid = next_free_tid ();
    if (tid == -1)
    {
      fprintf (stderr, LIMIT_NUM_OF_TRD_ERR);
      return -1;
    }
    Thread *thread = new (std::nothrow) Thread (tid, entry_point);
    if (thread == nullptr)
    {
      fprintf (stderr, MEMORY_ALOC_ERR);
      _free_tids[tid] = 0;
      exit (1);
    }
    _threads[tid] = thread;
    _ready_queue.push_back (tid);
    setup_thread (tid);
    return tid;
  }

  void remove_thread (int tid)
  {
    delete _threads[tid];
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
        remove_thread (i);
      }
    }
    remove_thread(0);
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

  void setup_thread (int tid)
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
    while (! _ready_queue.empty()) {
      int tid = _ready_queue.front();
      _ready_queue.pop_front();
      if (_free_tids[tid] == 1) {
        return tid;
      }
    }
    return 0;
  }

  void switch_thread (int is_cur_terminated = 0)
  {
    int cur_tid = _running_thread;
    int next_tid = get_next_ready_tid ();

    // save the current thread(if flag=0)
    int ret_val = 0;
    if (is_cur_terminated == 0)
    {
      ret_val = sigsetjmp(_env[cur_tid], 1);
    }

    start_timer ();
    if (ret_val == 0)
    {
      if (is_cur_terminated == 0 && cur_tid != next_tid &&
          _threads[cur_tid]->get_state() == RUNNING &&
          !_threads[cur_tid]->get_is_sleeping())
      {
        _threads[cur_tid]->set_state(READY);
        _ready_queue.push_back(cur_tid);
      }

      //update cur thread
      _running_thread = next_tid;
      _quantum_counter++;

      _threads[next_tid]->inc_quantum_counter ();
      _threads[next_tid]->set_state(RUNNING);
      manage_sleepers ();

      if (_pending_delete != -1){
        _free_tids[_pending_delete] = 0;
      }

      siglongjmp (_env[_running_thread], 1);
    }

    else {

      if (_pending_delete != -1)
      {
        int dead = _pending_delete;
        _pending_delete = -1;
        remove_thread (dead);
      }
    }
  }

  int get_quantum_counter_of_tid (int tid)
  {
    if (not is_tid_exists (tid))
    {
      fprintf (stderr, TID_NOT_EXISTS_ERR);
      return -1;
    }

    return _threads[tid]->get_quantum_counter ();
  }

  int get_total_quantum_counter () const
  {
    return _quantum_counter;
  }

  int terminate_thread(int tid) {

    if (tid == 0) {
      if (_running_thread == tid)
      {
        remove_all ();
        _threads.clear ();
        _env.clear ();
        exit (0);
      }
      else {
        while (_running_thread != 0){
          switch_thread ();
        }
        remove_all ();
        _threads.clear ();
        _env.clear ();
        exit(0);
      }
    }
    if (_running_thread == tid) {
      _pending_delete = tid;
      switch_thread(1);
      return 0;
    }
    else {
      remove_thread(tid);
      return 0;
    }
  }


  int block_thread (int tid)
  {
    if (tid == 0)
    {
      fprintf (stderr, BLOCK_MAIN_THREAD_ERR);
      return -1;
    }
    if (not is_tid_exists (tid))
    {
      fprintf (stderr, BLOCK_TID_NOT_EXISTS_ERR);
      return -1;
    }

    _threads[tid]->set_state (BLOCKED);
    if (tid == _running_thread)
    {
      switch_thread ();
    }
    else
    {
      erase_tid_from_queue (tid);
    }
    return 0;
  }

  int resume_thread (int tid)
  {
    if (not is_tid_exists (tid))
    {
      fprintf (stderr, RESUME_NOT_EXISTS_TID_ERR);
      return -1;
    }
    if (tid != 0 && _threads[tid]->get_state () == BLOCKED)
    {
      _threads[tid]->set_state (READY);
      _ready_queue.push_back (tid);
    }
    return 0;
  }

  int sleep_running_thread (int num_quantums)
  {
    if (get_running_tid () == 0)
    {
      fprintf (stderr, MAIN_THREAD_CALL_SLEEP_ERR);
      return -1;
    }
    if (num_quantums < 0)
    {
      fprintf (stderr, NOT_VALID_QUANTUM_NUM);
      return -1;
    }
    _threads[_running_thread]->set_sleeping_counter (num_quantums + 1);
    switch_thread ();
    return 0;
  }

  void manage_sleepers ()
  {
    for (auto pair: _threads)
    {
      if (pair.second->get_is_sleeping ())
      {
        pair.second->dec_sleeping_counter ();
        if (!pair.second->get_is_sleeping () &&
        pair.second->get_state () !=BLOCKED)
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

sigset_t block_signals ()
{
  sigset_t new_mask, old_mask;
  sigemptyset (&new_mask);
  sigaddset (&new_mask, SIGVTALRM);
  sigprocmask (SIG_BLOCK, &new_mask, &old_mask);
  return old_mask;
}

void unblock_signals (sigset_t old_mask)
{
  sigprocmask (SIG_SETMASK, &old_mask, nullptr);
  sigset_t waiting_list;
  sigpending (&waiting_list);
  if (sigismember (&waiting_list, SIGVTALRM))
  {
    manager.switch_thread ();
  }
}

void cleanup_all_threads() {
  manager.remove_all();
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
  atexit(cleanup_all_threads);
  manager.start_timer ();
  return 0;
}

int uthread_spawn (thread_entry_point entry_point)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.add_thread (entry_point);
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_terminate (int tid)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.terminate_thread (tid);
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_block (int tid)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.block_thread (tid);
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_resume (int tid)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.resume_thread (tid);
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_sleep (int num_quantums)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.sleep_running_thread (num_quantums);
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_get_tid ()
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.get_running_tid ();
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_get_total_quantums ()
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.get_total_quantum_counter ();
  unblock_signals (blocked_sigs);
  return ret_val;
}

int uthread_get_quantums (int tid)
{
  sigset_t blocked_sigs = block_signals ();
  int ret_val = manager.get_quantum_counter_of_tid (tid);
  unblock_signals (blocked_sigs);
  return ret_val;
}

