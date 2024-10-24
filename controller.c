#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 3000
#define MAX_BUFFER 256
#define MAX_CARS 10

typedef struct {
    int sockfd;
    char name[32];
    char current_floor[4];
    char highest_floor[4];
    char status[16];
} Car;

Car cars[MAX_CARS];
int car_count = 0;
pthread_mutex_t car_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function prototypes
void recv_looped(int fd, void *buf, size_t sz);
char *receive_msg(int fd);
void send_length_prefixed(int fd, const char *msg);
void *handle_client(void *arg);
void handle_car_registration(int fd, char *msg);
void handle_car_status(int fd, char *msg);
void handle_call_pad(int fd, char *msg);

int main() {
    int listensockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listensockfd == -1) {
        perror("socket()");
        exit(1);
    }

    // Enable address reuse
    int opt_enable = 1;
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1) {
        perror("setsockopt()");
        exit(1);
    }

    // Set up address structure
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket
    if (bind(listensockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind()");
        exit(1);
    }

    // Start listening for connections
    if (listen(listensockfd, 10) == -1) {
        perror("listen()");
        exit(1);
    }

    printf("Controller listening on port %d...\n", PORT);

    // Accept incoming connections in a loop
    for (;;) {
        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        int clientfd = accept(listensockfd, (struct sockaddr *)&clientaddr, &clientaddr_len);
        if (clientfd == -1) {
            perror("accept()");
            continue;
        }

        // Create a thread to handle the client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)(intptr_t)clientfd) != 0) {
            perror("pthread_create()");
            close(clientfd);
        }
    }

    close(listensockfd);
    return 0;
}

// Read a fixed number of bytes from a socket
void recv_looped(int fd, void *buf, size_t sz) {
    char *ptr = buf;
    size_t remain = sz;
    while (remain > 0) {
        ssize_t received = read(fd, ptr, remain);
        if (received <= 0) {
            perror("read()");
            exit(1);
        }
        ptr += received;
        remain -= received;
    }
}

// Receive a length-prefixed message from the client
char *receive_msg(int fd) {
    uint32_t nlen;
    recv_looped(fd, &nlen, sizeof(nlen));
    uint32_t len = ntohl(nlen);
    char *buf = malloc(len + 1);
    if (!buf) {
        perror("malloc()");
        exit(1);
    }
    buf[len] = '\0';
    recv_looped(fd, buf, len);
    return buf;
}

// Send a length-prefixed message to the client
void send_length_prefixed(int fd, const char *msg) {
    uint32_t len = strlen(msg);
    uint32_t nlen = htonl(len);
    write(fd, &nlen, sizeof(nlen));
    write(fd, msg, len);
}

// Handle client connection
void *handle_client(void *arg) {
    int clientfd = (intptr_t)arg;
    char *msg = receive_msg(clientfd);

    // Check if the message is from a car or a call pad
    if (strncmp(msg, "CAR", 3) == 0) {
        handle_car_registration(clientfd, msg);
    } else if (strncmp(msg, "CALL", 4) == 0) {
        handle_call_pad(clientfd, msg);
    } else {
        printf("Received unknown message: %s\n", msg);
    }

    free(msg);
    close(clientfd);
    return NULL;
}

// Handle car registration
void handle_car_registration(int fd, char *msg) {
    char name[32], floor1[4], floor2[4];
    sscanf(msg, "CAR %s %s %s", name, floor1, floor2);

    pthread_mutex_lock(&car_mutex);
    if (car_count < MAX_CARS) {
        Car *new_car = &cars[car_count++];
        new_car->sockfd = fd;
        strncpy(new_car->name, name, sizeof(new_car->name) - 1);
        strncpy(new_car->current_floor, floor1, sizeof(new_car->current_floor) - 1);
        strncpy(new_car->highest_floor, floor2, sizeof(new_car->highest_floor) - 1);
        strncpy(new_car->status, "Closed", sizeof(new_car->status) - 1);
        printf("Registered car: %s (Floors: %s to %s)\n", name, floor1, floor2);
    } else {
        printf("Max car limit reached, cannot register: %s\n", name);
    }
    pthread_mutex_unlock(&car_mutex);

    // Send a status message after registration
    send_length_prefixed(fd, "STATUS Registered");
}

// Handle call pad messages
void handle_call_pad(int fd, char *msg) {
    char floor_from[4], floor_to[4];
    sscanf(msg, "CALL %s %s", floor_from, floor_to);

    pthread_mutex_lock(&car_mutex);
    int dispatched = 0;
    for (int i = 0; i < car_count; i++) {
        if (atoi(floor_from) >= atoi(cars[i].current_floor) && atoi(floor_from) <= atoi(cars[i].highest_floor)) {
            char response[MAX_BUFFER];
            snprintf(response, sizeof(response), "CAR %s", cars[i].name);
            send_length_prefixed(fd, response);

            // Send the floor request to the car
            snprintf(response, sizeof(response), "FLOOR %s", floor_from);
            send_length_prefixed(cars[i].sockfd, response);
            dispatched = 1;
            break;
        }
    }

    if (!dispatched) {
        send_length_prefixed(fd, "UNAVAILABLE");
    }

    pthread_mutex_unlock(&car_mutex);
}
