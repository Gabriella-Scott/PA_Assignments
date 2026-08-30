/********************************************************************
 *
 * This contains the code for the communication between the C client and
 * the IF wrapper.
 *
 */

#include "comms.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int socket_desc;

int initialise_comms(unsigned long int ip, int port) {
    struct sockaddr_in server;

    // Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Could not create socket\n");
        return 0;
    }

    // Prepare sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = ip;
    server.sin_port = htons(port);

    // Connect to remote server using the socket
    if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect failed. Error");
        return 0;
    }

    return 1;
}

/**
 * Upon a match reset, the referee sends "MatchResetMessage" immediately
 * followed with "GenActionMessage". On occasion, this is read with one `recv`
 * call, storing "MatchResetMessage\nGenActionMessage" in one buffer.
 *
 * `leftover_buffer` is specifically for this case.
 **/

static char leftover_buffer[BUFFER_SIZE] = {0};
static int leftover_size = 0;

static int process_message(const char *message, int *move) {
    int result = UNKNOWN;

    char temp_buffer[BUFFER_SIZE];
    strncpy(temp_buffer, message, BUFFER_SIZE - 1);
    temp_buffer[BUFFER_SIZE - 1] = '\0';

    char *command = strtok(temp_buffer, " \n");
    char *param = strtok(NULL, " \n");

    if (command != NULL) {
        if (strcmp(command, "GameTerminatedMessage") == 0) {
            result = GAME_TERMINATION;
        } else if (strcmp(command, "GenActionMessage") == 0) {
            result = GENERATE_MOVE;
        } else if (strcmp(command, "PlayedMoveMessage") == 0 && param != NULL) {
            sscanf(param, "%d", move);
            result = PLAY_MOVE;
        } else if (strcmp(command, "MatchResetMessage") == 0) {
            result = MATCH_RESET;
        }
    }

    return result;
}

int receive_message(int *move) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    if (leftover_size > 0) {
        int result = process_message(leftover_buffer, move);

        memset(leftover_buffer, 0, BUFFER_SIZE);
        leftover_size = 0;

        return result;
    }

    memset(buffer, 0, sizeof(buffer));

    bytes_read = recv(socket_desc, buffer, sizeof(buffer), 0);

    if (bytes_read < 0) {
        perror("recv failed");
        return RECV_FAILED;
    } else if (bytes_read == 0) {
        printf("Client disconnected\n");
        return CLIENT_DISCONNECTED;
    }

    char *newline_pos = strchr(buffer, '\n');
    if (newline_pos != NULL && (newline_pos - buffer + 1) < bytes_read) {
        // Found newline before the end of buffer; We have multiple messages.

        // Store everything after the first newline for the next call
        leftover_size = bytes_read - (newline_pos - buffer + 1);
        memcpy(leftover_buffer, newline_pos + 1, leftover_size);
        leftover_buffer[leftover_size] = '\0';

        // Null-terminate the first message
        *(newline_pos + 1) = '\0';
    }

    // Process the first message
    return process_message(buffer, move);
}

int send_move(char *move) {
    if (send(socket_desc, move, strlen(move), 0) < 0) {
        printf("Send failed\n");
        return -1;
    }

    return 0;
}

void close_comms(void) { close(socket_desc); }
