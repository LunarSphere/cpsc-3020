/*
Student: Kevius Tribble
Instructor: Dr. Jacob Sorber
CPSC 3020
10/9/2025
Implementation of a simple workqueue library. create threadsafe tasks to be executed concurrently by a thread pool.
*/

#include "workqueue.h"
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_TASKS 9999

// track state of a queued task 
typedef enum{
    SUBMITTED,
    DONE
}task_state;

// represent a task in the work queue
typedef struct w_task{
    wq_job_id_t id;
    wq_job_fn fn;
    void *arg;
}w_task;

/*
* A circular queue of tasks to be executed by a thread pool. 
*/
//little note: conditionals > spinlocks because spinlocks(busy wait) burns a cpu core doing nothing
struct wq{
    size_t queue_capacity;
    size_t num_workers;
    int front;
    int rear;
    pthread_t *threads;
    w_task *tasks;
    task_state *task_states;
    wq_job_id_t next_task_id;
    pthread_mutex_t lock; // so we can lock the queue when we are enqueuing or dequeuing tasks
    pthread_cond_t not_empty; // signal workers that their is work in queue
    pthread_cond_t not_full; // tell submitter their is space to submit tasks
    pthread_cond_t done; // tell thread pool that task is done
    int shutdown;
};

//make an empty queue
static void initQueue(struct wq *q){
    q->front = -1;
    q->rear = -1;
}

//is queue empty
static bool is_wq_empty(struct wq *q){
    return (q->front == -1);
}
//is queue full
static bool is_wq_full(struct wq *q){
    return ((q->rear + 1) % q->queue_capacity == q->front);
}
//add an item to the queue
static void enqueue(struct wq *q, w_task task){
    if (is_wq_full(q)){
        fprintf(stderr, "queue is full\n");
        return;
    }
    if (is_wq_empty(q)){
        q->front = 0;
    }
    q->rear = (q->rear + 1) % q->queue_capacity;
    q->tasks[q->rear] = task;
}
//remove an item from the queue
static w_task dequeue(struct wq *q){
    if (is_wq_empty(q)){
        w_task empty_task = {0, NULL, NULL};
        return empty_task;
    }
    
    w_task task = q->tasks[q->front];
    if (q->front == q->rear){
        initQueue(q);
    } else {
        q->front = (q->front + 1) % q->queue_capacity;
    }
    return task;
}


//start routine for the thread
// imagine a thread spawns and just starts following its daily routine. 
/*
* takes void ptr | so thread join has a ** ptr and tfn has a * this is useful for if we want the thread
* to change something permanently
*/ 
static void *tfn(void *arg){
    struct wq *q = arg; 
    while (true){
        // lock the queue 
        pthread_mutex_lock(&q->lock);
        // while queue is empty and not shutdown
        while (is_wq_empty(q) && !q->shutdown){
            //release the lock, sleep until not empty is signaled, reacquire lock
            pthread_cond_wait(&q->not_empty, &q->lock); 
        }
        // if shut down signal signal not empty, unlock, wake up other workers so they can exit
        if (q->shutdown){
            pthread_cond_signal(&q->not_empty);
            pthread_mutex_unlock(&q->lock);
            break;
        }
        
        // once a task is added we dequeue the task and execute it
        w_task task = dequeue(q);
        // unlock the queue
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);
        // execute tasks function with its args
        wq_job_id_t completed_task_id = task.id;
        task.fn(task.arg);
        //mark the task as done | critical section make sure other threads cant edit this
        pthread_mutex_lock(&q->lock);
        q->task_states[completed_task_id] = DONE; // this will index 
        pthread_cond_signal(&q->done);
        pthread_mutex_unlock(&q->lock);
    }
    return NULL;
}

// initialize a queue and worker threads (threadpool)
wq_t *wq_create(size_t num_workers, size_t queue_capacity){
    struct wq *q = calloc(1, sizeof(wq_t));
    initQueue(q);
    q->queue_capacity = queue_capacity;
    q->num_workers = num_workers;
    q->task_states = calloc(MAX_TASKS, sizeof(task_state));
    q->tasks = calloc(queue_capacity, sizeof(w_task));
    q->threads = calloc(num_workers, sizeof(pthread_t));
    q->next_task_id = 1;
    q->shutdown = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->done, NULL);
    for (size_t task_id = 0; task_id < num_workers; task_id++){
        //takes thread ids, attributes, function, and the queue as an argument to the function
        pthread_create(&q->threads[task_id], NULL, tfn, q); 
    }
    return q;
}


/*
* takes queue, jobfn and arguments for jobfn
* submits job to the queue for execution
*/
wq_job_id_t wq_submit(wq_t *q, wq_job_fn fn, void *arg){
    // a task consists of a pointer to a function of type wq_job_fn and a void* arguement. 
    // indicate errors by returning a -1 | queue is empty no function provided
    if (!q || !fn){
        return -1;
    }
    //wait for threads to finish queued tasks then submit a new task
    pthread_mutex_lock(&q->lock);
    while (is_wq_full(q) && !q->shutdown){
        pthread_cond_wait(&q->not_full, &q->lock); 
    }
    // once task is added the function assgins the task an id and returns the id back to the calling function
    w_task submitted_task;
    q->next_task_id++;
    submitted_task.id = q->next_task_id;
    submitted_task.fn = fn;
    submitted_task.arg = arg;
    q->task_states[submitted_task.id] = SUBMITTED;
    enqueue(q, submitted_task);
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);    
    return submitted_task.id;
    
}

// wait for all the ids we submitted to finish
// ill need to see if a task is done by checking its state. 
// easier to check if a task is in processes and once its not in queue or being worked on if its not one of these its done
void wq_wait(wq_t *q, wq_job_id_t *ids, int numids){
    // takes an array of task ids
    // waits until they all execute
    wq_job_id_t task_id;
    for (int i = 0; i < numids; i++){
        task_id = ids[i];
        // do nothing until state is done
        pthread_mutex_lock(&q->lock);
        while (!(q->task_states[task_id] == DONE) && !q->shutdown){
            pthread_cond_wait(&q->done, &q->lock); // sleep until done is signaled
        }
        pthread_mutex_unlock(&q->lock);
    }
}

// frees everything created by the queue and joins the threads
void wq_shutdown(wq_t *q){
    pthread_mutex_lock(&q->lock);
    //shutdown
    q->shutdown = 1;
    pthread_cond_signal(&q->done);
    pthread_cond_signal(&q->not_empty);
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    //suspend main thread until thread in thread pool finishes| wait for workers to finish executing tasks
    for (size_t i = 0; i < q->num_workers; i++){
        pthread_join(q->threads[i], NULL);
    }
    // free pthread inits
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->done);
    // Free Callocs
    free(q->task_states);
    free(q->threads);
    free(q->tasks);
    free(q);
    return;
}