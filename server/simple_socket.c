#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

volatile sig_atomic_t keep_running = 1;
char *data_file = "/tmp/socket_data";

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Caught signal, exiting\n");
        keep_running = 0;
    }
}

int main(void) {
    int sockfd, clientfd;
    struct addrinfo hints, *servinfo;
    char buffer[BUFFER_SIZE];
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Set up socket
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    int status;
    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    
    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd < 0) {
        perror("Error opening socket");
        freeaddrinfo(servinfo);
        exit(1);
    }
    
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        perror("setsockopt");
        freeaddrinfo(servinfo);
        exit(1);
    }
    
    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("Error binding to socket");
        freeaddrinfo(servinfo);
        exit(1);
    }
    
    if (listen(sockfd, 1) == -1) {
        perror("listen failed");
        exit(1);
    }
    
    freeaddrinfo(servinfo);
    printf("Server listening on port 9000\n");
    
    while (keep_running) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof their_addr;
        
        clientfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (clientfd < 0) {
            perror("Error accepting connection");
            continue;
        }
        
        // Read data from client
        ssize_t bytes_read = read(clientfd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            
            // Append data to file with a newline
            int fd = open(data_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                // Check if the string already ends with a newline
                if (buffer[bytes_read - 1] != '\n') {
                    // Add a newline if it doesn't have one
                    buffer[bytes_read] = '\n';
                    bytes_read++;
                }
                
                write(fd, buffer, bytes_read);
                close(fd);
                
                // Read entire file and send back to client
                fd = open(data_file, O_RDONLY);
                if (fd >= 0) {
                    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
                        if (send(clientfd, buffer, bytes_read, 0) < 0) {
                            perror("Error sending data to client");
                            break;
                        }
                    }
                    close(fd);
                }
            }
        }
        
        close(clientfd);
    }
    
    close(sockfd);
    unlink(data_file);  // Clean up data file
    return 0;
} 