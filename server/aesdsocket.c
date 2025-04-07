/*******************************************************************************
 * Copyright (C) 2025 by Abhirath Koushik
 *
 * Redistribution, modification or use of this software in source or binary
 * forms is permitted as long as the files maintain this copyright. Users are
 * permitted to modify this and use it to learn about the field of embedded
 * software. Abhirath Koushik and the University of Colorado are not liable for
 * any misuse of this material.
 * ****************************************************************************/

/**
 * @file    aesdsocket.c
 * @brief   This file is used to create a socket program to interact between the server and client.
 *
 *
 * @author  Abhirath Koushik
 * @date    02-21-2025
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netdb.h>
#include <syslog.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>
#include <errno.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT "9000"
#define BUFFER_SIZE 1024
#define USE_AESD_CHAR_DEVICE 1

#ifdef USE_AESD_CHAR_DEVICE
    #define FILE_PATH "/dev/aesdchar"
#else
    #define FILE_PATH "/var/tmp/aesdsocketdata"
#endif


// Global Variables
volatile sig_atomic_t terminate_program = 0;
int server_fd=-1;

// Declaring Mutex Variables
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Protects file write access
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER; // Protects access to the thread list

#if !USE_AESD_CHAR_DEVICE
// Declaring Timer Thread
pthread_t timer_thread_id;
#endif

/* Structure to hold thread information */
struct thread_info {
    pthread_t thread_id;
    int client_fd;
    LIST_ENTRY(thread_info) entries;
};

/* Global thread list head declaration */
LIST_HEAD(thread_info_list, thread_info);
struct thread_info_list thread_list;

/*
 * This function is used to handle Signals to terminate the program and handle cleaup tasks.
 *
 * Parameters:
 *   signo: The signal number that triggered this handler
 *
 * Returns:
 *   None
 */
void signal_handler(int signo)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    terminate_program = 1;
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

/*
 * This function is used to daemonize a given process and redirect the program outputs to detach from terminal
 *
 * Parameters:
 *   None
 *
 * Returns:
 *   None
 */
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed");
        exit(1);
    }

    if (pid > 0) {
        exit(0); // Parent exits
    }

    // Create a new session and detach from the controlling terminal
    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create a new session");
        exit(1);
    }

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "Failed to change working directory to /");
        exit(1);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdin, stdout, and stderr to /dev/null to avoid interaction with terminal
    open("/dev/null", O_RDONLY);  // stdin
    open("/dev/null", O_WRONLY);  // stdout
    open("/dev/null", O_WRONLY);  // stderr
}

#if !USE_AESD_CHAR_DEVICE
/*
 * This function is used to create and configure the timer thread for timestamps.
 *
 * Parameters:
 *   arg: Unused Argument
 * 
 * Returns:
 *   NULL
 *  
 */
void *timer_thread(void *arg)
{
    (void)arg; // Unused
    while (!terminate_program) {
        sleep(10);
        if (terminate_program) {
            break;
        }
        char timestamp[128];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);

        // Format the timestamp in a compliant format
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);
        
        // Ensure atomic write to the file using a mutex.
        pthread_mutex_lock(&file_mutex);
        int file_fd = open(FILE_PATH, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (file_fd != -1) {
            ssize_t ret = write(file_fd, timestamp, strlen(timestamp));
            if(ret < 0) {
                syslog(LOG_ERR, "Failed to write timestamp");
            }
            close(file_fd);
        } else {
            syslog(LOG_ERR, "Failed to open file for timestamp writing");
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}
#endif

/*
 * This is a Cleanup function called if a thread is cancelled or is exiting.
 *
 * Parameters:
 *   arg: Pointer to thread_info structure associated with the thread
 * 
 * Returns:
 *   None
 *  
 */
void thread_cleanup(void *arg)
{
    struct thread_info *tinfo = (struct thread_info *)arg;
    if (tinfo->client_fd != -1) {
        close(tinfo->client_fd);
    }
    pthread_mutex_lock(&list_mutex);
    LIST_REMOVE(tinfo, entries);
    pthread_mutex_unlock(&list_mutex);
    free(tinfo);
}

/*
 * This is a Thread function to handle Client Connections.
 *
 * Parameters:
 *   arg: Pointer to thread_info structure containing client socket details.
 * 
 * Returns:
 *   NULL
 *  
 */
void *handle_client(void *arg)
{
    struct thread_info *tinfo = (struct thread_info *)arg;
    int client_fd = tinfo->client_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    /* Register a cleanup handler to ensure thread cleanup on exit */
    pthread_cleanup_push(thread_cleanup, tinfo);

    int file_fd = open(FILE_PATH, O_CREAT | O_APPEND | O_RDWR, S_IRWXU | S_IRGRP | S_IROTH);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open device file %s: %s", FILE_PATH, strerror(errno));
    }

    while (1) {
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            break;
        }

        buffer[bytes_received] = '\0';

        // Check for AESDCHAR_IOCSEEKTO:X,Y pattern
        if (strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            unsigned int write_cmd, write_cmd_offset;
            if (sscanf(buffer + 19, "%u,%u", &write_cmd, &write_cmd_offset) == 2) {
                if (file_fd < 0) {
                    syslog(LOG_ERR, "Failed to open file for ioctl: %s", strerror(errno));
                } else {
                    struct aesd_seekto seekto;
                    seekto.write_cmd = write_cmd;
                    seekto.write_cmd_offset = write_cmd_offset;
                    if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) < 0) {
                        syslog(LOG_ERR, "Failed to perform ioctl: %s", strerror(errno));
                    }

                    char read_buf[BUFFER_SIZE];
                    ssize_t n;
                    while ((n = read(file_fd, read_buf, BUFFER_SIZE)) > 0)
                    {
                        send(tinfo->client_fd, read_buf, n, 0);
                    }
                    close(file_fd);
                }
            } else {
                syslog(LOG_ERR, "Invalid ioctl command format from client");
            }
            continue;
        }

        /* Synchronize file writes using a mutex */
        pthread_mutex_lock(&file_mutex);

        // Open file for appending
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file");
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        if (write(file_fd, buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "Failed to write to file");
            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        close(file_fd);
        pthread_mutex_unlock(&file_mutex);

        /* If the received data ends with a newline, send the file content to the client */
        if (buffer[bytes_received - 1] == '\n') {
            int file_fd_read = open(FILE_PATH, O_RDONLY);
            if (file_fd_read == -1) {
                syslog(LOG_ERR, "Failed to open file for reading");
                break;
            }
            ssize_t read_bytes;
            while ((read_bytes = read(file_fd_read, buffer, BUFFER_SIZE)) > 0) {
                if (send(client_fd, buffer, read_bytes, 0) == -1) {
                    syslog(LOG_ERR, "Failed to send data to client");
                    break;
                }
            }
            close(file_fd_read);
        }
    }

    // The cleanup handler will remove the thread info and free memory
    pthread_cleanup_pop(1);
    return NULL;
}


/*
 * This is the base function used for initiating the program execution.
 *
 * Parameters:
 *   argc: The number of command-line arguments passed to the program.
 *   argv: The array of command-line argument strings.
 *
 * Returns:
 *   On Success: 0
 *   On Failure: -1
 */
int main(int argc, char *argv[]) 
{
    struct addrinfo hints, *servinfo;
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int status, daemon_mode = 0;

    // Set up signal handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Open syslog
    openlog("aesdsocket", LOG_PID | LOG_PERROR, LOG_USER);

    // Parse arguments to check for -d option
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        }
    }

    // Configure hints structure
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       // Setting as IPv4
    hints.ai_socktype = SOCK_STREAM; // TCP Stream Socket
    hints.ai_flags = AI_PASSIVE;     // Use wildcard address

    // Get address info for the specified port
     if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }
    
    // Create socket
    server_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket");
        freeaddrinfo(servinfo);
        return -1;
    }

    // Set socket option to allow reuse of address and port
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "setsockopt failed");
        close(server_fd);
        freeaddrinfo(servinfo);
        return -1;
    }
    
    // Bind the socket to the address and port
    if (bind(server_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        syslog(LOG_ERR, "Bind failed");
        close(server_fd);
        freeaddrinfo(servinfo);
        return -1;
    }
    
    // Issue freeaddrinfo after bind step
    freeaddrinfo(servinfo);

    // Checking for Daemon after Binding if -d option was specified
    if (daemon_mode) {
        daemonize();
    }
    
    // Listen for connections
    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Listen failed");
        close(server_fd);
        return -1;
    }

    /* Initialize the global thread list */
    LIST_INIT(&thread_list);

    #if !USE_AESD_CHAR_DEVICE
    /* Create the timer thread to write timestamps every 10 seconds */
    if (pthread_create(&timer_thread_id, NULL, timer_thread, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timer thread");
    }
    #endif
    
    // Main server loop to handle incoming connections
    while (!terminate_program) {
        // Accept connection
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (terminate_program) {
                break;
            }
            syslog(LOG_ERR, "Accept failed");
            continue;
        }

        struct sockaddr_in *client_in = (struct sockaddr_in *)&client_addr;
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_in->sin_addr));

        // Allocate a new thread_info structure for this connection
        struct thread_info *tinfo = malloc(sizeof(struct thread_info));
        if (!tinfo) {
            syslog(LOG_ERR, "Failed to allocate memory for thread info");
            close(client_fd);
            continue;
        }
        tinfo->client_fd = client_fd;

        // Add the thread info to the global list
        pthread_mutex_lock(&list_mutex);
        LIST_INSERT_HEAD(&thread_list, tinfo, entries);
        pthread_mutex_unlock(&list_mutex);

         // Create a thread to handle the client
         if (pthread_create(&tinfo->thread_id, NULL, handle_client, tinfo) != 0) {
            syslog(LOG_ERR, "Failed to create thread");
            pthread_mutex_lock(&list_mutex);
            LIST_REMOVE(tinfo, entries);
            pthread_mutex_unlock(&list_mutex);
            close(client_fd);
            free(tinfo);
            continue;
         }
    }

    if (server_fd != -1){
        close(server_fd);
    }

    #if !USE_AESD_CHAR_DEVICE
    /* Cancel and join the timer thread */
    pthread_cancel(timer_thread_id);
    pthread_join(timer_thread_id, NULL);
    #endif

    /* Request exit from each thread by canceling them and then join */
    pthread_mutex_lock(&list_mutex);
    struct thread_info *entry;
    for (entry = LIST_FIRST(&thread_list); entry != NULL; ) {
        struct thread_info *next_entry = LIST_NEXT(entry, entries);
        pthread_cancel(entry->thread_id);
        pthread_mutex_unlock(&list_mutex);
        pthread_join(entry->thread_id, NULL);
        pthread_mutex_lock(&list_mutex);
        LIST_REMOVE(entry, entries);
        free(entry);
        entry = next_entry;
    }
    pthread_mutex_unlock(&list_mutex);

    #if !USE_AESD_CHAR_DEVICE
    remove(FILE_PATH);
    #endif
    
    closelog();
    return 0;
}
