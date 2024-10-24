#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#define SHM_NAME_PREFIX "/car"

// Utility function to update the destination floor
void update_floor(car_shared_mem *shared_mem, const char *new_floor) {
    strncpy(shared_mem->destination_floor, new_floor, sizeof(shared_mem->destination_floor) - 1);
    shared_mem->destination_floor[sizeof(shared_mem->destination_floor) - 1] = '\0';
}

// Function to check if the car is between floors
int is_between_floors(car_shared_mem *shared_mem) {
    return strcmp(shared_mem->status, "Between") == 0;
}

// Function to check if the doors are closed
int are_doors_closed(car_shared_mem *shared_mem) {
    return strcmp(shared_mem->status, "Closed") == 0;
}

// Function to check if the doors are open
int are_doors_open(car_shared_mem *shared_mem) {
    return strcmp(shared_mem->status, "Open") == 0 || strcmp(shared_mem->status, "Opening") == 0;
}

// Helper function to get current floor as an integer
int get_floor_value(const char *floor) {
    if (floor[0] == 'B') {
        return -atoi(floor + 1); // Basement floors are negative
    }
    return atoi(floor); // Regular floors are positive
}

// Helper function to set the current floor as a string
void set_floor_string(char *floor_str, int floor_val) {
    if (floor_val < 0) {
        snprintf(floor_str, 4, "B%d", -floor_val);
    } else {
        snprintf(floor_str, 4, "%d", floor_val);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    char *car_name = argv[1];
    char *operation = argv[2];
    char shm_name[32];
    snprintf(shm_name, sizeof(shm_name), "%s%s", SHM_NAME_PREFIX, car_name);

    // Open shared memory segment
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        printf("Unable to access car %s.\n", car_name);
        return 1;
    }

    // Map the shared memory segment
    car_shared_mem *shared_mem = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        close(shm_fd);
        printf("Failed to map shared memory.\n");
        return 1;
    }

    // Lock the shared memory mutex
    if (pthread_mutex_lock(&shared_mem->mutex) != 0) {
        munmap(shared_mem, sizeof(car_shared_mem));
        close(shm_fd);
        printf("Failed to lock mutex.\n");
        return 1;
    }

    // Perform the requested operation
    int success = 0;

    // Reset buttons before operation
    shared_mem->open_button = 0;
    shared_mem->close_button = 0;

    // Handle operations
    if (strcmp(operation, "open") == 0) {
        if (are_doors_closed(shared_mem)) {
            shared_mem->open_button = 1;
            strcpy(shared_mem->status, "Opening");
            success = 1;
        } else {
            printf("Operation not allowed while doors are open.\n");
        }
    } else if (strcmp(operation, "close") == 0) {
        if (are_doors_open(shared_mem)) {
            shared_mem->close_button = 1;
            strcpy(shared_mem->status, "Closed");
            success = 1;
        } else if (are_doors_closed(shared_mem)) {
            printf("Doors are already closed.\n");
        } else {
            printf("Operation not allowed while doors are closed.\n");
        }
    } else if (strcmp(operation, "stop") == 0) {
        shared_mem->emergency_stop = 1;
        shared_mem->emergency_mode = 1;
        success = 1;
    } else if (strcmp(operation, "service_on") == 0) {
        shared_mem->individual_service_mode = 1;
        shared_mem->emergency_mode = 0;
        success = 1;
    } else if (strcmp(operation, "service_off") == 0) {
        shared_mem->individual_service_mode = 0;
        success = 1;
    } else if (strcmp(operation, "up") == 0) {
        if (!shared_mem->individual_service_mode) {
            printf("Operation only allowed in service mode.\n");
        } else if (!are_doors_closed(shared_mem)) {
            printf("Operation not allowed while doors are open.\n");
        } else if (is_between_floors(shared_mem)) {
            printf("Operation not allowed while elevator is moving.\n");
        } else {
            int current_floor = get_floor_value(shared_mem->current_floor);
            if (current_floor < 999) {
                char new_floor[4];
                set_floor_string(new_floor, current_floor + 1);
                update_floor(shared_mem, new_floor);
                success = 1;
            } else {
                printf("Already at the highest floor.\n");
            }
        }
    } else if (strcmp(operation, "down") == 0) {
        if (!shared_mem->individual_service_mode) {
            printf("Operation only allowed in service mode.\n");
        } else if (!are_doors_closed(shared_mem)) {
            printf("Operation not allowed while doors are open.\n");
        } else if (is_between_floors(shared_mem)) {
            printf("Operation not allowed while elevator is moving.\n");
        } else {
            int current_floor = get_floor_value(shared_mem->current_floor);
            if (current_floor > -99) { // Handle basement floors up to B99
                char new_floor[4];
                set_floor_string(new_floor, current_floor - 1);
                update_floor(shared_mem, new_floor);
                success = 1;
            } else {
                printf("Already at the lowest floor.\n");
            }
        }
    } else {
        printf("Invalid operation.\n");
    }

    // Signal condition variable if the operation was successful
    if (success) {
        pthread_cond_broadcast(&shared_mem->cond);
    }

    // Unlock the shared memory mutex
    pthread_mutex_unlock(&shared_mem->mutex);

    // Clean up
    munmap(shared_mem, sizeof(car_shared_mem));
    close(shm_fd);

    return 0;
}
