#include "ws.h"
#include "../third_party/sha1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

/* ── WebSocket magic ──────────────────────────────────────────────────────── */
#define WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* ── Client state ─────────────────────────────────────────────────────────── */
typedef enum { CS_HANDSHAKE, CS_OPEN, CS_CLOSED } ClientState;

typedef struct {
    int         fd;
    ClientState state;
    char        rbuf[4096];
    int         rlen;
} WsClient;

static int        g_listen_fd = -1;
static WsClient   g_clients[WS_MAX_CLIENTS];
static WsMsgHandler g_handler = NULL;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static WsClient *find_client(int fd) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (g_clients[i].fd == fd) return &g_clients[i];
    return NULL;
}

static WsClient *find_free(void) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (g_clients[i].fd < 0) return &g_clients[i];
    return NULL;
}

static void close_client(WsClient *c) {
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    c->state = CS_CLOSED;
    c->rlen  = 0;
}

/* ── WebSocket handshake ──────────────────────────────────────────────────── */

static void do_handshake(WsClient *c) {
    /* Find Sec-WebSocket-Key header */
    char *key_hdr = strcasestr(c->rbuf, "Sec-WebSocket-Key:");
    if (!key_hdr) return;

    key_hdr += strlen("Sec-WebSocket-Key:");
    while (*key_hdr == ' ') key_hdr++;
    char key[128] = {0};
    int ki = 0;
    while (*key_hdr && *key_hdr != '\r' && *key_hdr != '\n' && ki < 127)
        key[ki++] = *key_hdr++;
    key[ki] = '\0';

    /* Accept = base64(SHA1(key + magic)) */
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", key, WS_MAGIC);
    uint8_t digest[20];
    sha1((const uint8_t *)combined, strlen(combined), digest);
    char accept[64];
    base64_encode(digest, 20, accept);

    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept);

    send(c->fd, response, strlen(response), MSG_NOSIGNAL);
    c->state = CS_OPEN;
    c->rlen  = 0;
    fprintf(stderr, "ws: client %d handshake OK\n", c->fd);
}

/* ── WebSocket frame send ────────────────────────────────────────────────── */

void ws_send(int fd, const char *json) {
    if (fd < 0 || !json) return;
    size_t payload_len = strlen(json);
    uint8_t header[10];
    int hlen = 0;

    header[hlen++] = 0x81;  /* FIN + text opcode */

    if (payload_len < 126) {
        header[hlen++] = (uint8_t)payload_len;
    } else if (payload_len < 65536) {
        header[hlen++] = 126;
        header[hlen++] = (payload_len >> 8) & 0xff;
        header[hlen++] = payload_len & 0xff;
    } else {
        header[hlen++] = 127;
        for (int i = 7; i >= 0; i--)
            header[hlen++] = (payload_len >> (i*8)) & 0xff;
    }

    struct iovec iov[2] = {
        { .iov_base = header,       .iov_len = hlen },
        { .iov_base = (void *)json, .iov_len = payload_len },
    };
    writev(fd, iov, 2);
}

void ws_broadcast(const char *json) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (g_clients[i].fd >= 0 && g_clients[i].state == CS_OPEN)
            ws_send(g_clients[i].fd, json);
    }
}

/* ── WebSocket frame receive ─────────────────────────────────────────────── */

static void process_frames(WsClient *c) {
    uint8_t *buf = (uint8_t *)c->rbuf;
    int      len = c->rlen;

    while (len >= 2) {
        /* uint8_t fin  = (buf[0] >> 7) & 1; */
        uint8_t opcode = buf[0] & 0x0f;
        int     masked = (buf[1] >> 7) & 1;
        uint64_t plen  = buf[1] & 0x7f;
        int      hlen  = 2;

        if (plen == 126) {
            if (len < 4) break;
            plen = ((uint64_t)buf[2] << 8) | buf[3];
            hlen = 4;
        } else if (plen == 127) {
            if (len < 10) break;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | buf[2+i];
            hlen = 10;
        }

        if (masked) hlen += 4;
        uint64_t total = hlen + plen;
        if ((uint64_t)len < total) break;

        if (opcode == 0x8) {
            /* Close frame */
            close_client(c);
            return;
        } else if (opcode == 0x1 || opcode == 0x2) {
            /* Text / binary frame */
            uint8_t payload[4096];
            uint64_t copy = plen < sizeof(payload)-1 ? plen : sizeof(payload)-1;

            if (masked) {
                uint8_t mask[4];
                memcpy(mask, buf + hlen - 4, 4);
                for (uint64_t i = 0; i < copy; i++)
                    payload[i] = buf[hlen + i] ^ mask[i & 3];
            } else {
                memcpy(payload, buf + hlen, copy);
            }
            payload[copy] = '\0';
            if (g_handler) g_handler((char *)payload);
        }
        /* Shift buffer */
        memmove(buf, buf + total, len - total);
        len -= (int)total;
    }
    c->rlen = len;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int ws_init(uint16_t port, WsMsgHandler handler) {
    g_handler = handler;

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        g_clients[i].fd    = -1;
        g_clients[i].state = CS_CLOSED;
        g_clients[i].rlen  = 0;
    }

    g_listen_fd = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_listen_fd < 0) {
        /* Fallback to IPv4 */
        g_listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (g_listen_fd < 0) { perror("ws: socket"); return -1; }

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port   = htons(port),
            .sin_addr   = { .s_addr = INADDR_ANY },
        };
        int one = 1;
        setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("ws: bind"); close(g_listen_fd); return -1;
        }
    } else {
        /* IPv6 dual-stack */
        int zero = 0, one = 1;
        setsockopt(g_listen_fd, IPPROTO_IPV6, IPV6_V6ONLY,  &zero, sizeof(zero));
        setsockopt(g_listen_fd, SOL_SOCKET,   SO_REUSEADDR,  &one,  sizeof(one));
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6,
            .sin6_port   = htons(port),
            .sin6_addr   = in6addr_any,
        };
        if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("ws: bind6"); close(g_listen_fd); return -1;
        }
    }

    set_nonblock(g_listen_fd);
    listen(g_listen_fd, 8);
    fprintf(stderr, "ws: listening on port %d\n", port);
    return 0;
}

int ws_listen_fd(void) { return g_listen_fd; }

int ws_accept(void) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept4(g_listen_fd, (struct sockaddr *)&addr, &addrlen, SOCK_CLOEXEC);
    if (fd < 0) return -1;

    WsClient *c = find_free();
    if (!c) { close(fd); return -1; }

    set_nonblock(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    c->fd    = fd;
    c->state = CS_HANDSHAKE;
    c->rlen  = 0;
    fprintf(stderr, "ws: new client fd=%d\n", fd);
    return fd;
}

int ws_client_read(int fd) {
    WsClient *c = find_client(fd);
    if (!c) return -1;

    int space = (int)sizeof(c->rbuf) - c->rlen - 1;
    if (space <= 0) { close_client(c); return -1; }

    ssize_t n = recv(fd, c->rbuf + c->rlen, space, 0);
    if (n <= 0) { close_client(c); return -1; }
    c->rlen += n;
    c->rbuf[c->rlen] = '\0';

    if (c->state == CS_HANDSHAKE) {
        /* Look for end of HTTP headers */
        if (strstr(c->rbuf, "\r\n\r\n")) do_handshake(c);
    } else if (c->state == CS_OPEN) {
        process_frames(c);
        if (c->state == CS_CLOSED) return -1;
    }
    return 0;
}

int ws_client_fds(int *out, int max) {
    int n = 0;
    for (int i = 0; i < WS_MAX_CLIENTS && n < max; i++)
        if (g_clients[i].fd >= 0) out[n++] = g_clients[i].fd;
    return n;
}

void ws_destroy(void) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        close_client(&g_clients[i]);
    if (g_listen_fd >= 0) { close(g_listen_fd); g_listen_fd = -1; }
}
