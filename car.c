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
void handle_door_timing(car_shared_mem *shm);
void cleanup_resources();

int shm_fd;
static car_shared_mem *shm;

int main() {
    // Open shared memory
    shm_fd = shm_open("/carTest", O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to open shared memory");
        exit(EXIT_FAILURE);
    }

    // Map the shared memory
    shm = mmap(0, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("Failed to map shared memory");
        exit(EXIT_FAILURE);
    }

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

        // Sleep briefly to prevent busy-waiting (adjust as needed)
        usleep(5 * MILLISECOND);
    }

    // Cleanup resources (on exit or interruption)
    cleanup_resources();
    return 0;
}

void handle_door_timing(car_shared_mem *shm) {
    int open_time = shm->delay * MILLISECOND;  // Calculate open time based on delay

    // Transition to 'Opening' at 0ms
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Opening");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait until fully open at delay ms
    usleep(open_time);
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Open");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait for additional delay ms before starting to close (at 2*delay ms)
    usleep(open_time);
    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Closing");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    // Wait until fully closed at 3*delay ms
    usleep(open_time);
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
