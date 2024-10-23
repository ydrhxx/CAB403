#include "safety.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#define SHM_NAME_PREFIX "/car"

// Function to check if floor is valid
int is_valid_floor(const char *floor) {
    if (floor[0] == 'B' && isdigit(floor[1])) {
        return 1;
    } else if (isdigit(floor[0])) {
        return 1;
    }
    return 0;
}

// Function to check if status is valid
int is_valid_status(const char *status) {
    return strcmp(status, "Opening") == 0 || strcmp(status, "Open") == 0 ||
           strcmp(status, "Closing") == 0 || strcmp(status, "Closed") == 0 ||
           strcmp(status, "Between") == 0;
}

// Function to check safety conditions and handle emergencies
void check_safety_conditions(car_shared_mem *shared_mem) {
    if (shared_mem->door_obstruction == 1 && strcmp(shared_mem->status, "Closing") == 0) {
        printf("Obstruction detected while closing. Reopening doors.\n");
        strcpy(shared_mem->status, "Opening");
    }

    if (shared_mem->emergency_stop == 1 && shared_mem->emergency_mode == 0) {
        printf("The emergency stop button has been pressed!\n");
        shared_mem->emergency_mode = 1;
    }

    if (shared_mem->overload == 1 && shared_mem->emergency_mode == 0) {
        printf("The overload sensor has been tripped!\n");
        shared_mem->emergency_mode = 1;
    }

    if (shared_mem->emergency_mode != 1) {
        if (!is_valid_floor(shared_mem->current_floor) || !is_valid_floor(shared_mem->destination_floor) ||
            !is_valid_status(shared_mem->status) ||
            shared_mem->open_button > 1 || shared_mem->close_button > 1 ||
            shared_mem->door_obstruction > 1 || shared_mem->overload > 1 ||
            shared_mem->emergency_stop > 1 || shared_mem->individual_service_mode > 1 ||
            (shared_mem->door_obstruction == 1 && strcmp(shared_mem->status, "Opening") != 0 &&
             strcmp(shared_mem->status, "Closing") != 0)) {
            printf("Data consistency error!\n");
            shared_mem->emergency_mode = 1;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s {car name}\n", argv[0]);
        return 1;
    }

    char *car_name = argv[1];
    char shm_name[32];
    snprintf(shm_name, sizeof(shm_name), "%s%s", SHM_NAME_PREFIX, car_name);

    // Open shared memory segment
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        printf("Unable to access car %s.\n", car_name);
        return 1;
    }

    car_shared_mem *shared_mem = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return 1;
    }

    // Safety system loop
    while (1) {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);

        // Check safety conditions
        check_safety_conditions(shared_mem);

        pthread_mutex_unlock(&shared_mem->mutex);
    }

    // Clean up
    munmap(shared_mem, sizeof(car_shared_mem));
    close(shm_fd);

    return 0;
}