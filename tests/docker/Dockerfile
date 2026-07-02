# Headless network-validation image (spec 17 milestone). Builds ONLY the netcheck
# harness (the shipped Windows/macOS app targets are not built here). Two containers
# from this image connect over a real Docker network and forward encrypted input via
# the exact product logic (secure_link -> ConnectionManager -> MeshNode ->
# EncryptedTransport) over a POSIX WebSocket transport.
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make libssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tools ./tools
COPY packaging ./packaging

# Configure with tests off (the app exe is never built -- it needs the Windows/macOS
# tray, which doesn't exist on Linux; only the netcheck target and its libraries are
# built here).
RUN cmake -S . -B build -DSM_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target netcheck -j

# Default is overridden by docker-compose per node.
ENTRYPOINT ["/src/build/netcheck"]
