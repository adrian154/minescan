#include "sqlite/sqlite3.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "addr-gen.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

struct SocketState {
    int fd;
    in_addr_t addr;
    int payload_bytes_sent;
    char *packet_buf;
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
    0x00  // packet ID (0 = request status)

};

// Maximum number of epoll events that we try to process simultaneously
#define EPOLL_MAX_EVENTS 10000

// Limit on response size from server
#define MAX_RESPONSE_SIZE 65536

// Number of sockets to open at a time
#define MAX_SOCKETS 4000

// Client port used for outgoing connections
#define CLIENT_PORT 12345

int setup_db(sqlite3 **db, sqlite3_stmt **stmt) {
    
    // set up sqlite3 database
    int result = sqlite3_open("results.db", db);
    if(result != SQLITE_OK) {
        fprintf(stderr, "failed to open database: %s\n", sqlite3_errstr(result));
        sqlite3_close(*db);
        return 1;
    }

    // create table
    const char *create_table_query = "CREATE TABLE IF NOT EXISTS servers (address TEXT NOT NULL, timestamp INTEGER NOT NULL, response TEXT NOT NULL)";
    char *err_msg;
    result = sqlite3_exec(*db, create_table_query, NULL, NULL, &err_msg);
    if(result != SQLITE_OK) {
        fprintf(stderr, "failed to create table: %s\n", err_msg);
        sqlite3_close(*db);
        return 1;
    }

    // prepare insert statement
    const char *insert_query = "INSERT INTO servers (address, timestamp, response) VALUES (?, ?, ?)";
    result = sqlite3_prepare_v2(*db, insert_query, -1, stmt, NULL);
    if(result != SQLITE_OK) {
        fprintf(stderr, "failed to prepare statement: %s\n", sqlite3_errmsg(*db));
    }

    return 0;
    
}

int connect_socket(int client_port, in_addr_t addr) {
    
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(socket_fd == -1) {
        perror("socket");
        return -1;
    }

    // To avoid ephemeral port exhaustion, reuse the same client port for all outgoing connections (this works because each connection is to a different IP)
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
        if(errno != ENETUNREACH) {
            char buf[32];
            inet_ntop(AF_INET, &addr, buf, 32);
            fprintf(stderr, "(address %s) ", buf);
            perror("connect");
        }
        close(socket_fd);
        return -1;
    }

    return socket_fd;

}

int add_socket(int epoll_fd, int client_port, in_addr_t addr, int *num_tracked_fds) {

    int socket_fd = connect_socket(client_port, addr);
    if(socket_fd == -1) {
        return 1;
    }

    struct SocketState *state = malloc(sizeof(struct SocketState));
    if(state == NULL) {
        fprintf(stderr, "failed to allocate socket state\n");
        close(socket_fd);
        return 1;
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
        return 1;
    }

    (*num_tracked_fds)++;
    return 0;

}

void parse_packet(struct SocketState *state, sqlite3_stmt *stmt) {

    char addr_str[32];
    inet_ntop(AF_INET, &state->addr, addr_str, 32);

    // find opening brace
    int start_pos = 0;
    while(start_pos < state->packet_length && state->packet_buf[start_pos] != '{') {
        start_pos++;
    }

    int length = state->packet_length - start_pos;
    if(length == 0) {
        return;
    }

    printf("found a server on %s\n", addr_str);

    // FIXME: Check bind calls for errors
    sqlite3_bind_text(stmt, 1, addr_str,  -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, time(NULL));
    sqlite3_bind_text(stmt, 3, state->packet_buf + start_pos, length, SQLITE_TRANSIENT);
    int result = sqlite3_step(stmt);
    if(result != SQLITE_DONE) {
        fprintf(stderr, "failed to insert result for %s: %s\n", addr_str, sqlite3_errstr(result));
    }

    sqlite3_reset(stmt);

}

void close_socket(struct SocketState *state, int *num_tracked_fds) {
    close(state->fd);
    (*num_tracked_fds)--;
    free(state->packet_buf);
    free(state);
}

int main(void) {

    sqlite3 *db;
    sqlite3_stmt *stmt;
    if(setup_db(&db, &stmt)) {
        return 1;
    }

    // Create epoll
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) {
        perror("epoll_create1");
        sqlite3_close(db);
        return 1;
    }

    struct epoll_event *events = malloc(EPOLL_MAX_EVENTS * sizeof(struct epoll_event));

    // Keep track of how many sockets are currently watched
    int num_tracked_fds = 0;

    struct AddressGenerator addr_gen;
    if(init_addrgen(&addr_gen)) {
        return 1;
    }

    do {

        // Open new sockets as necessary
        for(int i = num_tracked_fds; i < MAX_SOCKETS; i++) {
            in_addr_t addr = next_address(&addr_gen);
            if(addr == 0) {
                break;
            }
            if(add_socket(epoll_fd, CLIENT_PORT, addr, &num_tracked_fds)) {
                continue;
            }
        }

        // Wait for events to arrive
        int num_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
        if(num_events == -1) {
            perror("epoll_wait");
            sqlite3_close(db);
            return 1;
        }

        for(int i = 0; i < num_events; i++) {

            struct epoll_event *event = &events[i];
            struct SocketState *state = event->data.ptr;

            // If an error occurred or the server closed the connection, remove the socket
            if(event->events & EPOLLERR) {
                close_socket(state, &num_tracked_fds);
                continue;
            }

            // If socket is writable, check if there is data to be written
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

            // Read data if socket is readable
            if(event->events & EPOLLIN) {

                if(state->packet_buf == NULL) {

                    // We assume that we will never need more than one read() call to read the entire packet length field
                    unsigned char packetlen_buf[5];
                    int bytes_read = read(state->fd, packetlen_buf, 5);
                    if(bytes_read != 5) {
                        close_socket(state, &num_tracked_fds);
                        continue;
                    }

                    // Packet length is encoded as a variable length integer where the MSB indicates whether there are more bits.
                    unsigned long packet_length = 0;
                    int pos = 0;
                    while(1) {

                        unsigned char byte = packetlen_buf[pos];
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

                    // Reject packets with nonsense packet length
                    if(packet_length == 0 || packet_length > MAX_RESPONSE_SIZE) {
                        close_socket(state, &num_tracked_fds);
                        continue;
                    }

                    state->packet_length = packet_length;
                    state->packet_buf = malloc(packet_length);
                    if(state->packet_buf == NULL) {
                        fprintf(stderr, "failed to allocate response buffer\n");
                        return 1; // OOM
                    }

                    // The earlier read() call probably read some bytes of the packet body along with the packet length field, copy these to the packet buffer
                    for(int i = pos; i < 5; i++) {
                        state->packet_buf[i - pos] = packetlen_buf[i];
                    }
                    state->packet_bytes_read += 5 - pos;

                }

                int remaining_bytes = state->packet_length - state->packet_bytes_read;
                int bytes_read = read(state->fd, state->packet_buf + state->packet_bytes_read, remaining_bytes);
                if(bytes_read == -1) {
                    close_socket(state, &num_tracked_fds);
                    continue;
                }
                state->packet_bytes_read += bytes_read;

                if(state->packet_bytes_read == state->packet_length) {
                    parse_packet(state, stmt);
                    close_socket(state, &num_tracked_fds);
                    continue;
                }

            }

            if(event->events & EPOLLHUP) {
                close_socket(state, &num_tracked_fds);
            }

        }

    } while(num_tracked_fds > 0);

    close(epoll_fd);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;

}