
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
#include <signal.h>

#define PORT 3000
#define MAX_CARS 10
#define MAX_QUEUE 20
#define BUFFER_SIZE 256


typedef enum { UP, DOWN, NONE } Direction;

typedef struct {
    int sockfd;
    char name[BUFFER_SIZE];
    char lowest_floor[4];
    char highest_floor[4];
    char current_floor[4];
    char destination_floor[4];
    char status[BUFFER_SIZE];
    int active;
    int queue[MAX_QUEUE];
    Direction directions[MAX_QUEUE];
    int queue_size;
} Car;

Car cars[MAX_CARS];
int num_cars = 0;
pthread_mutex_t cars_mutex = PTHREAD_MUTEX_INITIALIZER;

// Function to send length-prefixed messages
void send_message(int sockfd, const char *msg) {
    uint32_t len = htonl(strlen(msg));
    if (write(sockfd, &len, sizeof(len)) == -1 || write(sockfd, msg, strlen(msg)) == -1) {
        perror("write()");
    }
}

// Function to receive length-prefixed messages
char *receive_msg(int sockfd) {
    uint32_t nlen;
    if (read(sockfd, &nlen, sizeof(nlen)) <= 0) return NULL;
    uint32_t len = ntohl(nlen);
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    if (read(sockfd, buf, len) <= 0) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

// Helper to check if a floor is in the queue
int floor_exists(Car *car, int floor) {
    for (int i = 0; i < car->queue_size; i++) {
        if (car->queue[i] == floor) return 1;
    }
    return 0;
}

// Add floors to a car's queue, maintaining the order and direction
void insert_floors(Car *car, int from_floor, int to_floor, Direction direction) {
    // Check if the from_floor or to_floor is already in the queue
    if (floor_exists(car, from_floor) || floor_exists(car, to_floor)) return;

    // If queue is empty, add the from and to floors
    if (car->queue_size == 0) {
        car->queue[0] = from_floor;
        car->directions[0] = direction;
        car->queue[1] = to_floor;
        car->directions[1] = direction;
        car->queue_size = 2;
        return;
    }

    // Insert from_floor and to_floor in the correct block
    car->queue[car->queue_size] = from_floor;
    car->directions[car->queue_size] = direction;
    car->queue[car->queue_size + 1] = to_floor;
    car->directions[car->queue_size + 1] = direction;
    car->queue_size += 2;
}

// Register a new car
void register_car(int sockfd, char *msg) {
    char name[BUFFER_SIZE], lowest[4], highest[4];
    sscanf(msg, "CAR %s %s %s", name, lowest, highest);

    pthread_mutex_lock(&cars_mutex);
    if (num_cars < MAX_CARS) {
        Car *car = &cars[num_cars++];
        car->sockfd = sockfd;
        strcpy(car->name, name);
        strcpy(car->lowest_floor, lowest);
        strcpy(car->highest_floor, highest);
        strcpy(car->status, "Closed");
        car->active = 1;
        car->queue_size = 0;

        printf("Car registered: %s (Floors: %s to %s)\n", name, lowest, highest);
        send_message(sockfd, "STATUS Registered");
    } else {
        printf("Max number of cars reached!\n");
    }
    pthread_mutex_unlock(&cars_mutex);
}

// Handle call pad requests
void handle_call(int clientfd, char *msg) {
    char from[4], to[4];
    sscanf(msg, "CALL %s %s", from, to);
    int from_floor = atoi(from);
    int to_floor = atoi(to);

    pthread_mutex_lock(&cars_mutex);

    int found = 0;
    for (int i = 0; i < num_cars; i++) {
        if (atoi(cars[i].lowest_floor) <= from_floor && atoi(cars[i].highest_floor) >= to_floor && cars[i].active) {
            Direction direction = (from_floor < to_floor) ? UP : DOWN;

            // Respond with the car's name
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "CAR %s", cars[i].name);
            send_message(clientfd, response);

            // Insert the from_floor and to_floor into the queue
            insert_floors(&cars[i], from_floor, to_floor, direction);

            // If the car is not moving, send the next floor in the queue
            if (strcmp(cars[i].status, "Closed") == 0) {
                char floor_msg[BUFFER_SIZE];
                snprintf(floor_msg, sizeof(floor_msg), "FLOOR %d", cars[i].queue[0]);
                send_message(cars[i].sockfd, floor_msg);
            }

            found = 1;
            break;
        }
    }

    if (!found) {
        send_message(clientfd, "UNAVAILABLE");
    }

    pthread_mutex_unlock(&cars_mutex);
}

// Update car status and send the next floor if needed
void update_car_status(int car_index, char *msg) {
    Car *car = &cars[car_index];
    sscanf(msg, "STATUS %s %s %s", car->status, car->current_floor, car->destination_floor);

    if (strcmp(car->status, "Closed") == 0 && car->queue_size > 0) {
        // Send the next floor in the queue
        char floor_msg[BUFFER_SIZE];
        snprintf(floor_msg, sizeof(floor_msg), "FLOOR %d", car->queue[0]);
        send_message(car->sockfd, floor_msg);

        // Shift the queue
        for (int i = 1; i < car->queue_size; i++) {
            car->queue[i - 1] = car->queue[i];
        }
        car->queue_size--;
    }
}

// Handle incoming client connections
void *client_handler(void *arg) {
    int clientfd = *(int *)arg;
    free(arg);

    char *msg;
    while ((msg = receive_msg(clientfd)) != NULL) {
        if (strncmp(msg, "CAR", 3) == 0) {
            register_car(clientfd, msg);
        } else if (strncmp(msg, "CALL", 4) == 0) {
            handle_call(clientfd, msg);
        } else if (strncmp(msg, "STATUS", 6) == 0) {
            pthread_mutex_lock(&cars_mutex);
            for (int i = 0; i < num_cars; i++) {
                if (cars[i].sockfd == clientfd) {
                    update_car_status(i, msg);
                    break;
                }
            }
            pthread_mutex_unlock(&cars_mutex);
        }
        free(msg);
    }

    close(clientfd);
    return NULL;
}

// Main server function
int main() {
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE to prevent crashes
    int serverfd, *clientfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd == -1) {
        perror("socket()");
        exit(1);
    }

    int opt = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind()");
        exit(1);
    }

    if (listen(serverfd, 10) == -1) {
        perror("listen()");
        exit(1);
    }

    printf("Controller running on port %d\n", PORT);

    while (1) {
        clientfd = malloc(sizeof(int));
        *clientfd = accept(serverfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*clientfd == -1) {
            perror("accept()");
            free(clientfd);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, clientfd);
        pthread_detach(tid);
    }

    close(serverfd);
    return 0;
}