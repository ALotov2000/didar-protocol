# Didar Documentation

Didar is an application networking protocol on top of UDP. Clients send all datagrams to a single didar port, and didar demultiplexes them to their target UDP ports. Optionally, traffic is encrypted after an initial handshake.

## Table of Contents

1. [Guide](guide.md) — how to build didar and use it.
2. [Glossary](glossary.md) — terms used across the docs.
3. [Requirements](requirements.md) — what didar must do.
   1. [Functional Requirements](requirements.md#functional-requirements)
   2. [Security Requirements](requirements.md#security-requirements)
   3. [Performance Requirements](requirements.md#performance-requirements)
4. [Prior Art](prior-art.md) — existing projects and standards compared with the requirements.
5. [Architecture](architecture.md) — components, wire format, internals, and entity relationships.
   1. [Wire Format](architecture.md#wire-format)
   2. [Entity Relationships](architecture.md#entity-relationships)
6. [API](api.md) — how to set up and use each side.
   1. [Client](api.md#client)
   2. [Server](api.md#server)
