#include "workqueue.h"
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>

typedef struct w_task{
    int id;
}w_task;

// should probably make a queue helper
struct wq{
    size_t queue_capacity;
    size_t num_workers;
    int front;
    int rear;
    pthread_t *threads;
    w_task *items;
}Queue;

//make an empty queue
void initialzeQueue(struct wq *q){
    q->front = -1;
    q->rear = -1;
}
//is queue empty
bool is_wq_empty(struct wq *q){
    return (q->front == -1);
}
//is queue full
bool is_wq_full(struct wq *q){
    return (q->rear == q->queue_capacity-1);
}
//add an item to the queue
void enqueue(struct wq *q, w_task task){
    if (is_wq_full(q)){
        fprintf(stderr, "queue is full\n");
    }
    if (is_wq_empty(q)){
        q->front = 0;
    }
    q->rear++;
    q->items[q->rear] = task;
}
//remove an item from the queue
w_task dequeue(struct wq *q){
    w_task task;
    if (!is_wq_empty(q)){
        task = q->items[q->front];
    }
    if (q->front == q->rear){
        q->front = -1;
        q->rear = -1;
    }else{
        q->front++;
    }
    return task;
}


//we might need this later currently a placeholder for a function to run when the thread is created 
void *tfn(void *arg){
    int *ptr_to_int = malloc(sizeof(int));
	*ptr_to_int = 47;
	return ptr_to_int;
}

// create a queue and worker threads (threadpool)
wq_t *wq_create(size_t num_workers, size_t queue_capacity){
    struct wq *q = calloc(1, sizeof(wq_t));
    q->front = -1;
    q->rear = -1;
    q->queue_capacity = queue_capacity;
    q->num_workers = num_workers;
    q->items = calloc(queue_capacity, sizeof(w_task));
    q->threads = calloc(num_workers, sizeof(pthread_t));
    for (size_t task_id = 0; task_id < num_workers-1; task_id++){
        pthread_create(&q->threads[task_id], NULL, tfn, NULL); //takes thread id, attributes, function, and arguments
    }
    return q;
}


// submit a work task to the work queue, and wait for an availible worker thread to complete it. 
wq_job_id_t wq_submit(wq_t *q, wq_job_fn fn, void *arg){
    // a task consists of a pointer to a function of type wq_job_fn (we need to make this), void* arguement. 
    //if the queue is full we wait 
    // once task is added the function assgins the task an id and returns the id back to the calling function
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
    free(q);
    return;
}