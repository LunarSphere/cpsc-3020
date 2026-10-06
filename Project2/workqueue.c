#include "workqueue.h"
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

typedef enum{
    SUBMITTED,
    IN_PROG,
    DONE
}task_state;

// represent a task in the work queue
typedef struct w_task{
    wq_job_id_t id;
    wq_job_fn fn;
    void *arg;
    task_state state;
}w_task;

/*
* A work queue holds tasks that will be exeucted by a thread pool 
* This Queue is circular 
*/
struct wq{
    size_t queue_capacity;
    size_t num_workers;
    size_t count;
    int front;
    int rear;
    pthread_t *threads;
    w_task *tasks;
    wq_job_id_t next_task_id;
    pthread_mutex_t lock; // bundle sync logic and task data
    pthread_cond_t not_empty; // signal workers that their is work in queue
    pthread_cond_t not_full; // tell submitter their is space to submit tasks
    pthread_cond_t done; // tell wq that the task is done
    int shutdown;
};

//make an empty queue
static void initQueue(struct wq *q){
    q->front = 0;
    q->rear = 0;
    q->count = 0;
}
//is queue empty
static bool is_wq_empty(struct wq *q){
    return (q->count == 0);
}
//is queue full
static bool is_wq_full(struct wq *q){
    return (q->count == q->queue_capacity);
}
//add an item to the queue
static void enqueue(struct wq *q, w_task task){
    if (is_wq_full(q)){
        fprintf(stderr, "queue is full\n");
        return;
    }
    q->tasks[q->rear] = task;
    q->rear = (q->rear + 1) % q-> queue_capacity;
    q->count++;
}
//remove an item from the queue
static w_task dequeue(struct wq *q){
    w_task task = {0, NULL, NULL, IN_PROG};
    if (is_wq_empty(q)){
        fprintf(stderr, "queue is empty\n");
        return task;
    }
    task = q->tasks[q->front];
    q->front = (q->front + 1) % q->queue_capacity;
    q->count--;
    return task;
}


//start routine for the thread
// image a thread spawns and just starts following its daily routine.  
// threads never terminate until shutdown is set. so join takes fover
// wait is waiting for task ids not thread ids. 
// come up with a way to keep track with if a task is still in progress. 
// track states: task could still be in, worker thread could be working on it. or it could be finished. 
static void *tfn(void *arg){
    struct wq *q = (struct wq *)arg; 
    while (true){
        // lock the queue
        pthread_mutex_lock(&q->lock);
        // while the queue is empty and not shutdown we wait for a task to be added
        while (is_wq_empty(q) && !q->shutdown){
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        // if shut down signal signal not empty, unlock queue, signal not empty, and break
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
        //mark the task as done
        pthread_mutex_lock(&q->lock);
        q->tasks[completed_task_id % q->queue_capacity].state = DONE;
        pthread_cond_signal(&q->done);
        pthread_mutex_unlock(&q->lock);
    }
    return NULL;
}

static bool is_task_done(struct wq *q, wq_job_id_t task_id){
    return (q->tasks[task_id % q->queue_capacity].state == DONE);
}

// initialize a queue and worker threads (threadpool)
wq_t *wq_create(size_t num_workers, size_t queue_capacity){
    struct wq *q = calloc(1, sizeof(wq_t));
    initQueue(q);
    q->queue_capacity = queue_capacity;
    q->num_workers = num_workers;
    q->tasks = calloc(queue_capacity, sizeof(w_task));
    q->threads = calloc(num_workers, sizeof(pthread_t));
    q->next_task_id = 1;
    q->shutdown = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->done, NULL);
    for (size_t task_id = 0; task_id < num_workers; task_id++){
        pthread_create(&q->threads[task_id], NULL, tfn, (void *)q); //takes thread id, attributes, function, and the queue as an argument to the function
    }
    return q;
}


// submit a work task to the work queue, and wait for an availible worker thread to complete it. 
// task ids should be unique positive integers
wq_job_id_t wq_submit(wq_t *q, wq_job_fn fn, void *arg){
    // a task consists of a pointer to a function of type wq_job_fn and a void* arguement. 
    // indicate errors by returning a -1. 
    if (!q || !fn){
        return -1;
    }
    //while the queue is full we wait
    pthread_mutex_lock(&q->lock);
    while (is_wq_full(q) && !q->shutdown){
        pthread_cond_wait(&q->not_full, &q->lock); // lock submission until q is not full
    }
    // abandon submitted task
    if (q->shutdown){
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    // once task is added the function assgins the task an id and returns the id back to the calling function
    w_task submitted_task;
    q->next_task_id ++;
    submitted_task.id = q->next_task_id;
    submitted_task.fn = fn;
    submitted_task.arg = arg;
    submitted_task.state = SUBMITTED;
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
        task_id = ids[i] % q->queue_capacity;
        // do nothing until state is done
        pthread_mutex_lock(&q->lock);
        while (!is_task_done(q, task_id) && !q->shutdown){
            pthread_cond_wait(&q->done, &q->lock);
        }
        pthread_mutex_unlock(&q->lock);
        pthread_cond_signal(&q->not_empty);
    }
}

// when called all worker threads complete their task and exit, abandon any tasks remaining on queue
// free any allocated memory for queue and thread pool
void wq_shutdown(wq_t *q){
    // free thread pool
    // free worker queue
    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_signal(&q->not_empty);
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    //maybe a join so workers can finish their tasks before we free the memory
    for (size_t i = 0; i < q->num_workers; i++){
        pthread_join(q->threads[i], NULL);
    }
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->done);
    free(q->threads);
    free(q->tasks);
    free(q);
    return;
}