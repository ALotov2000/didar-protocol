#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

namespace didar {

template <typename V>
struct FlowTable {
    struct Slot {
        uint64_t key;
        uint64_t last_seen;
        V value;
        bool used;
    };
    Slot* slots;
    uint32_t mask;
    uint32_t count;
    uint32_t max_flows;
};

inline uint64_t monotonic_sec() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return uint64_t(now.tv_sec);
}

/** Packs a peer address and a 16-bit tag (target port or map index) into a key. Peer IP 0 marks a listening socket. */
inline uint64_t flow_key(const sockaddr_in& peer, uint16_t tag) {
    return uint64_t(ntohl(peer.sin_addr.s_addr)) << 32 | uint64_t(ntohs(peer.sin_port)) << 16 | tag;
}

inline bool is_listener_key(uint64_t key) {
    return key >> 32 == 0;
}

inline sockaddr_in flow_peer(uint64_t key) {
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(uint32_t(key >> 32));
    peer.sin_port = htons(uint16_t(key >> 16));
    return peer;
}

inline uint16_t flow_tag(uint64_t key) {
    return uint16_t(key);
}

inline uint32_t flow_hash(uint64_t key) {
    return uint32_t((key * 0x9E3779B97F4A7C15ull) >> 32);
}

/** Allocates room for max_flows at a load factor of at most 0.5. */
template <typename V>
bool flow_table_init(FlowTable<V>* t, uint32_t max_flows) {
    uint32_t capacity = 2;
    while (capacity < max_flows * 2) capacity <<= 1;
    t->slots = static_cast<typename FlowTable<V>::Slot*>(calloc(capacity, sizeof *t->slots));
    t->mask = capacity - 1;
    t->count = 0;
    t->max_flows = max_flows;
    return t->slots != nullptr;
}

template <typename V>
typename FlowTable<V>::Slot* flow_find(FlowTable<V>* t, uint64_t key) {
    for (uint32_t i = flow_hash(key) & t->mask;; i = (i + 1) & t->mask) {
        auto* slot = &t->slots[i];
        if (!slot->used) return nullptr;
        if (slot->key == key) return slot;
    }
}

/** Claims a slot for a key that is not present. Returns nullptr when max_flows is reached. */
template <typename V>
typename FlowTable<V>::Slot* flow_insert(FlowTable<V>* t, uint64_t key) {
    if (t->count == t->max_flows) return nullptr;
    uint32_t i = flow_hash(key) & t->mask;
    while (t->slots[i].used) i = (i + 1) & t->mask;
    auto* slot = &t->slots[i];
    slot->used = true;
    slot->key = key;
    t->count++;
    return slot;
}

/** Removes a slot with backward-shift deletion, so probe chains stay intact without tombstones. */
template <typename V>
void flow_erase(FlowTable<V>* t, typename FlowTable<V>::Slot* slot) {
    uint32_t hole = uint32_t(slot - t->slots);
    for (uint32_t i = (hole + 1) & t->mask; t->slots[i].used; i = (i + 1) & t->mask) {
        uint32_t home = flow_hash(t->slots[i].key) & t->mask;
        if (((i - home) & t->mask) >= ((i - hole) & t->mask)) {
            t->slots[hole] = t->slots[i];
            hole = i;
        }
    }
    t->slots[hole].used = false;
    t->count--;
}

// ponytail: scans the whole table once per second; keep a timer wheel if max_flows gets into the millions.
template <typename V, typename OnClose>
void flow_close_idle(FlowTable<V>* t, uint64_t now, uint64_t idle_sec, OnClose on_close) {
    uint32_t i = 0;
    while (i <= t->mask) {
        auto* slot = &t->slots[i];
        if (slot->used && now - slot->last_seen >= idle_sec) {
            on_close(slot->value);
            flow_erase(t, slot);
        } else {
            i++;
        }
    }
}

}
