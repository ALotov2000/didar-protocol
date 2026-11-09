# Glossary

| Term | Definition |
| ---- | ---------- |
| Allow-list | The list of target ports didard may forward to. Every other port is rejected. |
| Client | The peer that sends datagrams to the didar port and, when encryption is enabled, starts the handshake. |
| Datagram | A single UDP packet. |
| Demultiplexing | Delivering each datagram received on the didar port to the target port it is addressed to. |
| Didar | The application networking protocol on top of UDP described by these docs. |
| Didar port | The single UDP port on the server that receives all didar datagrams. |
| didar-agent | The client program that maps local UDP ports to target ports, so unmodified apps can use didar. Listens only on `127.0.0.1`. |
| didard | The didar server program. Listens on the didar port and proxies each flow to its target port. |
| Error code | A 1-byte number in an error datagram that tells the client why the server could not forward a datagram. |
| Flow | One conversation through didar. On the server: a client address and port plus a target port. On the agent: an app address plus a local port. |
| Flow table | The fixed-size table, allocated at startup, that holds all active flows. |
| Handshake | The initial exchange in which the server sends its public key to the client, before encrypted transmission starts. |
| libdidar | The didar library: the wire format and the client API. |
| Proxy socket | The datagram socket didard opens for each flow, connected to `127.0.0.1:<target port>`. |
| Public key | The server's key, sent to the client during the handshake and used to establish encrypted transmission. |
| Server | The host running didar on the didar port and the services on the target ports. |
| Target port | The local UDP port on the server that a datagram is finally meant for. |
