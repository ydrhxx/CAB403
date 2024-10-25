#include "call.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <ctype.h>

#define PORT 3000
#define SERVER_IP "127.0.0.1"

// Helper function to check if the floor is valid
int is_valid_floor(const char *floor) {
    if (floor == NULL) return 0;
    size_t len = strlen(floor);
    if (len < 1 || len > 3) return 0; // Floor names are 1 to 3 characters

    if (floor[0] == 'B') {
        // Basement floors: B1 to B99
        if (len < 2 || len > 3) return 0;
        for (size_t i = 1; i < len; i++) {
            if (!isdigit(floor[i])) return 0;
        }
        int num = atoi(floor + 1);
        if (num < 1 || num > 99) return 0;
    } else {
        // Regular floors: 1 to 999
        for (size_t i = 0; i < len; i++) {
            if (!isdigit(floor[i])) return 0;
        }
        int num = atoi(floor);
        if (num < 1 || num > 999) return 0;
    }
    return 1;
}

// Function to send message to the controller
int send_message(int sock, const char *message) {
    uint32_t msg_len = htonl(strlen(message));

    // Send message length
    if (send(sock, &msg_len, sizeof(msg_len), 0) != sizeof(msg_len)) {
        perror("Failed to send message length");
        return -1;
    }

    // Send message content
    if (send(sock, message, strlen(message), 0) != (ssize_t)strlen(message)) {
        perror("Failed to send message content");
        return -1;
    }

    return 0;
}

// Function to receive response from the controller
int receive_response(int sock, char *response, size_t max_len) {
    uint32_t msg_len;

    // Receive message length
    if (recv(sock, &msg_len, sizeof(msg_len), 0) != sizeof(msg_len)) {
        perror("Failed to receive message length");
        return -1;
    }

    msg_len = ntohl(msg_len);
    if (msg_len >= max_len) {
        fprintf(stderr, "Received message too long.\n");
        return -1;
    }

    // Receive message content
    if (recv(sock, response, msg_len, 0) != (ssize_t)msg_len) {
        perror("Failed to receive message content");
        return -1;
    }

    response[msg_len] = '\0'; // Null-terminate the received message
    return 0;
}

// Function to handle TCP communication and floor request
void connect_to_controller(const char *source_floor, const char *destination_floor) {
    int sock;
    struct sockaddr_in controller_addr;
    char message[256];
    char response[256];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        printf("Unable to connect to elevator system.\n");
        return;
    }

    // Set up controller address
    memset(&controller_addr, 0, sizeof(controller_addr));
    controller_addr.sin_family = AF_INET;
    controller_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &controller_addr.sin_addr) <= 0) {
        perror("Invalid address");
        printf("Unable to connect to elevator system.\n");
        close(sock);
        return;
    }

    // Connect to controller
    if (connect(sock, (struct sockaddr *)&controller_addr, sizeof(controller_addr)) == -1) {
        perror("Connection to controller failed");
        printf("Unable to connect to elevator system.\n");
        close(sock);
        return;
    }

    // Prepare call pad request message
    snprintf(message, sizeof(message), "CALL %s %s", source_floor, destination_floor);

    // Send call pad request
    if (send_message(sock, message) == -1) {
        printf("Unable to connect to elevator system.\n");
        close(sock);
        return;
    }

    // Receive response from the controller
    if (receive_response(sock, response, sizeof(response)) == -1) {
        printf("Unable to connect to elevator system.\n");
        close(sock);
        return;
    }

    // Process the controller's response
    if (strncmp(response, "CAR ", 4) == 0) {
        printf("Car %s is arriving.\n", response + 4); // Extract car name
    } else if (strcmp(response, "UNAVAILABLE") == 0) {
        printf("Sorry, no car is available to take this request.\n");
    } else {
        printf("Received unexpected response: %s\n", response);
    }

    // Clean up
    close(sock);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s {source floor} {destination floor}\n", argv[0]);
        exit(1);
    }

    const char *source_floor = argv[1];
    const char *destination_floor = argv[2];

    // Validate floors
    if (!is_valid_floor(source_floor) || !is_valid_floor(destination_floor)) {
        printf("Invalid floor(s) specified.\n");
        return 1;
    }

    // Check if source and destination floors are different
    if (strcmp(source_floor, destination_floor) == 0) {
        printf("You are already on that floor!\n");
        return 1;
    }

    // Connect to controller and send request
    connect_to_controller(source_floor, destination_floor);

    return 0;
}