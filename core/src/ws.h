#pragma once
#include <stddef.h>

/* Maximum simultaneous WebSocket clients */
#define WS_MAX_CLIENTS 8

/* Called for each text frame received from any client.
   json: null-terminated UTF-8 payload. */
typedef void (*WsMsgHandler)(const char *json);

/* Initialise TCP listen socket on port. Returns 0 on success. */
int  ws_init(uint16_t port, WsMsgHandler handler);

/* Return listen fd — add to epoll with EPOLLIN. */
int  ws_listen_fd(void);

/* Accept a new client (call when listen fd is readable).
   Returns the new client fd (already added to client set), or -1. */
int  ws_accept(void);

/* Process I/O on a client fd (call when it's readable).
   Returns -1 if the client disconnected (fd is closed + removed). */
int  ws_client_read(int fd);

/* Send a text frame to all connected clients. */
void ws_broadcast(const char *json);

/* Send a text frame to one specific client fd. */
void ws_send(int fd, const char *json);

/* Close all clients + listen socket. */
void ws_destroy(void);

/* Iterate connected client fds (for registering with epoll externally).
   Call ws_client_fds() to get snapshot. Returns count. */
int  ws_client_fds(int *fds_out, int max);
