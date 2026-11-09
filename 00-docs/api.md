# API

How to set up and use each side of didar. See [Architecture](architecture.md) for the internals.

## Client

There are two ways to use didar on the client: link `libdidar` into the app, or run `didar-agent` so unmodified apps can use it.

### libdidar

A C-callable (`extern "C"`) library. One handle talks to one server; the target port is given per call. Sockets are non-blocking datagram sockets, and nothing is allocated after `didar_open`.

```cpp
didar_client* didar_open(const char* server_ip, uint16_t didar_port);
int           didar_send(didar_client* c, uint16_t target_port, const void* buf, size_t len);
int           didar_recv(didar_client* c, uint16_t* source_port, void* buf, size_t cap);
int           didar_fd(didar_client* c);
void          didar_close(didar_client* c);
const char*   didar_strerror(int rc);
```

| Function | Returns | Description |
| -------- | ------- | ----------- |
| `didar_open` | Handle, or `NULL` with `errno` set | Creates a datagram socket for the given server. |
| `didar_send` | Payload bytes sent, or a negative code | Adds the didar header and sends one datagram to `target_port`. `-EMSGSIZE` if the payload is over 1468 bytes. |
| `didar_recv` | Payload bytes received, or a negative code | Reads one datagram and sets `source_port`. `-EAGAIN` when nothing is waiting. A server error is returned as `-DIDAR_E_*`. |
| `didar_fd` | File descriptor | For the app's own `epoll` or `poll` loop. |
| `didar_close` | Nothing | Closes the socket and frees the handle. |
| `didar_strerror` | Message | Describes both `-errno` and `-DIDAR_E_*` codes. |

Didar error codes start at 1000 so they never collide with `errno`: `DIDAR_E_TARGET_UNREACHABLE` is 1001, up to `DIDAR_E_PORT_NOT_ALLOWED` at 1005, matching the wire codes in [Architecture](architecture.md#error-datagram-5-bytes).

```mermaid
sequenceDiagram
    participant App
    participant L as libdidar
    participant D as didard
    App->>L: didar_open(server_ip, didar_port)
    L-->>App: handle
    App->>L: didar_fd(handle)
    L-->>App: fd, added to the app's epoll
    App->>L: didar_send(handle, 7000, buf, len)
    L->>D: header(DATA, 7000) + payload
    D-->>L: header(DATA, 7000) + reply
    Note over App,L: epoll reports fd readable
    App->>L: didar_recv(handle, &port, buf, cap)
    L-->>App: len, port = 7000
    D-->>L: header(ERROR, 7001) + code 5
    App->>L: didar_recv(handle, &port, buf, cap)
    L-->>App: -DIDAR_E_PORT_NOT_ALLOWED, port = 7001
    App->>L: didar_strerror(rc)
    L-->>App: "The target port is not open for didar on this server."
    App->>L: didar_close(handle)
```

### didar-agent

Maps local ports to target ports. It listens only on `127.0.0.1`.

| Config key | Flag | Default | Description |
| ---------- | ---- | ------- | ----------- |
| — | `-c`, `--config <file>` | None | Config file to read. |
| `server` | `--server <ip:port>` | Required | The didard server and didar port. |
| `map` | `--map <local:target>` | Required | Local port to target port. Repeat for each mapping. |
| `max_flows` | `--max-flows <n>` | 4096 | Size of the flow table. |
| `idle_sec` | `--idle-sec <n>` | 60 | Idle seconds before a flow is closed. |

Flags override the config file.

```ini
# didar-agent.conf
server    = 203.0.113.5:9000
map       = 5000:7000
map       = 5001:7001
max_flows = 4096
idle_sec  = 60
```

Run it with `didar-agent -c didar-agent.conf`.

#### Setup

```mermaid
sequenceDiagram
    participant Op as Operator
    participant Ag as didar-agent
    participant K as Kernel
    Op->>Ag: didar-agent -c didar-agent.conf --map 5002:7002
    Ag->>Ag: read config file, apply flags
    Ag->>Ag: allocate flow table (max_flows)
    loop each map entry
        Ag->>K: socket(SOCK_DGRAM), bind 127.0.0.1:local_port
        Ag->>K: add listening fd to epoll
    end
    Ag-->>Op: ready
```

#### Forwarding

```mermaid
sequenceDiagram
    participant App as Unmodified app
    participant Ag as didar-agent
    participant D as didard
    App->>Ag: datagram to 127.0.0.1:5000
    Ag->>Ag: look up flow (app address, 5000)
    alt new flow
        Ag->>Ag: didar_open(server), add didar_fd to epoll
    end
    Ag->>D: didar_send(handle, 7000, payload)
    D-->>Ag: reply from target port 7000
    Ag->>Ag: didar_recv, map handle to app address
    Ag-->>App: reply from 127.0.0.1:5000
    D-->>Ag: ERROR code
    Ag->>Ag: log didar_strerror(rc), drop
    Note over Ag: flows idle longer than idle_sec are closed
```

## Server

### didard

Listens on the didar port and forwards to allowed target ports on `127.0.0.1`.

| Config key | Flag | Default | Description |
| ---------- | ---- | ------- | ----------- |
| — | `-c`, `--config <file>` | None | Config file to read. |
| `port` | `--port <n>` | Required | The didar port. |
| `allow` | `--allow <p1,p2,...>` | Required | Target ports clients may reach. Anything else gets `PORT_NOT_ALLOWED`. |
| `max_flows` | `--max-flows <n>` | 65536 | Size of the flow table per thread. |
| `idle_sec` | `--idle-sec <n>` | 60 | Idle seconds before a flow is closed. |

Flags override the config file.

```ini
# didard.conf
port      = 9000
allow     = 7000,7001
max_flows = 65536
idle_sec  = 60
```

Run it with `didard -c didard.conf`.

#### Setup

```mermaid
sequenceDiagram
    participant Op as Operator
    participant D as didard
    participant K as Kernel
    Op->>D: didard -c didard.conf --port 9100
    D->>D: read config file, apply flags
    loop one thread per core
        D->>D: allocate flow table (max_flows)
        D->>K: socket(SOCK_DGRAM), SO_REUSEPORT, bind 0.0.0.0:port
        D->>K: add fd to the thread's epoll
    end
    D-->>Op: ready
```

#### Forwarding

```mermaid
sequenceDiagram
    participant C as Client
    participant D as didard
    participant T as Handler on 127.0.0.1:7000
    C->>D: header(DATA, 7000) + payload
    D->>D: check version and header
    alt port not in allow
        D-->>C: header(ERROR, 7000) + PORT_NOT_ALLOWED
    else allowed
        D->>D: look up flow (client ip, client port, 7000)
        alt new flow
            D->>D: proxy socket(SOCK_DGRAM), connect 127.0.0.1:7000
        end
        D->>T: payload
        T-->>D: reply on the proxy socket
        D-->>C: header(DATA, 7000) + reply
    end
    Note over D: ECONNREFUSED on a proxy socket closes the flow and sends TARGET_UNREACHABLE
    Note over D: flows idle longer than idle_sec are closed
```
