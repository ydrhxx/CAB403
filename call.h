#ifndef CALL_H
#define CALL_H

#include <stdint.h>
#include <pthread.h>

#define SHM_NAME_PREFIX "/call"

// Shared memory structure for call pad
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char floor[4];
    char destination[4];
    uint8_t call_button;
} call_shared_mem;

// Function prototypes
void initialize_shared_memory(char *call_name, call_shared_mem **shared_mem, int *shm_fd);
void handle_call_operation(call_shared_mem *shared_mem, const char *operation);
void cleanup_shared_memory(call_shared_mem *shared_mem, int shm_fd, const char *shm_name);

#endif
