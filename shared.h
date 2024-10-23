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
} car_shared_mem;

// Initialize the shared memory structure
void init_shm(car_shared_mem *s) {
    // Initialize the mutex with PTHREAD_PROCESS_SHARED
    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_setpshared(&mutattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&s->mutex, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    // Initialize the condition variable with PTHREAD_PROCESS_SHARED
    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&s->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    // Reset other values to default
    reset_shm(s);
}

// Reset the shared memory structure to default values
void reset_shm(car_shared_mem *s) {
    pthread_mutex_lock(&s->mutex);

    // Clear all fields except for the mutex and condition variable
    size_t offset = offsetof(car_shared_mem, current_floor);
    memset((char *)s + offset, 0, sizeof(*s) - offset);

    // Set initial values
    strcpy(s->current_floor, "1");
    strcpy(s->destination_floor, "1");
    strcpy(s->status, "Closed");
    s->open_button = 0;
    s->close_button = 0;
    s->door_obstruction = 0;
    s->overload = 0;
    s->emergency_stop = 0;
    s->individual_service_mode = 0;
    s->emergency_mode = 0;

    pthread_mutex_unlock(&s->mutex);
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

// Utility function to print the current shared memory state
void displaycond(car_shared_mem *s) {
    pthread_mutex_lock(&s->mutex);
    printf("Current state: {%s, %s, %s, %d, %d, %d, %d, %d, %d, %d}\n",
        s->current_floor,
        s->destination_floor,
        s->status,
        s->open_button,
        s->close_button,
        s->door_obstruction,
        s->overload,
        s->emergency_stop,
        s->individual_service_mode,
        s->emergency_mode
    );
    pthread_mutex_unlock(&s->mutex);
}

// Utility function for printing debug messages
void msg(const char *string) {
    printf("### %s\n    ", string);
    fflush(stdout);
}

#endif // SHARED_H
