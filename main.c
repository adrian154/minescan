#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

struct SocketState {
    int fd;
    in_addr_t addr;
    int payload_bytes_sent;
    unsigned char *packet_buf;
    int packet_bytes_read;
    int packet_length;
};

// For full documentation of ping protocol see https://wiki.vg/Server_List_Ping
const unsigned char ping_payload[] = {
    
    0x15, // packet length
    0x00, // packet ID (0 = handshake)
    0xff, 0xff, 0xff, 0xff, 0x0f, // protocol version number (-1 = ping),
    0x0b, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, // hostname (we just use 'example.com')
    0xdd, 0x36, // port (25565)
    0x01, // next state (1 = querying server status)
    
    0x01, // packet length
    0x00, // packet ID (0 = request status)

};

// Maximum number of epoll events that we try to process simultaneously.
#define EPOLL_MAX_EVENTS 64

// Reject suspiciously long responses
#define MAX_RESPONSE_SIZE 65536

// Number of sockets to open at a time
#define NUM_SOCKETS 10

/* Because all of our sockets are connected to different addresses, we can
   reuse the same outgoing port for all of them. This prevents issues with
   ephemeral port exhaustion. */
int connect_socket(int client_port, in_addr_t addr) {
    
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(socket_fd == -1) {
        perror("socket");
        return 1;
    }

    int optval = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(socket_fd);
        return -1;
    }

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client_port);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(socket_fd, (struct sockaddr *)&client_addr, sizeof(client_addr)) == -1) {
        perror("bind");
        close(socket_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(25565);
    server_addr.sin_addr.s_addr = addr;
    if(connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1 && errno != EINPROGRESS) {
        perror("connect");
        close(socket_fd);
        return -1;
    }

    return socket_fd;

}

int add_socket(int epoll_fd, int client_port, char *addrstr) {

    in_addr_t addr = inet_addr(addrstr);
    int socket_fd = connect_socket(client_port, addr);
    if(socket_fd == -1) {
        return -1;
    }

    struct SocketState *state = malloc(sizeof(struct SocketState));
    if(state == NULL) {
        fprintf(stderr, "failed to allocate socket state");
        close(socket_fd);
        return -1;
    }

    state->fd = socket_fd;
    state->addr = addr;
    state->packet_buf = NULL;
    state->packet_bytes_read = 0;
    state->packet_length = 0;
    state->payload_bytes_sent = 0;

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT;
    event.data.ptr = state;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
        perror("epoll_ctl");
        close(socket_fd);
        free(state);
        return -1;
    }

    return 0;

}

void parse_packet(struct SocketState *state) {

    char addr_str[32];
    inet_ntop(AF_INET, &state->addr, addr_str, 32);

    // find opening brace
    int start_pos;
    for(start_pos = 0; start_pos < state->packet_length; start_pos++) {
        if(state->packet_buf[start_pos] == '{') {
            break;
        }
    }

    int length = state->packet_length - start_pos;
    printf("%s: %.*s", addr_str, length, state->packet_buf + start_pos);
    fflush(stdout);

}

void close_socket(struct SocketState *state, int *num_tracked_fds) {
    close(state->fd);
    (*num_tracked_fds)--;
    free(state->packet_buf);
    free(state);
}

int main(int argc, char **argv) {

    // create epoll
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    struct epoll_event *events = malloc(EPOLL_MAX_EVENTS);
    if(events == NULL) {
        fprintf(stderr, "failed to allocate events array");
        return 1;
    }

    // We need to manually track the number of watched sockets
    int num_tracked_fds = 0;

    do {

        // Maintain a steady number of sockets by opening new ones as necessary
        if(num_tracked_fds == 0) {
            if(add_socket(epoll_fd, 12345, argv[1]) == -1) {
                return 1;
            }
            num_tracked_fds++;
        }

        // watch for events
        int num_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
        if(num_events == -1) {
            perror("epoll_wait");
            return 1;
        }


        for(int i = 0; i < num_events; i++) {

            struct epoll_event *event = &events[i];
            struct SocketState *state = event->data.ptr;

            /* If an error occurred or the server closed the connection, we 
               can remove the socket. */
            if(event->events & EPOLLERR) {
                close_socket(state, &num_tracked_fds);
                continue;
            }

            /* If we can write to the socket, check if there is data that needs
               to be sent. */
            if(event->events & EPOLLOUT) {
                if(state->payload_bytes_sent < sizeof(ping_payload)) {
                    int bytes_written = write(state->fd, ping_payload + state->payload_bytes_sent, sizeof(ping_payload) - state->payload_bytes_sent);
                    if(bytes_written == -1) {
                        if(errno != EAGAIN && errno != EWOULDBLOCK) {
                            close_socket(state, &num_tracked_fds);
                        }
                        continue;
                    }
                    state->payload_bytes_sent += bytes_written;
                }
            }

            /* Read packet from socket. */
            if(event->events & EPOLLIN) {

                if(state->packet_buf == NULL) {

                    /* Assume that we will never need more than one read() call
                       to read the entire packet length field. */
                    unsigned char buf[5];
                    int bytes_read = read(state->fd, buf, sizeof(buf));
                    if(bytes_read == -1) {
                        if(errno != EAGAIN && errno != EWOULDBLOCK) {
                            close_socket(state, &num_tracked_fds);
                        }
                        continue;
                    }

                    /* Packet length is encoded as a variable length integer
                       where the MSB indicates whether there are more bits. */
                    unsigned long packet_length = 0;
                    int pos = 0;
                    while(pos < bytes_read) {

                        unsigned char byte = buf[pos];
                        packet_length |= (byte & 0x7f) << (pos * 7);
                        pos++;

                        if((byte & 0x80) == 0) {
                            break;
                        }

                        
                        // VarInts are never longer than 5 bytes
                        if(pos > 5) {
                            close_socket(state, &num_tracked_fds);
                            continue;
                        }

                    }

                    if(packet_length == 0 || packet_length > MAX_RESPONSE_SIZE) {
                        close_socket(state, &num_tracked_fds);
                        continue;
                    }

                    state->packet_length = packet_length;
                    state->packet_buf = malloc(packet_length);
                    if(state->packet_buf == NULL) {
                        close_socket(state, &num_tracked_fds);
                        continue;
                    }

                    /* We'll probably accidentally read some bytes that aren't
                       part of the packet length; copy these to the packet
                       buffer. */
                    for(int i = pos; i < 5; i++) {
                        state->packet_buf[i - pos] = buf[i];
                    }
                    state->packet_bytes_read += 5 - pos;

                }

                int remaining_bytes = state->packet_length - state->packet_bytes_read;
                int bytes_read = read(state->fd, state->packet_buf + state->packet_bytes_read, remaining_bytes);
                if(bytes_read == -1) {
                    if(errno != EAGAIN && errno != EWOULDBLOCK) {
                         close_socket(state, &num_tracked_fds);
                    }
                    continue;
                }
                state->packet_bytes_read += bytes_read;

                if(state->packet_bytes_read == state->packet_length) {
                    parse_packet(state);
                    close_socket(state, &num_tracked_fds);
                    continue;
                }

            }

            if(event->events & EPOLLHUP) {
                close_socket(state, &num_tracked_fds);
            }

        }

    } while(num_tracked_fds > 0);

    // close epoll
    if(close(epoll_fd)) {
        perror("close");
        return 1;
    }

    return 0;

}