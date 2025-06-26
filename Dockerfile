# Dockerfile for building and testing WebKit BiDi tests
FROM debian:bullseye

# Base setup
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    git curl gnupg python3 python3-pip \
    xvfb sudo bash ca-certificates gnupg \
    build-essential cmake ninja-build \
    libglib2.0-dev libgles2-mesa-dev \
    libxcomposite-dev libxdamage-dev \
    libxrandr-dev libgbm-dev libgtk-3-dev \
    libjpeg-dev libpng-dev libwebp-dev \
    libxtst-dev libx11-dev libegl1-mesa-dev \
    libnss3-dev libdrm-dev libwayland-dev \
    libxkbcommon-dev libwoff1 \
    && rm -rf /var/lib/apt/lists/*

# Add a non-root user (for security in GH runners)
RUN useradd -m -s /bin/bash webkit
USER webkit
WORKDIR /home/webkit

# Clone WebKit (or mount volume)
RUN git clone https://github.com/WebKit/WebKit.git

WORKDIR /home/webkit/WebKit

# Set up script entrypoint
CMD bash
