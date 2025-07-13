#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "log.h"

#define INITIAL_CAPACITY 1024

// Opaque struct definition
struct Server_Log {
    char* buffer;
    int size;
    int capacity;

    pthread_mutex_t mutex;
    pthread_cond_t can_read;
    pthread_cond_t can_write;

    int readers;
    int writers_waiting;
    int writer_active;
};

// Creates a new server log instance
server_log create_log() {
    server_log log = malloc(sizeof(struct Server_Log));
    if (!log) return NULL;

    log->buffer = malloc(INITIAL_CAPACITY);
    if (!log->buffer) {
        free(log);
        return NULL;
    }

    log->size = 0;
    log->capacity = INITIAL_CAPACITY;
    log->readers = 0;
    log->writers_waiting = 0;
    log->writer_active = 0;

    pthread_mutex_init(&log->mutex, NULL);
    pthread_cond_init(&log->can_read, NULL);
    pthread_cond_init(&log->can_write, NULL);

    return log;
}

// Destroys and frees the log
void destroy_log(server_log log) {
    if (!log) return;

    free(log->buffer);
    pthread_mutex_destroy(&log->mutex);
    pthread_cond_destroy(&log->can_read);
    pthread_cond_destroy(&log->can_write);
    free(log);
}

// Appends a new entry to the log (thread-safe with writer priority)
void add_to_log(server_log log, const char* data, int data_len) {
    if (!log || !data || data_len <= 0) return;

    pthread_mutex_lock(&log->mutex);

    // Writer priority: wait until no active writer or readers
    log->writers_waiting++;
    while (log->writer_active || log->readers > 0) {
        pthread_cond_wait(&log->can_write, &log->mutex);
    }
    log->writers_waiting--;
    log->writer_active = 1;

    // Ensure enough space
    if (log->size + data_len >= log->capacity) {
        int new_capacity = log->capacity * 2;
        while (log->size + data_len >= new_capacity)
            new_capacity *= 2;

        char* new_buf = realloc(log->buffer, new_capacity);
        if (!new_buf) {
            // Can't recover from OOM safely
            pthread_mutex_unlock(&log->mutex);
            return;
        }

        log->buffer = new_buf;
        log->capacity = new_capacity;
    }

    memcpy(log->buffer + log->size, data, data_len);
    log->size += data_len;

    log->writer_active = 0;

    // Notify all: waiting writers (priority), then readers
    if (log->writers_waiting > 0) {
        pthread_cond_signal(&log->can_write);
    } else {
        pthread_cond_broadcast(&log->can_read);
    }

    pthread_mutex_unlock(&log->mutex);
}

// Returns the full log contents (thread-safe with writer priority)
int get_log(server_log log, char** dst) {
    if (!log || !dst) return 0;

    pthread_mutex_lock(&log->mutex);

    // Writer priority: wait if any writer is active or waiting
    while (log->writer_active || log->writers_waiting > 0) {
        pthread_cond_wait(&log->can_read, &log->mutex);
    }
    log->readers++;

    pthread_mutex_unlock(&log->mutex);

    // Copy content outside lock
    *dst = malloc(log->size + 1);
    if (!*dst) {
        pthread_mutex_lock(&log->mutex);
        log->readers--;
        if (log->readers == 0)
            pthread_cond_signal(&log->can_write);
        pthread_mutex_unlock(&log->mutex);
        return 0;
    }

    memcpy(*dst, log->buffer, log->size);
    (*dst)[log->size] = '\0';

    pthread_mutex_lock(&log->mutex);
    log->readers--;

    if (log->readers == 0)
        pthread_cond_signal(&log->can_write);

    pthread_mutex_unlock(&log->mutex);
    return log->size;
}
