# Base: official VS Code C++ dev container (Ubuntu 24.04 / noble).
# Ships with gcc, g++, cmake, git, ninja, gdb, and a non-root `vscode` user.
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Extra apt packages needed by this project:
#   - sqlite3            : invoked at build time to produce reference.sqlite
#   - autoconf/automake  : required by several vcpkg ports
#   - autoconf-archive   : required by icu / fontconfig ports (transitive)
#   - libtool            : needed by some vcpkg ports
#   - pkg-config         : used by vcpkg + cmake find_package fallbacks
#   - zip/unzip/tar/curl : vcpkg download/extract deps
#   - bison/flex/gperf   : occasionally needed by transitive vcpkg builds
#   - python3            : used by a handful of vcpkg port scripts
#   - nasm/yasm          : some multimedia deps need these (safe to include)
#   - libx11-dev + friends: ftxui itself is header-only + termcap, but keep
#                            xkb/xcb around in case future TUI deps need them
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      sqlite3 \
      autoconf \
      automake \
      autoconf-archive \
      libtool \
      pkg-config \
      zip \
      unzip \
      tar \
      curl \
      ca-certificates \
      bison \
      flex \
      gperf \
      python3 \
      nasm \
      yasm \
      ninja-build \
      libncurses-dev \
    && rm -rf /var/lib/apt/lists/*
