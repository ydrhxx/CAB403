
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

#define MILLISECOND 1000 // 1ms
#define CONTROLLER_IP "127.0.0.1"
#define CONTROLLER_PORT 3000

// Define the shared memory structure
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char current_floor[4];
    char destination_floor[4];
    char status[8];
    uint8_t open_button;
    uint8_t close_button;
    uint8_t door_obstruction;
    uint8_t overload;
    uint8_t emergency_stop;
    uint8_t individual_service_mode;
    uint8_t emergency_mode;
    int delay;
    char highest_floor[4];
} car_shared_mem;



// Function prototypes
void init_shared_memory(car_shared_mem *shm, const char *lowest_floor, const char *highest_floor, int delay);
void handle_door_timing(car_shared_mem *shm);
void handle_individual_service_mode(car_shared_mem *shm);
void handle_closing_timing(car_shared_mem *shm);
void move_one_floor(car_shared_mem *shm);
void cleanup_resources();
void signal_handler(int sig);

// Global variables
int shm_fd;
static car_shared_mem *shm;
char shm_name[64]; // Global shared memory name

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
    snprintf(shm_name, sizeof(shm_name), "/car%s", name);

    // Set up signal handler for SIGINT
    signal(SIGINT, signal_handler);

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

    init_shared_memory(shm, lowest_floor, highest_floor, delay);

    // Ignore SIGPIPE to prevent crashes on write failures
    signal(SIGPIPE, SIG_IGN);

    // Main loop: continuously check and respond to shared memory changes
    while (1) {
        pthread_mutex_lock(&shm->mutex);

        // Normal operations handling
        if (shm->open_button == 1) {
            // Reset the open button (handled)
            shm->open_button = 0;
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            // Execute the door timing logic
            handle_door_timing(shm);
            continue;// Go to the next iteration of the loop
        }

        // Handle individual service mode
        if (shm->individual_service_mode == 1) {
            //Turn om individual service mode
            pthread_mutex_unlock(&shm->mutex);
            handle_individual_service_mode(shm);
            continue;
        }

        
        pthread_mutex_unlock(&shm->mutex);
        usleep(5 * MILLISECOND);// Sleep briefly to prevent busy-waiting
    }


    // Cleanup resources (on exit or interruption)
    cleanup_resources();
    return 0;
}


// Initialize the shared memory structure
void init_shared_memory(car_shared_mem *shm, const char *lowest_floor, const char *highest_floor, int delay) {
    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shm->mutex, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shm->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    strcpy(shm->current_floor, lowest_floor);
    strcpy(shm->destination_floor, lowest_floor);
    strcpy(shm->highest_floor, highest_floor);
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

void handle_individual_service_mode(car_shared_mem *shm) {
    while (1) {
        pthread_mutex_lock(&shm->mutex);

        // Check if the door is closed and can move
        if (strcmp(shm->status, "Closed") == 0) {
            // Moving up or down manually
            int dest_floor = atoi(shm->destination_floor);
            int current_floor = atoi(shm->current_floor);
            int highest_floor = atoi(shm->highest_floor);

            // Check if destination exceeds the highest floor
            if (dest_floor > highest_floor) {
                strcpy(shm->destination_floor, shm->current_floor);
            }
            // Check if destination is valid and lower than the current floor
            else if (dest_floor < current_floor) {
                pthread_mutex_unlock(&shm->mutex);
                move_one_floor(shm);
                return;
            }
            // Check if destination is valid and higher than the current floor
            else if (dest_floor > current_floor) {
                pthread_mutex_unlock(&shm->mutex);
                move_one_floor(shm);
                return;
            }
        }

        // Open the door if the open button is pressed
        if (shm->open_button == 1) {
            shm->open_button = 0;
            pthread_cond_broadcast(&shm->cond);
            pthread_mutex_unlock(&shm->mutex);
            handle_door_timing(shm);
            continue;
        }

        pthread_mutex_unlock(&shm->mutex);
        usleep(5 * MILLISECOND);
    }
}

void move_one_floor(car_shared_mem *shm) {
    int delay_time = shm->delay * MILLISECOND;

    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->status, "Between");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);

    usleep(delay_time);

    pthread_mutex_lock(&shm->mutex);
    strcpy(shm->current_floor, shm->destination_floor);
    strcpy(shm->status, "Closed");
    pthread_cond_broadcast(&shm->cond);
    pthread_mutex_unlock(&shm->mutex);
}

// Signal handler for SIGINT
void signal_handler(int sig) {
    if (sig == SIGINT) {
        cleanup_resources();
        exit(0);
    }
}

// Cleanup resources and unlink shared memory
void cleanup_resources() {
    if (shm != MAP_FAILED) {
        munmap(shm, sizeof(car_shared_mem));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
    shm_unlink(shm_name);
}