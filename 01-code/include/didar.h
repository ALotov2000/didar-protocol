#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct didar_client didar_client;

enum {
    DIDAR_E_BASE = 1000,
    DIDAR_E_TARGET_UNREACHABLE = 1001,
    DIDAR_E_FLOW_TABLE_FULL = 1002,
    DIDAR_E_BAD_VERSION = 1003,
    DIDAR_E_MALFORMED = 1004,
    DIDAR_E_PORT_NOT_ALLOWED = 1005,
};

/** Opens a non-blocking datagram socket to a didard server. Returns NULL with errno set on failure. */
didar_client* didar_open(const char* server_ip, uint16_t didar_port);

/** Sends one datagram to target_port. Returns len, -EMSGSIZE above 1468 bytes, or -errno. */
int didar_send(didar_client* c, uint16_t target_port, const void* buf, size_t len);

/** Receives one datagram and sets source_port. Returns the payload length, -EAGAIN, -errno, or -DIDAR_E_*. */
int didar_recv(didar_client* c, uint16_t* source_port, void* buf, size_t cap);

/** Returns the socket for the caller's epoll or poll loop. */
int didar_fd(didar_client* c);

/** Closes the socket and frees the handle. */
void didar_close(didar_client* c);

/** Describes a negative return value: -errno or -DIDAR_E_*. */
const char* didar_strerror(int rc);

#ifdef __cplusplus
}
#endif
