# didar-protocol

Didar is a small application protocol on top of UDP that puts many UDP services behind **one** UDP port.

Clients send every datagram to a single didar port on the server. Each datagram carries a 4-byte header naming its target port, and the server forwards the payload to that service on `127.0.0.1`. Replies come back the same way. In effect, didar is a reverse proxy for UDP.

```text
client                                    server
  app ──► didar-agent ─┐                ┌──► 127.0.0.1:7000  (game server)
                       ├── UDP :9000 ──►│
  your program ────────┘   (didard)     └──► 127.0.0.1:7001  (voice, metrics, ...)
  (libdidar)
```

## Why use it

- **One port to open.** Expose a single UDP port in the firewall, cloud security group, or NAT, instead of one per service. Adding a service doesn't need a new firewall rule.
- **Services stay private.** Services only listen on `127.0.0.1`. Clients can reach only the ports on the server's allow-list.
- **Works with existing apps.** `didar-agent` maps local ports to remote services, so an app that speaks plain UDP works by pointing it at `127.0.0.1:<port>`, with no code change.
- **Tiny overhead.** 4 bytes per datagram, and under 50 µs of added latency at p99 (measured: about 33 µs over loopback).
- **Plain UDP semantics.** One datagram in is one datagram out. No connection setup, no retransmission, no reordering: what your app already expects from UDP.
- **Clear errors.** When the server can't forward a datagram, it says why (port not allowed, nothing listening, flow table full) instead of silently dropping it.

A typical fit is a host running several UDP services (game servers, VoIP, DNS, telemetry) behind a firewall or NAT where opening many ports is impractical.

There are similar tools, but none covers this combination (see [Prior Art](00-docs/prior-art.md)).

## Quick start

Requirements: Linux, `g++` with C++20, and CMake 3.20 or newer.

```bash
cmake -S 01-code -B 01-code/build -DCMAKE_BUILD_TYPE=Release
cmake --build 01-code/build

# On the server: forward didar port 9000 to the service on 127.0.0.1:7000
./01-code/build/didard --port 9000 --allow 7000

# On the client: apps send to 127.0.0.1:5000 to reach the server's port 7000
./01-code/build/didar-agent --server <server-ip>:9000 --map 5000:7000
```

To use didar from your own C or C++ program, include `01-code/include/didar.h` and link `libdidar.a`. Docker images are available too. See the [Guide](00-docs/guide.md) for both.

## Status

| Phase | Scope | State |
| ----- | ----- | ----- |
| 1 | Single-port demultiplexing, server allow-list, error reporting, client library and agent | Done |
| 2 | Encryption after a handshake (can be disabled) | Planned |

Phase 1 traffic is **not encrypted**. Until phase 2, don't send sensitive data over untrusted networks without another layer of protection.

## Documentation

- [Guide](00-docs/guide.md): build, run, and use didar.
- [Requirements](00-docs/requirements.md): what didar must do.
- [Architecture](00-docs/architecture.md): components, wire format, and internals.
- [API](00-docs/api.md): the client library, `didar-agent`, and `didard` options.
- [All docs](00-docs/index.md)
