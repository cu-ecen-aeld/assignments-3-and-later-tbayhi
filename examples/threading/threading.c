// #define _GNU_SOURCE

#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
// #define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    // make the thread_data arguments accessible
    struct thread_data* thread_func_args = (struct thread_data*) thread_param;
    
    // wait to obtain
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // obtain the mutex
    pthread_mutex_lock(thread_func_args->mutex);

    // wait to release
    usleep(thread_func_args->wait_to_release_ms * 1000);

    // release the mutex
    pthread_mutex_unlock(thread_func_args->mutex);
    
    // update thread_param to indicate success
    thread_func_args->thread_complete_success = 1;

    // return thread_param
    return thread_param;
}

bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int retval = false;

    // dynamically allocate memory for thread data
    struct thread_data *thread_param = (struct thread_data*) malloc(sizeof(struct thread_data));

    // set up mutex and wait arguments
    thread_param->mutex = mutex;
    thread_param->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_param->wait_to_release_ms = wait_to_release_ms;
    thread_param->thread_complete_success = false;

    // create thread w/ thread paramters
    retval = pthread_create(thread, NULL, threadfunc, thread_param);

    if (retval) {
        errno = retval;
        perror("pthread_create");
    }

    // returns true if successful
    return !retval;
}

