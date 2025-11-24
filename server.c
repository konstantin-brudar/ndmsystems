#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>


#define SERVER_PORT 12345
#define LISTEN_BACKLOG 50
#define BUFFER_SIZE 1024
#define SMALL_BUFFER_SIZE 256
#define MAX_EVENTS_NUMBER 20

int is_server_running = 0;
int total_clients_number = 0;
int currently_connected_clients = 0;

int create_tcp_socket();
int create_udp_socket();
int create_epoll();
int wait_epoll_events(int epoll_fd, struct epoll_event * events, int events_size);
void add_socket_to_epoll(int epoll_fd, int socket_fd);
void remove_socket_from_epoll(int epoll_fd, int socket_fd);
void process_command(const char * buffer, int buffer_size, char * data, int data_size);


int main()
{
    int tcp_server_fd = create_tcp_socket();
    int udp_server_fd = create_udp_socket();
    int epoll_fd = create_epoll();

    add_socket_to_epoll(epoll_fd, tcp_server_fd);
    add_socket_to_epoll(epoll_fd, udp_server_fd);

    is_server_running = 1;
    total_clients_number = 0;
    currently_connected_clients = 0;

    char buffer[BUFFER_SIZE];
    struct epoll_event events[MAX_EVENTS_NUMBER];

    while (is_server_running) {
        int events_number = wait_epoll_events(epoll_fd, events, sizeof(events) / sizeof(events[0]));

        for (int i = 0; i < events_number; i++) {
            struct epoll_event event = events[i];
            int event_fd = event.data.fd;

            if (event.events & EPOLLRDHUP) {
                printf("Client disconnected\n");
                remove_socket_from_epoll(epoll_fd, event_fd);
                close(event_fd);
                currently_connected_clients--;
            } else if (event.events & EPOLLIN) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_size = sizeof(client_addr);

                if (event_fd == tcp_server_fd) {
                    printf("Client connected\n");
                    int client_fd = accept(tcp_server_fd, (struct sockaddr*) &client_addr, &client_addr_size);
                    add_socket_to_epoll(epoll_fd, client_fd);
                    total_clients_number++;
                    currently_connected_clients++;
                } else if (event_fd == udp_server_fd) {
                    int client_fd = event_fd;
                    int bytes_read = recvfrom(client_fd, buffer, sizeof(buffer), 0, (struct sockaddr*) &client_addr, &client_addr_size);
                    int length = (buffer[bytes_read - 1] == '\n' ? bytes_read - 1 : bytes_read);

                    if (bytes_read > 0) {
                        printf(">> %.*s\n", length, buffer);
                    } else if (bytes_read == 0) {
                        continue;
                    } else if (bytes_read == -1) {
                        perror("recvfrom() failed");
                        continue;
                    }

                    int is_command = (buffer[0] == '/');

                    if (is_command) {
                        char data[SMALL_BUFFER_SIZE];

                        process_command(buffer, length, data, sizeof(data));

                        sendto(client_fd, data, strlen(data), 0, (struct sockaddr*) &client_addr, client_addr_size);
                        printf("<< %s", data);
                    } else {
                        int bytes_written = sendto(client_fd, buffer, bytes_read, 0, (struct sockaddr*) &client_addr, client_addr_size);
                        int length = (buffer[bytes_written - 1] == '\n' ? bytes_written - 1 : bytes_written);

                        if (bytes_written > 0) {
                            printf("<< %.*s\n", length, buffer);
                        } else if (bytes_written == -1) {
                            perror("sendto() failed");
                        }
                    }
                } else {
                    int client_fd = event_fd;
                    int bytes_read = read(client_fd, buffer, sizeof(buffer));
                    int length = (buffer[bytes_read - 1] == '\n' ? bytes_read - 1 : bytes_read);

                    if (bytes_read > 0) {
                        printf(">> %.*s\n", length, buffer);
                    } else if (bytes_read == 0) {
                        printf("Client disconnected\n");
                        remove_socket_from_epoll(epoll_fd, client_fd);
                        close(client_fd);
                        currently_connected_clients--;
                        continue;
                    } else if (bytes_read == -1) {
                        perror("read() failed");
                        continue;
                    }

                    int is_command = (buffer[0] == '/');

                    if (is_command) {
                        char data[SMALL_BUFFER_SIZE];

                        process_command(buffer, length, data, sizeof(data));

                        write(client_fd, data, strlen(data));
                        printf("<< %s", data);
                    } else {
                        int bytes_written = write(client_fd, buffer, bytes_read);
                        int length = (buffer[bytes_written - 1] == '\n' ? bytes_written - 1 : bytes_written);

                        if (bytes_written > 0) {
                            printf("<< %.*s\n", length, buffer);
                        } else if (bytes_written == -1) {
                            perror("write() failed");
                        }
                    }
                }
            }
        }
    }

    close(tcp_server_fd);
    close(udp_server_fd);
    close(epoll_fd);

    return EXIT_SUCCESS;
}


int create_tcp_socket()
{
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_fd == -1) {
        perror("tcp socket() failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("tcp bind() failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, LISTEN_BACKLOG) == -1) {
        perror("listen() failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    return socket_fd;
}


int create_udp_socket()
{
    int socket_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socket_fd == -1) {
        perror("udp socket() failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("udp bind() failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    return socket_fd;
}


int create_epoll()
{
    int epoll_fd = epoll_create(1);

    if (epoll_fd == -1) {
        perror("epoll_create() failed");
        exit(EXIT_FAILURE);
    }

    return epoll_fd;
}


int wait_epoll_events(int epoll_fd, struct epoll_event * events, int events_size)
{
    int events_number = epoll_wait(epoll_fd, events, events_size, -1);

    if (events_number == -1) {
        perror("epoll_wait() failed");
        exit(EXIT_FAILURE);
    }

    return events_number;
}


void add_socket_to_epoll(int epoll_fd, int socket_fd)
{
    struct epoll_event event = {
            .events = EPOLLIN | EPOLLRDHUP,
            .data.fd = socket_fd
    };

    int result = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event);

    if (result == -1) {
        perror("epoll_ctl(add) failed");
        exit(EXIT_FAILURE);
    }
}


void remove_socket_from_epoll(int epoll_fd, int socket_fd)
{
    struct epoll_event event = {
            .events = EPOLLIN | EPOLLRDHUP,
            .data.fd = socket_fd
    };

    int result = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, socket_fd, &event);

    if (result == -1) {
        perror("epoll_ctl(del) failed");
        exit(EXIT_FAILURE);
    }
}


void process_command(const char * buffer, int buffer_size, char * data, int data_size)
{
    if (!strncmp(buffer, "/time", buffer_size)) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        snprintf(data, data_size, "%04d-%02d-%02d %02d:%02d:%02d\n",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
    } else if (!strncmp(buffer, "/stats", buffer_size)) {
        snprintf(data, data_size, "Total tcp clients number: %d; Currently connected tcp clients: %d\n",
                 total_clients_number, currently_connected_clients);
    } else if (!strncmp(buffer, "/shutdown", buffer_size)) {
        snprintf(data, data_size, "Server shutdown\n");
        is_server_running = 0;
    } else {
        snprintf(data, data_size, "No such command\n");
    }
}
