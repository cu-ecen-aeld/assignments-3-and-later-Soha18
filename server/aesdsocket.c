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
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "queue.h" // Include the linked list queue implementation

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_socket = -1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
int terminate_flag = 0; // Global flag for termination

// Structure to hold thread data
struct client_thread {
    pthread_t thread_id;
    int client_socket;
    TAILQ_ENTRY(client_thread) entries;
};

// Define the head for the thread queue
TAILQ_HEAD(client_thread_head, client_thread);
struct client_thread_head thread_list = TAILQ_HEAD_INITIALIZER(thread_list);

// Function to handle SIGINT and SIGTERM
void handle_signal(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    terminate_flag = 1;
    close(server_socket);
    remove(FILE_PATH);
    closelog();
}

// Function to handle a client connection in a new thread
void *client_handler(void *arg) {
    struct client_thread *client = (struct client_thread *)arg;
    char buffer[1024];
    ssize_t bytes_received;

    // Open the file in append mode
    pthread_mutex_lock(&file_mutex);
    int file_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    pthread_mutex_unlock(&file_mutex);
    
    if (file_fd == -1) {
        syslog(LOG_ERR, "Error opening file");
        close(client->client_socket);
        pthread_exit(NULL);
    }

    // Receive data and write to file
    while ((bytes_received = recv(client->client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        pthread_mutex_lock(&file_mutex);
        write(file_fd, buffer, bytes_received);
        pthread_mutex_unlock(&file_mutex);
        if (strchr(buffer, '\n')) break;
    }
    close(file_fd);

    // Read file and send data back to client
    pthread_mutex_lock(&file_mutex);
    file_fd = open(FILE_PATH, O_RDONLY);
    pthread_mutex_unlock(&file_mutex);
    
    if (file_fd != -1) {
        while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
            send(client->client_socket, buffer, bytes_received, 0);
        }
        close(file_fd);
    }
    
    close(client->client_socket);
    pthread_exit(NULL);
}

// Function to log timestamp every 10 seconds
void *timestamp_logger(void *arg) {
    while (!terminate_flag) {
        sleep(10);
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[100];
        strftime(time_str, sizeof(time_str), "timestamp: %a, %d %b %Y %H:%M:%S %z\n", t);
        
        pthread_mutex_lock(&file_mutex);
        int file_fd = open(FILE_PATH, O_WRONLY | O_APPEND);
        if (file_fd != -1) {
            write(file_fd, time_str, strlen(time_str));
            close(file_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Create TCP socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Error binding socket: %s", strerror(errno));
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_socket, 10) == -1) {
        syslog(LOG_ERR, "Error listening on socket: %s", strerror(errno));
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    syslog(LOG_INFO, "Server started on port %d", PORT);
    
    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, timestamp_logger, NULL);
    
    while (!terminate_flag) {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) continue;
        
        struct client_thread *new_client = malloc(sizeof(struct client_thread));
        new_client->client_socket = client_socket;
        
        pthread_create(&new_client->thread_id, NULL, client_handler, new_client);
        
        TAILQ_INSERT_TAIL(&thread_list, new_client, entries);
    }
    
    // Cleanup all threads
    struct client_thread *client;
    while ((client = TAILQ_FIRST(&thread_list))) {
        pthread_join(client->thread_id, NULL);
        TAILQ_REMOVE(&thread_list, client, entries);
        free(client);
    }
    
    pthread_join(timestamp_thread, NULL);
    pthread_mutex_destroy(&file_mutex);
    return 0;
}

