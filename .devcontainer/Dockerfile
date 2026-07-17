# tcbのビルド・実機デバッグ用。ホストのCPUアーキテクチャ(Intel/ARM)や
# OS(macOS/Windows)に関わらず、常に本物のUbuntu(glibc・実tty)上で
# 動かすことが目的。ubuntu:26.04がまだDocker Hubに無い場合は下の行を
# ubuntu:24.04 (直近のLTS) に変更して使う。
FROM ubuntu:26.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libncurses-dev \
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
