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

#define PORT 9000
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_socket = -1;

void handle_signal(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (server_socket != -1) close(server_socket);
    remove(FILE_PATH);
    closelog();
    exit(0);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    // Check for "-d" argument
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (daemon_mode) {
        if (daemon(0, 0) < 0) {
            syslog(LOG_ERR, "Failed to daemonize: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    // Create TCP socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
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

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1) continue;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        int file_fd = open(FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Error opening file");
            close(client_socket);
            continue;
        }

        char buffer[1024];
        ssize_t bytes_received;
        while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[bytes_received] = '\0';
            write(file_fd, buffer, bytes_received);
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

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_socket);
    }

    return 0;
}

