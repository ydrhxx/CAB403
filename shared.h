#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

// Define the shared memory structure for the car component
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
    int delay;
    char highest_floor[4];
} car_shared_mem;

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

// Utility function to send a message over a socket
void send_message(int fd, const char *buf) {
    uint32_t len = htonl(strlen(buf));
    send_looped(fd, &len, sizeof(len));
    send_looped(fd, buf, strlen(buf));
}

// Helper function for sending data over a socket
void send_looped(int fd, const void *buf, size_t sz) {
    const char *ptr = buf;
    size_t remain = sz;

    while (remain > 0) {
        ssize_t sent = write(fd, ptr, remain);
        if (sent == -1) {
            perror("write()");
            exit(1);
        }
        ptr += sent;
        remain -= sent;
    }
}

// Utility function to receive a message over a socket
char *receive_msg(int fd) {
    uint32_t nlen;
    recv_looped(fd, &nlen, sizeof(nlen));
    uint32_t len = ntohl(nlen);

    char *buf = malloc(len + 1);
    buf[len] = '\0';
    recv_looped(fd, buf, len);
    return buf;
}

// Helper function for receiving data over a socket
void recv_looped(int fd, void *buf, size_t sz) {
    char *ptr = buf;
    size_t remain = sz;

    while (remain > 0) {
        ssize_t received = read(fd, ptr, remain);
        if (received == -1) {
            perror("read()");
            exit(1);
        }
        ptr += received;
        remain -= received;
    }
}

#endif // SHARED_H