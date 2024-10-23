#include shared.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#define SHM_SIZE sizeof(car_shared_mem)

void init_shared_memory(const char *name, car_shared_mem **shm_ptr) {
    int shm_fd;
    char shm_name[20];

    // Create shared memory name based on car name
    snprintf(shm_name, sizeof(shm_name), "/car%s", name);

    // Create or open the shared memory object
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to open shared memory");
        exit(EXIT_FAILURE);
    }

    // Set the size of the shared memory
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("Failed to set shared memory size");
        exit(EXIT_FAILURE);
    }

    // Map the shared memory object
    *shm_ptr = (car_shared_mem *)mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (*shm_ptr == MAP_FAILED) {
        perror("Failed to map shared memory");
        exit(EXIT_FAILURE);
    }

    // Initialize mutex and condition variable for pshared (inter-process)
    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&(*shm_ptr)->mutex, &mutex_attr);

    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&(*shm_ptr)->cond, &cond_attr);

    // Initialize shared memory fields
    strncpy((*shm_ptr)->current_floor, "1", sizeof((*shm_ptr)->current_floor) - 1);
    strncpy((*shm_ptr)->destination_floor, "", sizeof((*shm_ptr)->destination_floor) - 1);
    strncpy((*shm_ptr)->status, "Closed", sizeof((*shm_ptr)->status) - 1);
    (*shm_ptr)->open_button = 0;
    (*shm_ptr)->close_button = 0;
    (*shm_ptr)->door_obstruction = 0;
    (*shm_ptr)->overload = 0;
    (*shm_ptr)->emergency_stop = 0;
    (*shm_ptr)->individual_service_mode = 0;
    (*shm_ptr)->emergency_mode = 0;
}


