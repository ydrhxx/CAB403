#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>
#include <pthread.h>

#define SHM_NAME_PREFIX "/car"

// Shared memory structure for car
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
} car_shared_mem;

// Function prototypes
int is_valid_floor(const char *floor);
int is_valid_status(const char *status);
void check_safety_conditions(car_shared_mem *shared_mem);

#endif
