#include "didar.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wire.h"

struct didar_client {
    int fd;
};

didar_client* didar_open(const char* server_ip, uint16_t didar_port) {
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(didar_port);
    if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1) {
        errno = EINVAL;
        return nullptr;
    }
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return nullptr;
    auto* c = static_cast<didar_client*>(malloc(sizeof(didar_client)));
    if (!c || connect(fd, reinterpret_cast<sockaddr*>(&server), sizeof server) < 0) {
        int saved = c ? errno : ENOMEM;
        free(c);
        close(fd);
        errno = saved;
        return nullptr;
    }
    c->fd = fd;
    return c;
}

int didar_send(didar_client* c, uint16_t target_port, const void* buf, size_t len) {
    if (len > didar::kMaxPayload) return -EMSGSIZE;
    uint8_t header[didar::kHeaderSize];
    didar::write_header(header, didar::Type::Data, target_port);
    iovec parts[2] = {{header, sizeof header}, {const_cast<void*>(buf), len}};
    msghdr msg{};
    msg.msg_iov = parts;
    msg.msg_iovlen = 2;
    if (sendmsg(c->fd, &msg, 0) < 0) return -errno;
    return int(len);
}

int didar_recv(didar_client* c, uint16_t* source_port, void* buf, size_t cap) {
    uint8_t header[didar::kHeaderSize];
    uint8_t spare;
    iovec parts[2] = {{header, sizeof header}, cap ? iovec{buf, cap} : iovec{&spare, 1}};
    msghdr msg{};
    msg.msg_iov = parts;
    msg.msg_iovlen = 2;
    ssize_t n = recvmsg(c->fd, &msg, 0);
    if (n < 0) return -errno;
    didar::Header h;
    if (didar::read_header(header, size_t(n), &h) != didar::ErrorCode::None) return -EPROTO;
    *source_port = h.port;
    if (h.type == didar::Type::Error) return -(DIDAR_E_BASE + *static_cast<uint8_t*>(parts[1].iov_base));
    if (msg.msg_flags & MSG_TRUNC) return -EMSGSIZE;
    return int(n - ssize_t(didar::kHeaderSize));
}

int didar_fd(didar_client* c) {
    return c->fd;
}

void didar_close(didar_client* c) {
    close(c->fd);
    free(c);
}

const char* didar_strerror(int rc) {
    switch (-rc) {
    case DIDAR_E_TARGET_UNREACHABLE: return "Nothing is listening on the target port on the server.";
    case DIDAR_E_FLOW_TABLE_FULL: return "Server has reached its maximum number of flows; try again later.";
    case DIDAR_E_BAD_VERSION: return "Server does not support this protocol version.";
    case DIDAR_E_MALFORMED: return "Server could not parse the datagram header.";
    case DIDAR_E_PORT_NOT_ALLOWED: return "The target port is not open for didar on this server.";
    }
    if (rc < 0 && -rc < DIDAR_E_BASE) return strerror(-rc);
    return "Unknown didar error.";
}
