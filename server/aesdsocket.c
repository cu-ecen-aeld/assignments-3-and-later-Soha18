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
#include <sys/ioctl.h>
#include <pthread.h>
#include "queue.h"
#include "aesd_ioctl.h"

#define PORT 9000
#define USE_AESD_CHAR_DEVICE 1
#ifdef USE_AESD_CHAR_DEVICE
const char *filename = "/dev/aesdchar";
#else
const char *filename = "/var/tmp/aesdsocketdata";
#endif

int server_socket = -1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

struct client_data {
    int client_socket;
    TAILQ_ENTRY(client_data) entries;
};

TAILQ_HEAD(client_list, client_data);
struct client_list clients;
int exit_flag = 0;

void *handle_client(void *arg) {
    struct client_data *data = (struct client_data *)arg;
    int client_socket = data->client_socket;
    char buffer[1024];
    ssize_t bytes_received;

    syslog(LOG_INFO, "Handling new client connection");
    int file_fd = open(filename, O_RDWR, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Error opening file");
        close(client_socket);
        free(data);
        return NULL;
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        
        // Check for special seek command
        if (strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 18) == 0) {
            unsigned int write_cmd, write_cmd_offset;
            if (sscanf(buffer + 18, "%u,%u", &write_cmd, &write_cmd_offset) == 2) {
                struct aesd_seekto seekto = {
                    .write_cmd = write_cmd,
                    .write_cmd_offset = write_cmd_offset
                };
                
                if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
                    syslog(LOG_ERR, "Error performing seek operation");
                }
            }
            // For seek commands, we don't check for newline as we don't want to write them
            continue;
        }
        
        // Normal write operation
        pthread_mutex_lock(&file_mutex);
        write(file_fd, buffer, bytes_received);
        pthread_mutex_unlock(&file_mutex);
        
        if (strchr(buffer, '\n')) break;
    }

    // Read and send back the content using the same file descriptor
    lseek(file_fd, 0, SEEK_SET);  // Reset to beginning
    while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
        send(client_socket, buffer, bytes_received, 0);
    }

    syslog(LOG_INFO, "Client disconnected");
    close(file_fd);
    close(client_socket);
    free(data);
    return NULL;
}

void handle_signal(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
    
//#ifndef USE_AESD_CHAR_DEVICE
    remove(filename);  // Only remove file if using /var/tmp/aesdsocketdata
//#endif
    
    close(server_socket);
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
