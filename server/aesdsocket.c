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
#include <pthread.h>
#include "queue.h"

#define PORT "9000"
#define BACKLOG 10
#define TEMPFILE "/var/tmp/aesdsocketdata"
#define MAX_LINE_LENGTH 65536

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int server_fd;
static int temp_fd;
static pthread_t timer_thread;
static int running_as_usual;

struct node {
	pthread_t thread;
	SLIST_ENTRY(node) next;
};

SLIST_HEAD(head_s, node) head;

struct timer_thread_arg_s {
    int num_secs;
    int exit_code;
};

struct client_thread_arg_s {
	int connection_fd;
    int exit_code;
};

void *timer_thread_worker(void *thread_param) {
    struct timespec tp0, tp1;
    char timestr[25];
    size_t b_written;
    time_t rawtime;
    int interval = 10;

    clock_gettime(CLOCK_MONOTONIC, &tp0);

    while (running_as_usual) {
        do {
            clock_gettime(CLOCK_MONOTONIC, &tp1);
            usleep(50000);
        } while (tp1.tv_sec - tp0.tv_sec < interval && running_as_usual);

        if (running_as_usual) {
            // get a head start on the next time delta (pointless!)
            clock_gettime(CLOCK_MONOTONIC, &tp0);

            // timestamp:00001122334455"
            time(&rawtime);
            strftime(timestr, 26, "timestamp:%Y%m%d%H%M%S\n", localtime(&rawtime));

            pthread_mutex_lock(&mutex);

            b_written = write(temp_fd, timestr, strlen(timestr));

            pthread_mutex_unlock(&mutex);

            if (b_written == -1) {
                perror("write");
                syslog(LOG_ERR, "Error writing to temp file.");
            }
        }
    }

    return thread_param;
}

void *client_thread_worker(void *thread_param) {
    struct client_thread_arg_s* thread_args = (struct client_thread_arg_s*) thread_param;

	int conn_fd = thread_args->connection_fd;

	// read from client until newlinez
	char curr;
	char line[MAX_LINE_LENGTH + 2];

	memset(&line, '\0', MAX_LINE_LENGTH + 2);

	int index = 0;
	int ret;

    do {
        ret = recv(conn_fd, &curr, 1, 0);

		if (ret == 1) {
            if (curr != '\r' && curr != '\n') {
                line[index++] = curr;

                if (index > MAX_LINE_LENGTH) {
                    // empty the string so we ignore this oversized line
                    line[0] = '\0';
                    break;
                }
            }
        } else if (ret == -1 && (errno != EAGAIN || errno != EWOULDBLOCK)) {
            thread_args->exit_code = 1;
        }
	} while (curr != '\n' && !thread_args->exit_code);

	if (!thread_args->exit_code && strlen(line) > 0) {
		// printf("Bytes: %lu; Len: %lu; Heard: '%s'\n", strlen(line) * sizeof(char), strlen(line), line);
		line[index] = '\n';

		pthread_mutex_lock(&mutex);
		
		// we are writing in append mode
		ret = write(temp_fd, line, strlen(line));

		pthread_mutex_unlock(&mutex);

		if (ret == -1) {
			perror("write");
            thread_args->exit_code = 1;
		}
	}

    if (!thread_args->exit_code) {
        int offset = 0;

        // get one line at a time from TEMPFILE
        do {
            index = 0;

            memset(&line, '\0', MAX_LINE_LENGTH + 1);

            pthread_mutex_lock(&mutex);

            lseek(temp_fd, offset, SEEK_SET);
            
            while (read(temp_fd, &curr, sizeof(char) && index <= MAX_LINE_LENGTH) > 0) {
                line[index++] = curr;

                if (curr == '\n') {
                    line[index] = '\0';

                    break;
                }
            }

            pthread_mutex_unlock(&mutex);

            if (index > MAX_LINE_LENGTH) {
                line[0] = '\0';
            }

            offset += index;

            if (strlen(line) > 0) {
                while ((ret = send(conn_fd, line, strlen(line) * sizeof(char), MSG_NOSIGNAL)) == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));

                if (ret == -1) {
                    thread_args->exit_code = 1;
                }
            }
        } while (!thread_args->exit_code && strlen(line) > 0);
    }

    close(conn_fd);

	return thread_param;
}

void cleanup() {
    running_as_usual = false;

    pthread_mutex_lock(&mutex);

    shutdown(server_fd, SHUT_RDWR);
    
    pthread_mutex_unlock(&mutex);

    void* thread_rtn;

	// loop over the threads and join them one by one
	while (!SLIST_EMPTY(&head)) {
        struct node *first = SLIST_FIRST(&head);

		if ((pthread_join(first->thread, &thread_rtn)) != 0) {
			syslog(LOG_WARNING, "Could not client thread.");
		}

        free(thread_rtn);

		SLIST_REMOVE_HEAD(&head, next);

		free(first);
		first = NULL;
	}

    if (pthread_join(timer_thread, &thread_rtn) != 0) {
        syslog(LOG_WARNING, "Could not join timer thread.");
    }

    free(thread_rtn);

    close(server_fd);
    close(temp_fd);
    
    remove(TEMPFILE);

    closelog();
}

void sigterm_handler(int signo) {
    syslog(LOG_DEBUG, "Caught signal, exiting");

	cleanup();
	
    exit(EXIT_SUCCESS);
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int main (int argc, char **argv) {
    running_as_usual = true;

    openlog(NULL, 0, LOG_USER);
    
    if (argc > 1) {
        if ((strcmp(argv[1], "-d")) == 0) {
            if (daemon(0, 0) != 0) {
                syslog(LOG_ERR, "Could not daemonize.");
                exit(EXIT_FAILURE);
            }
        } else {
            syslog(LOG_ERR, "Invalid arguments provided.");
            exit(EXIT_FAILURE);
        }
    }

    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *p;

    struct sockaddr_storage remote_addr;
    socklen_t sin_size;
    struct sigaction sa;

    int yes = 1;
    
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));

        exit(EXIT_FAILURE);
    }

    // loop and bind
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("aesdsocket: socket");

            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");

            exit(EXIT_FAILURE);
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_fd);
            perror("aesdsocket: bind");

            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "aesdsocket: failed to bind\n");

        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");

        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigterm_handler;

    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa, NULL) == -1 || sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");

        exit(EXIT_FAILURE);
    }

	// ready the tempfile
    temp_fd = open(TEMPFILE, O_CREAT | O_RDWR | O_APPEND, 0400 | 0200);

	if (temp_fd < 0) {
		fprintf(stderr, "Could not open temp file");
		
        exit(EXIT_FAILURE);
	}

    // init the client thread list
	SLIST_INIT(&head);

    // start the timer thread
    pthread_create(&timer_thread, NULL, timer_thread_worker, NULL);

    while (true) {
        sin_size = sizeof(remote_addr);

        int connection_fd = accept(server_fd, (struct sockaddr *)&remote_addr, &sin_size);

        if (connection_fd == -1 && !running_as_usual) {
            perror("accept");
            syslog(LOG_WARNING, "accept");

            continue;
        }

        inet_ntop(
            remote_addr.ss_family,
            get_in_addr((struct sockaddr *)&remote_addr),
            s, sizeof(s)
        );

        syslog(LOG_DEBUG, "Accepted connection from %s", s);

        struct client_thread_arg_s *thread_args = (struct client_thread_arg_s*) malloc(sizeof(struct client_thread_arg_s));

        thread_args->connection_fd = connection_fd;
        thread_args->exit_code = 0;

		pthread_t thread_id;

        if (pthread_create(&thread_id, NULL, client_thread_worker, thread_args) != 0) {
			syslog(LOG_ERR, "Could not create thread!");
			cleanup();

			exit(EXIT_FAILURE);
		}
		
        struct node *elem = malloc(sizeof(struct node));

        if (elem == NULL) {
            syslog(LOG_ERR, "malloc failed!");
            exit(EXIT_FAILURE);
        }

        elem->thread = thread_id;

		SLIST_INSERT_HEAD(&head, elem, next);
    }
 
    return EXIT_SUCCESS;
}

// use valgrind to check for memory leaks
/*
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-out.txt ./aesdsocket
*/