# Prior Art

Existing projects and standards checked against the [requirements](requirements.md) on 2026-11-09. None of them meets all requirements, so didar is built from scratch. Licenses and maintenance status were not checked.

## Projects

| Project | FR-1 single-port demultiplexing | FR-2/FR-3 encryption | FR-4 optional encryption | PR-1 size overhead |
| ------- | ------------------------------- | -------------------- | ------------------------ | ------------------ |
| [mwgp](https://github.com/apernet/mwgp) | Partly: one port, but routes by WireGuard peer, not per datagram | WireGuard | No | 32 bytes (WireGuard) |
| [rpxy-l4](https://github.com/junkurihara/rust-rpxy-l4/wiki) | Partly: one port, but routes by protocol type (QUIC, WireGuard) | Passes it through | Not applicable | None added |
| [SPP](https://github.com/esrrhs/spp/wiki) | Partly: forwarding rules multiplexed over one tunnel | HMAC handshake and AEAD | Yes | Not documented |
| [tiny-encrypted-udp-tunnel](https://github.com/wangyu-/tiny-encrypted-udp-tunnel) | No: one port to one destination | Password-based key | Yes | Small, no port header |
| [Arian8j2/forwarder](https://github.com/Arian8j2/forwarder) | No | XOR only (weak) | Yes | Tiny |
| [jsparber/udp-forwarder](https://github.com/jsparber/udp-forwarder) | No: appends 1 byte per datagram, not a port | No | Not applicable | 1 byte |
| [pion/ice UDPMux](https://www.mintlify.com/pion/ice/api/udp-mux) | Partly: one port for WebRTC ICE connections only | Not applicable | Not applicable | Not applicable |

## Standards

| Standard | FR-1 single-port demultiplexing | FR-2/FR-3 encryption | FR-4 optional encryption | PR-1 size overhead |
| -------- | ------------------------------- | -------------------- | ------------------------ | ------------------ |
| SOCKS5 UDP ASSOCIATE ([RFC 1928](https://www.rfc-editor.org/rfc/rfc1928)) | Yes: per-datagram header names destination address and port | No | Not applicable | About 10 bytes for IPv4, above the 4-byte limit |
| MASQUE CONNECT-UDP ([RFC 9298](https://www.rfc-editor.org/rfc/rfc9298)) | Yes: UDP to a target host and port through HTTP/3 | Yes, with certificate-based server authentication | No | Above the 32-byte limit |

## Takeaways

- No existing design combines a target-port header of at most 4 bytes with optional encryption.
- tiny-encrypted-udp-tunnel is a reference for optional keys.
- WireGuard's 32-byte encrypted framing is a reference for PR-1.
- MASQUE shows one way to authenticate the server's key, which FR-2 still needs.
