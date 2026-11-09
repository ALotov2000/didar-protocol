#include "harness.h"

namespace {

bool bound_only_to_loopback(uint16_t port) {
    FILE* table = fopen("/proc/net/udp", "r");
    CHECK(table != nullptr);
    char line[256];
    bool found = false;
    bool only_loopback = true;
    CHECK(fgets(line, sizeof line, table) != nullptr);
    while (fgets(line, sizeof line, table)) {
        unsigned address;
        unsigned local_port;
        if (sscanf(line, " %*d: %8X:%4X", &address, &local_port) != 2 || local_port != port) continue;
        found = true;
        only_loopback = only_loopback && address == 0x0100007F;
    }
    fclose(table);
    return found && only_loopback;
}

void sr4_1_agent_listens_on_loopback_only() {
    uint16_t local = test::free_port();
    test::Process agent = test::start_agent(test::free_port(), local, 7000);
    CHECK(bound_only_to_loopback(local));
    test::stop(agent);
}

void sr4_2_bind_address_cannot_be_changed() {
    char map[16];
    snprintf(map, sizeof map, "%u:7000", test::free_port());
    const char* args[] = {"--server", "127.0.0.1:9", "--map", map, "--bind", "0.0.0.0", nullptr};
    test::Process agent = test::spawn(DIDAR_AGENT, args);
    CHECK(test::exit_code(agent) == 2);
    close(agent.stderr_fd);
}

}

int main() {
    sr4_1_agent_listens_on_loopback_only();
    sr4_2_bind_address_cannot_be_changed();
    puts("SR-4: all scenarios passed");
}
