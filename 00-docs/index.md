# Didar Documentation

Didar is an application networking protocol on top of UDP. Clients send all datagrams to a single didar port, and didar demultiplexes them to their target UDP ports. Optionally, traffic is encrypted after an initial handshake.

## Table of Contents

1. [Glossary](glossary.md) — terms used across the docs.
2. [Requirements](requirements.md) — what didar must do.
   1. [Functional Requirements](requirements.md#functional-requirements)
   2. [Security Requirements](requirements.md#security-requirements)
   3. [Performance Requirements](requirements.md#performance-requirements)
3. [Prior Art](prior-art.md) — existing projects and standards compared with the requirements.
