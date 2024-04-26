// needed for daemon() function and sigaction stuff
#define _DEFAULT_SOURCE
#define __USE_XOPEN

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
#include <stdbool.h>

#define PORT "9000"
#define BACKLOG 10
#define TEMPFILE "/var/tmp/aesdsocketdata"
#define MAX_LINE_LENGTH 32768

int server_fd;
int connection_fd;
int temp_fd;

void sigterm_handler(int signo) {
    syslog(LOG_DEBUG, "Caught signal, exiting");

    shutdown(server_fd, SHUT_RD);

    sleep(1);

    shutdown(server_fd, SHUT_WR);

    close(server_fd);
    close(temp_fd);
    
    remove(TEMPFILE);

    exit(0);
}


void sigchld_handler(int signo) {
    int saved_errno = errno;
    
    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int main (int argc, char **argv) {
    openlog(NULL, 0, LOG_USER);

    if (argc > 1) {
        if ((strcmp(argv[1], "-d")) == 0) {
            if (daemon(0, 0) != 0) {
                syslog(LOG_ERR, "Could not daemonize.");
                exit(1);
            }
        } else {
            syslog(LOG_ERR, "Invalid arguments provided.");
            exit(1);
        }
    }

    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *p;

    struct sockaddr_storage remote_addr;
    socklen_t sin_size;
    struct sigaction sa, sa2;

    int yes = 1;
    
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        syslog(LOG_ERR, "getaddrinfo:%s", gai_strerror(rv));

        exit(1);
    }

    // loop and bind
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("aesdsocket: socket");
            syslog(LOG_WARNING, "socket");

            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            syslog(LOG_ERR, "setsockopt");

            exit(1);
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            perror("aesdsocket: bind");
            syslog(LOG_WARNING, "bind");

            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "aesdsocket: failed to bind\n");
        syslog(LOG_ERR, "failed to bind");

        exit(1);
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        syslog(LOG_ERR, "listen");

        exit(1);
    }

    sa.sa_handler = sigchld_handler;

    sigemptyset(&sa.sa_mask);
    
    sa.sa_flags = SA_RESTART;
    
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        syslog(LOG_ERR, "sigaction (sigchld_handler)");

        exit(1);
    }

    sa2.sa_handler = sigterm_handler;

    sigemptyset(&sa2.sa_mask);

    sa2.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa2, NULL) == -1 || sigaction(SIGINT, &sa2, NULL) == -1) {
        perror("sigaction");
        syslog(LOG_ERR, "sigaction (sigterm_handler)");

        exit(1);
    }

    // printf("aesdsocket: waiting for connections...\n");

    while (true) {
        sin_size = sizeof(remote_addr);

        connection_fd = accept(server_fd, (struct sockaddr *)&remote_addr, &sin_size);

        if (connection_fd == -1) {
            perror("accept");
            syslog(LOG_WARNING, "accept");

            continue;
        }

        inet_ntop(
            remote_addr.ss_family,
            get_in_addr((struct sockaddr *)&remote_addr),
            s, sizeof(s)
        );

        // printf("aesdsocket: got connection from %s\n", s);
        syslog(LOG_DEBUG, "Accepted connection from %s", s);

        if (!fork()) {
            close(server_fd);

            temp_fd = open(TEMPFILE, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);

            if (temp_fd < 0) {
                syslog(LOG_WARNING, "could not open temp file");

                // just send an empty string back
                if (send(connection_fd, "\n", 1, 0) == -1) {
                    syslog(LOG_ERR, "Error sending to %s", s);
                    close(connection_fd);
                    
                    return 1;
                }
            }

            // read from client until newline
            char curr;

            char line[MAX_LINE_LENGTH + 2];
            memset(&line, '\0', MAX_LINE_LENGTH + 2);

            int index = 0;

            while (true) {
                recv(connection_fd, &curr, 1, 0);

                if (curr == '\n') {
                    break;
                } else if (curr != '\r') {
                    line[index++] = curr;

                    //if (strlen(line) > MAX_LINE_LENGTH) {
                    if (index > MAX_LINE_LENGTH) {
                        // printf("line is '%s'; len is %lu.\n", line, strlen(line));

                        syslog(LOG_ERR, "Max line length exceded (in).");
                        
                        // empty the string so we ignore this line
                        line[0] = '\0';

                        break;
                    }
                }
            }

            if (strlen(line) > 0) {
                // printf("Bytes: %lu; Len: %lu; Heard: '%s'\n", strlen(line) * sizeof(char), strlen(line), line);
                line[index] = '\n';

                // we are writing in append mode
                size_t b_written = write(temp_fd, line, strlen(line));

                if (b_written == -1) {
                    perror("write");
                    syslog(LOG_ERR, "Error writing to temp file.");

                    exit(1);
                }
            }

            // we need to reset the pointer to beginning of file
            lseek(temp_fd, 0, SEEK_SET);

            // get one line at a time from TEMPFILE
            while (true) {
                index = 0;

                memset(&line, '\0', MAX_LINE_LENGTH + 1);

                while (read(temp_fd, &curr, sizeof(char)) > 0) {
                    line[index++] = curr;

                    if (index > MAX_LINE_LENGTH) {
                        syslog(LOG_ERR, "Max line length exceeded (out).");

                        close(connection_fd);

                        exit(1);
                    }

                    if (curr == '\n') {
                        line[index] = '\0';

                        break;
                    }
                }

                if (index > 0) {
                    if (send(connection_fd, line, strlen(line) * sizeof(char), 0) == -1) {
                        syslog(LOG_ERR, "Error sending to %s", s);

                        close(connection_fd);

                        exit(1);
                    }
                } else {
                    // done sending file contents
                    close(connection_fd);

                    syslog(LOG_DEBUG, "Closed connection from %s", s);

                    exit(0);
                }
            }
        }

        close(connection_fd);
    }

    return 0;
}

// use valgrind to check for memory leaks
/*
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-out.txt ./aesdsocket
*/