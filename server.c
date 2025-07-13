#include "segel.h"
#include "request.h"
#include "log.h"

#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdbool.h>

#define MAX_QUEUE_SIZE 1024
#define THREAD_POOL_SIZE 4

int *active_connections = NULL;
int all_requests = 0, thread_pool_size = 0, request_limit = 0;
pthread_t *thread_pool = NULL;

typedef struct {
    int fd;
    struct timeval arrival;
} request_t;

typedef struct {
    request_t buffer[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} request_queue_t;

request_queue_t queue;
server_log global_log;

threads_stats thread_stats_pool[THREAD_POOL_SIZE];

void init_queue(request_queue_t* q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void enqueue(request_queue_t* q, int fd, struct timeval arrival) {
    pthread_mutex_lock(&q->lock);
    while (q->count == MAX_QUEUE_SIZE) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    q->buffer[q->rear].fd = fd;
    q->buffer[q->rear].arrival = arrival;
    q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}

request_t dequeue(request_queue_t* q) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    request_t req = q->buffer[q->front];
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return req;
}

void* worker_thread(void* arg) {
    int tid = *(int*)arg;
    threads_stats* t = &thread_stats_pool[tid];
    (*t)->id = tid;
    (*t)->stat_req = 0;
    (*t)->dynm_req = 0;
    (*t)->post_req = 0;
    (*t)->total_req = 0;
    while (true) {
        request_t req = dequeue(&queue);
        struct timeval dispatch;
        gettimeofday(&dispatch, NULL);
        requestHandle(req.fd, req.arrival, dispatch, t, global_log);
        Close(req.fd);
    }
    return NULL;
}

void getargs(int *port, int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    request_limit = atoi(argv[3]);
}

int main(int argc, char *argv[]) {
    int listenfd, port, clientlen;
    struct sockaddr_in clientaddr;
    threads_stats *t_stats;

    getargs(&port, argc, argv);

    global_log = create_log();
    init_queue(&queue);
    init(&t_stats);

    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        int connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);

        struct timeval arrival;
        gettimeofday(&arrival, NULL);

        enqueue(&queue, connfd, arrival);
    }

    destroy_log(global_log);
    quit(t_stats, 0);
    return 0;
}

void init(threads_stats **t_stats)
{

    *t_stats = malloc(THREAD_POOL_SIZE * sizeof(threads_stats));
    thread_pool = malloc(thread_pool_size * sizeof(pthread_t));
    active_connections = malloc(THREAD_POOL_SIZE * sizeof(int));

    init_queue(&queue);

    if (*t_stats == NULL || active_connections == NULL)
    {
        perror("Failed to allocate memory");
        quit(*t_stats, 1);
    }
    
     pthread_t threads[THREAD_POOL_SIZE];
    int tids[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, worker_thread, &tids[i]);
    }
}



void quit(threads_stats *t_stats, int exit_status)
{
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_join(thread_pool[i], NULL);
        if (t_stats)
            free(t_stats[i]);
    }

    free(thread_pool);
    free(t_stats);
    free(active_connections);
}


/*int block(int connfd, int is_vip)
{
    Request request = malloc(sizeof(struct request));
    if (!request)
    {
        quit(NULL, 1);
    }

    request->connfd = connfd;
    gettimeofday(&request->arrival, NULL);

    pthread_mutex_lock(&waiting_queue_m);

    while (all_requests >= request_limit)
    {
        pthread_cond_wait(&free_space, &waiting_queue_m);
    }
   
    all_requests++;

    if (is_vip){
        queue_enqueue(&waiting_vip_queue, request);
        pthread_cond_signal(&vip_not_empty);
    }
    else {
        queue_enqueue(&waiting_reg_queue, request);
        if (waiting_vip_queue->size == 0)
            pthread_cond_signal(&reg_not_empty__vip_empty);
    }

    pthread_mutex_unlock(&waiting_queue_m);
    return 0;
}*/

