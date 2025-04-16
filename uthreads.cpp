#include "uthreads.h"

/* External interface */


int uthread_init(int quantum_usecs);


int uthread_spawn(thread_entry_point entry_point);


int uthread_terminate(int tid);


int uthread_block(int tid);



int uthread_resume(int tid);



int uthread_sleep(int num_quantums);



int uthread_get_tid();



int uthread_get_total_quantums();



int uthread_get_quantums(int tid);

