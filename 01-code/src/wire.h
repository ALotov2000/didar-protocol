#pragma once

#include <stddef.h>
#include <stdint.h>

namespace didar {

constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 4;
constexpr size_t kErrorSize = kHeaderSize + 1;
constexpr size_t kMaxPayload = 1468;
constexpr size_t kMaxDatagram = kHeaderSize + kMaxPayload;
constexpr size_t kMaxUdpPayload = 65507;

enum class Type : uint8_t { Data = 0, Error = 1 };

enum class ErrorCode : uint8_t {
    None = 0,
    TargetUnreachable = 1,
    FlowTableFull = 2,
    BadVersion = 3,
    Malformed = 4,
    PortNotAllowed = 5,
};

struct Header {
    uint8_t version;
    uint8_t flags;
    Type type;
    uint16_t port;
};

void write_header(uint8_t* out, Type type, uint16_t port);
ErrorCode read_header(const uint8_t* in, size_t len, Header* out);

}
