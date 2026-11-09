# Glossary

| Term | Definition |
| ---- | ---------- |
| Client | The peer that sends datagrams to the didar port and, when encryption is enabled, starts the handshake. |
| Datagram | A single UDP packet. |
| Demultiplexing | Delivering each datagram received on the didar port to the target port it is addressed to. |
| Didar | The application networking protocol on top of UDP described by these docs. |
| Didar port | The single UDP port on the server that receives all didar datagrams. |
| Handshake | The initial exchange in which the server sends its public key to the client, before encrypted transmission starts. |
| Public key | The server's key, sent to the client during the handshake and used to establish encrypted transmission. |
| Server | The host running didar on the didar port and the services on the target ports. |
| Target port | The local UDP port on the server that a datagram is finally meant for. |
