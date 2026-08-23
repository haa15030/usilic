# tcbのビルド・実機デバッグ用。ホストのCPUアーキテクチャ(Intel/ARM)や
# OS(macOS/Windows)に関わらず、常に本物のUbuntu(glibc・実tty)上で
# 動かすことが目的。ubuntu:26.04がまだDocker Hubに無い場合は下の行を
# ubuntu:24.04 (直近のLTS) に変更して使う。
FROM ubuntu:26.04

# pkg-config/libsdl2-dev/libsdl2-ttf-dev (2026-08-24 addition, docs/
# CHANGES.md item 420): src/cpp/src/graphical_display.cpp (the -g GUI
# Display backend) unconditionally #includes <SDL2/SDL.h>/<SDL2/
# SDL_ttf.h> with no #ifdef guard, and src/cpp/Makefile compiles every
# .cpp under src/ into both `make usilic` and `make test` regardless
# of target -- so even a headless container that will never actually
# open a GUI window still needs these to COMPILE at all, not just to
# run -g mode. Missing before this fix; a real user report tracing
# through docs/BUILD.md's own macOS Homebrew instructions surfaced
# that this container's own package list had never been updated to
# match.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libncurses-dev \
    pkg-config \
    libsdl2-dev \
    libsdl2-ttf-dev \
    git \
    curl \
    ca-certificates \
    sudo \
    locales \
    less \
    man-db \
    gdb \
    strace \
    && locale-gen ja_JP.UTF-8 \
    && rm -rf /var/lib/apt/lists/*

# Claude Code CLI (Node.js経由)
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && npm install -g @anthropic-ai/claude-code \
    && rm -rf /var/lib/apt/lists/*

ARG USERNAME=dev
RUN useradd -m -s /bin/bash "$USERNAME" \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/"$USERNAME" \
    && chmod 0440 /etc/sudoers.d/"$USERNAME"

ENV LANG=ja_JP.UTF-8
ENV LC_ALL=ja_JP.UTF-8

USER $USERNAME
WORKDIR /workspace
