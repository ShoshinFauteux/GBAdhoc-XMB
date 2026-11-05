# The Windows build of GBAdhoc XMB (desktop/build.sh): mingw-w64,
# plus python3 for embedding the UI and drawing the app icon at build time.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 \
        make python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
