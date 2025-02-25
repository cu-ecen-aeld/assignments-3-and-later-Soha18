#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include "queue.h"  // Include the queue header file

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_socket = -1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure for thread arguments
struct client_data {
    int client_socket;
    TAILQ_ENTRY(client_data) entries;
};

TAILQ_HEAD(client_list, client_data);
struct client_list clients;
pthread_t timer_thread;
int exit_flag = 0;

void *handle_client(void *arg) {
    struct client_data *data = (struct client_data *)arg;
    int client_socket = data->client_socket;
    char buffer[1024];
    ssize_t bytes_received;

    syslog(LOG_INFO, "Handling new client connection");
    int file_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Error opening file");
        close(client_socket);
        free(data);
        return NULL;
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        pthread_mutex_lock(&file_mutex);
        write(file_fd, buffer, bytes_received);
        pthread_mutex_unlock(&file_mutex);
        if (strchr(buffer, '\n')) break;
    }
    close(file_fd);

    file_fd = open(FILE_PATH, O_RDONLY);
    if (file_fd != -1) {
        while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
            send(client_socket, buffer, bytes_received, 0);
        }
        close(file_fd);
    }

    syslog(LOG_INFO, "Client disconnected");
    close(client_socket);
    free(data);
    return NULL;
}

void *timestamp_logger(void *arg) {
    while (!exit_flag) {
        sleep(10);
        time_t now = time(NULL);
        struct tm *time_info = localtime(&now);
        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp: %Y-%m-%d %H:%M:%S\n", time_info);

        pthread_mutex_lock(&file_mutex);
        int file_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file_fd != -1) {
            write(file_fd, timestamp, strlen(timestamp));
            close(file_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

void handle_signal(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
    pthread_cancel(timer_thread);
    pthread_join(timer_thread, NULL);
    close(server_socket);
    remove(FILE_PATH);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (daemon_mode && daemon(0, 0) < 0) {
        syslog(LOG_ERR, "Failed to daemonize");
        exit(EXIT_FAILURE);
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Error creating socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in server_addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(PORT) };

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Error binding socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) == -1) {
        syslog(LOG_ERR, "Error listening on socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Server started on port %d", PORT);
    pthread_create(&timer_thread, NULL, timestamp_logger, NULL);

    TAILQ_INIT(&clients);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) continue;

        struct client_data *data = malloc(sizeof(struct client_data));
        data->client_socket = client_socket;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, data);
        pthread_detach(thread);
    }
    return 0;
}
