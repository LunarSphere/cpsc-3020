#include "workqueue.h"
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>

typedef struct w_task{
    wq_job_id_t id;
    wq_job_fn fn;
    void *arg;
}w_task;

// should probably make a queue helper
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
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
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
    w_task task = {0, NULL, NULL};
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
static void *tfn(void *arg){
    struct wq *q = (struct wq *)arg; 
    while (true){
        // lock the queue
        // while the queue is empty we wait for a task to be added
        // once a task is added we dequeue the task and execute it
        // unlock the queue
        // execute tasks function with its argument
        pthread_mutex_lock(&q->lock);
        while (is_wq_empty(q) && !q->shutdown){
            pthread_cond_wait(&q->not_empty, &q->lock);
        }
        if (q->shutdown){
            pthread_cond_signal(&q->not_empty);
            pthread_mutex_unlock(&q->lock);
            break;
        }
        w_task task = dequeue(q);
        pthread_cond_signal(&q->not_full);
        pthread_mutex_unlock(&q->lock);
        task.fn(task.arg);
    }
    return NULL;
}

// create a queue and worker threads (threadpool)
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
    for (size_t task_id = 0; task_id < num_workers; task_id++){
        pthread_create(&q->threads[task_id], NULL, tfn, (void *)q); //takes thread id, attributes, function, and the queue as an argument to the function
    }
    return q;
}


// submit a work task to the work queue, and wait for an availible worker thread to complete it. 
wq_job_id_t wq_submit(wq_t *q, wq_job_fn fn, void *arg){
    // a task consists of a pointer to a function of type wq_job_fn and a void* arguement. 
    //while the queue is full we wait 
    if (!q || !fn){
        return -1;
    }
    pthread_mutex_lock(&q->lock);
    while (is_wq_full(q) && !q->shutdown){
        pthread_cond_wait(&q->not_full, &q->lock);
    }
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
    enqueue(q, submitted_task);
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);    
    return submitted_task.id;
    // task ids should be unique positive integers
    // indicate errors by returning a -1. 
}

void wq_wait(wq_t *q, wq_job_id_t *ids, int numids){
    // takes an array of task ids
    // waits until they all execute
    wq_job_id_t thread_id;
    for (int i = 0; i < numids; i++){
        thread_id = ids[i];
        pthread_join(q->threads[thread_id], NULL);
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
    free(q->threads);
    free(q->tasks);
    free(q);
    return;
}