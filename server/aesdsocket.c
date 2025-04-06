#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <time.h>
#include <sys/ioctl.h>
#include "aesd_ioctl.h"

#define PORT 9000
#define FILE_PATH "/dev/aesdchar"
#define BUFFER_SIZE 1024

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t exit_signal = 0;

struct client_data {
    int client_socket;
};

void signal_handler(int sig) {
    exit_signal = 1;
}

void *handle_client(void *arg) {
    struct client_data *data = (struct client_data *)arg;
    int client_socket = data->client_socket;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    syslog(LOG_INFO, "Handling new client connection");

    int file_fd = open(FILE_PATH, O_RDWR | O_APPEND);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Error opening file: %s", strerror(errno));
        close(client_socket);
        free(data);
        return NULL;
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';

        // Check for AESDCHAR_IOCSEEKTO command
        if (strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 20) == 0) {
            unsigned int write_cmd = 0, write_cmd_offset = 0;
            sscanf(buffer + 20, "%u,%u", &write_cmd, &write_cmd_offset);

            struct aesd_seekto seekto = {
                .write_cmd = write_cmd,
                .write_cmd_offset = write_cmd_offset
            };

            pthread_mutex_lock(&file_mutex);
            if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
                syslog(LOG_ERR, "ioctl failed: %s", strerror(errno));
            }
            pthread_mutex_unlock(&file_mutex);
        } else {
            pthread_mutex_lock(&file_mutex);
            write(file_fd, buffer, bytes_received);
            pthread_mutex_unlock(&file_mutex);
        }

        if (strchr(buffer, '\n')) break;
    }

    // Send back file content using same file_fd
    pthread_mutex_lock(&file_mutex);
    lseek(file_fd, 0, SEEK_SET);
    while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
        send(client_socket, buffer, bytes_received, 0);
    }
    pthread_mutex_unlock(&file_mutex);

    syslog(LOG_INFO, "Client disconnected");
    close(file_fd);
    close(client_socket);
    free(data);
    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread;

    openlog("aesdsocket", LOG_PID, LOG_USER);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_socket);
        return EXIT_FAILURE;
    }

    if (listen(server_socket, 10) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_socket);
        return EXIT_FAILURE;
    }

    while (!exit_signal) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            if (exit_signal) break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        struct client_data *data = malloc(sizeof(struct client_data));
        data->client_socket = client_socket;
        pthread_create(&thread, NULL, handle_client, data);
        pthread_detach(thread);
    }

    close(server_socket);
    closelog();
    return 0;
}

