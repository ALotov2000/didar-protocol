# Two images from one file:
#   docker build --target didard      -t didard 01-code
#   docker build --target didar-agent -t didar-agent 01-code
#
# didard forwards only to 127.0.0.1, and didar-agent listens only on 127.0.0.1 (SR-4).
# So each container must share the network namespace of what it talks to:
# --network host, or --network container:<service> for a sidecar.
# On Docker Desktop, "host" is the Docker VM, not your machine: use the sidecar form there.

FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" \
 && ctest --test-dir build -L requirement --output-on-failure

FROM ubuntu:24.04 AS runtime
USER 65534:65534

FROM runtime AS didard
COPY --from=build /src/build/didard /usr/local/bin/didard
# UDP port clients send every datagram to (--port).
ENV DIDARD_PORT=9000
# Required. Comma-separated target ports on 127.0.0.1 that clients may reach (--allow), e.g. 7000,7001.
ENV DIDARD_ALLOW=
# Size of the flow table per thread (--max-flows).
ENV DIDARD_MAX_FLOWS=65536
# Seconds without traffic before a flow is closed (--idle-sec).
ENV DIDARD_IDLE_SEC=60
EXPOSE 9000/udp
CMD ["sh", "-c", "exec didard --port \"$DIDARD_PORT\" --allow \"${DIDARD_ALLOW:?set DIDARD_ALLOW, e.g. 7000,7001}\" --max-flows \"$DIDARD_MAX_FLOWS\" --idle-sec \"$DIDARD_IDLE_SEC\""]

FROM runtime AS didar-agent
COPY --from=build /src/build/didar-agent /usr/local/bin/didar-agent
# Required. The didard server as ip:port (--server), e.g. 203.0.113.5:9000.
ENV DIDAR_AGENT_SERVER=
# Required. Space-separated local:target port pairs (one --map each), e.g. "5000:7000 5001:7001".
ENV DIDAR_AGENT_MAP=
# Size of the flow table (--max-flows).
ENV DIDAR_AGENT_MAX_FLOWS=4096
# Seconds without traffic before a flow is closed (--idle-sec).
ENV DIDAR_AGENT_IDLE_SEC=60
CMD ["sh", "-c", "set --; for m in ${DIDAR_AGENT_MAP:?set DIDAR_AGENT_MAP, e.g. 5000:7000}; do set -- \"$@\" --map \"$m\"; done; exec didar-agent --server \"${DIDAR_AGENT_SERVER:?set DIDAR_AGENT_SERVER, e.g. 203.0.113.5:9000}\" --max-flows \"$DIDAR_AGENT_MAX_FLOWS\" --idle-sec \"$DIDAR_AGENT_IDLE_SEC\" \"$@\""]
