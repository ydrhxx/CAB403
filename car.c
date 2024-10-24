#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define MILLISECOND 1000 // 1ms

// Define the shared memory structure
typedef struct {
    pthread_mutex_t mutex;           // Locked while the contents are being accessed/modified
    pthread_cond_t cond;             // Signaled when the contents of the structure change
    char current_floor[4];           // C string in the range "B99" to "B1" and "1" to "999"
    char destination_floor[4];       // Same format as above
    char status[8];                  // C string indicating the elevator's status
    uint8_t open_button;             // 1 if open doors button is pressed, else 0
    uint8_t close_button;            // 1 if close doors button is pressed, else 0
    uint8_t door_obstruction;        // 1 if obstruction detected, else 0
    uint8_t overload;                // 1 if overload detected, else 0
    uint8_t emergency_stop;          // 1 if emergency stop button pressed, else 0
    uint8_t individual_service_mode; // 1 if in individual service mode, else 0
    uint8_t emergency_mode;          // 1 if in emergency mode, else 0
    int delay;                       // Delay for door operation in ms
} car_shared_mem;

// Function prototypes
void init_shared_memory(car_shared_mem *shm, const char *lowest_floor, int delay);
void handle_door_timing(car_shared_mem *shm);
void cleanup_resources();
void handle_closing_timing(car_shared_mem *shm);

int shm_fd;
static car_shared_mem *shm;

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s {name} {lowest floor} {highest floor} {delay}\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *name = argv[1];
    const char *lowest_floor = argv[2];
    const char *highest_floor = argv[3];
    int delay = atoi(argv[4]);

    // Construct shared memory name
    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/car%s", name);

    // Create shared memory
    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to create/open shared memory");
        exit(EXIT_FAILURE);
    }

    // Set the size of shared memory
    if (ftruncate(shm_fd, sizeof(car_shared_mem)) == -1) {
        perror("Failed to set shared memory size");
        close(shm_fd);
        shm_unlink(shm_name);
        exit(EXIT_FAILURE);
    }

    // Map the shared memory
    shm = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("Failed to map shared memory");
        close(shm_fd);
        shm_unlink(shm_name);
        exit(EXIT_FAILURE);
    }

    // Initialize shared memory
    init_shared_memory(shm, lowest_floor, delay);

    // Main loop: continuously check and respond to shared memory changes
    while (1) {
        pthread_mutex_lock(&shm->mutex);

        // If open button is pressed, handle the door timing logic
        if (shm->open_button == 1) {
            // Reset the open button (handled)
            shm->open_button = 0;
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);

            // Execute the door timing logic
            handle_door_timing(shm);
            continue;  // Check for next conditions in shared memory
        }
        pthread_mutex_unlock(&shm->mutex);

        // Sleep briefly to prevent busy-waiting
        usleep(5 * MILLISECOND);
    }

    // Cleanup resources (on exit or interruption)
    cleanup_resources();
    return 0;
}

// Initialize the shared memory structure
void init_shared_memory(car_shared_mem *shm, const char *lowest_floor, int delay) {
    // Initialize the mutex with PTHREAD_PROCESS_SHARED
    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    // Initialize the condition variable with PTHREAD_PROCESS_SHARED
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shm->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    // Set initial values
    strcpy(shm->current_floor, lowest_floor);
    strcpy(shm->destination_floor, lowest_floor);
    strcpy(shm->status, "Closed");
    shm->open_button = 0;
    shm->close_button = 0;
    shm->door_obstruction = 0;
    shm->overload = 0;
    shm->emergency_stop = 0;
    shm->individual_service_mode = 0;
    shm->emergency_mode = 0;
    shm->delay = delay;
}

void handle_door_timing(car_shared_mem *shm) {
    int open_time = shm->delay * MILLISECOND;  // Calculate open time based on delay

    // Transition to 'Opening' at 0ms
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Opening");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait until fully open at delay ms, checking for close button press
    for (int elapsed = 0; elapsed < open_time; elapsed += 5 * MILLISECOND) {
        usleep(5 * MILLISECOND);  // Sleep in short intervals

        pthread_mutex_lock(&shm->mutex);
        if (shm->close_button == 1) {
            // Reset the close button and transition to closing
            shm->close_button = 0;
            strcpy(shm->status, "Closing");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);

            // Call the closing logic immediately
            handle_closing_timing(shm);
            return;  // Exit the function after handling closing
        }
        pthread_mutex_unlock(&shm->mutex);
    }

    // If no close button press, continue to 'Open'
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Open");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait for additional delay ms, checking for close button press
    for (int elapsed = 0; elapsed < open_time; elapsed += 5 * MILLISECOND) {
        usleep(5 * MILLISECOND);  // Sleep in short intervals

        pthread_mutex_lock(&shm->mutex);
        if (shm->close_button == 1) {
            // Reset the close button and transition to closing
            shm->close_button = 0;
            strcpy(shm->status, "Closing");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);

            // Call the closing logic immediately
            handle_closing_timing(shm);
            return;  // Exit the function after handling closing
        }
        pthread_mutex_unlock(&shm->mutex);
    }

    // Continue to 'Closing'
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Closing");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait until fully closed at 3*delay ms, checking for close button press
    for (int elapsed = 0; elapsed < open_time; elapsed += 5 * MILLISECOND) {
        usleep(5 * MILLISECOND);  // Sleep in short intervals

        pthread_mutex_lock(&shm->mutex);
        if (shm->close_button == 1) {
            // Reset the close button and transition to closing
            shm->close_button = 0;
            strcpy(shm->status, "Closing");
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);

            // Call the closing logic immediately
            handle_closing_timing(shm);
            return;  // Exit the function after handling closing
        }
        pthread_mutex_unlock(&shm->mutex);
    }

    // Set status to 'Closed'
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Closed");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);
}


void handle_closing_timing(car_shared_mem *shm) {
    int close_time = shm->delay * MILLISECOND;  // Calculate close time based on delay

    // Transition to 'Closing' at 0ms
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Closing");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait until fully closed at delay ms
    usleep(close_time);
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Closed");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);
}


void cleanup_resources() {
    if (shm != MAP_FAILED) {
        munmap(shm, sizeof(car_shared_mem));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
}