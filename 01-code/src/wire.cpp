#include "wire.h"

#include "didar.h"

namespace didar {

static_assert(DIDAR_E_BASE + int(ErrorCode::TargetUnreachable) == DIDAR_E_TARGET_UNREACHABLE);
static_assert(DIDAR_E_BASE + int(ErrorCode::FlowTableFull) == DIDAR_E_FLOW_TABLE_FULL);
static_assert(DIDAR_E_BASE + int(ErrorCode::BadVersion) == DIDAR_E_BAD_VERSION);
static_assert(DIDAR_E_BASE + int(ErrorCode::Malformed) == DIDAR_E_MALFORMED);
static_assert(DIDAR_E_BASE + int(ErrorCode::PortNotAllowed) == DIDAR_E_PORT_NOT_ALLOWED);

void write_header(uint8_t* out, Type type, uint16_t port) {
    out[0] = kVersion << 4;
    out[1] = uint8_t(type);
    out[2] = uint8_t(port >> 8);
    out[3] = uint8_t(port);
}

ErrorCode read_header(const uint8_t* in, size_t len, Header* out) {
    *out = {};
    if (len < kHeaderSize) return ErrorCode::Malformed;
    out->version = in[0] >> 4;
    out->flags = in[0] & 0x0f;
    out->type = Type(in[1]);
    out->port = uint16_t(in[2] << 8 | in[3]);
    if (out->version != kVersion) return ErrorCode::BadVersion;
    if (out->flags != 0) return ErrorCode::Malformed;
    if (out->type == Type::Data) return ErrorCode::None;
    if (out->type == Type::Error && len == kErrorSize) return ErrorCode::None;
    return ErrorCode::Malformed;
}

}
