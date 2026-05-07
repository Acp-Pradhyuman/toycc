# Multi-stage: build toycc, then a slim runtime image with nasm + binutils
# + firejail + python/flask.
#
# Target x86_64 only (toycc emits x86_64 syscalls). HuggingFace Spaces'
# default runtime is x86_64 which matches.

# -------- builder --------
FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
      gcc make libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . /src
RUN make clean && make

# -------- runtime --------
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      nasm \
      binutils \
      firejail \
      python3 \
      python3-pip \
      python3-flask \
    && rm -rf /var/lib/apt/lists/*

# Non-root user for the server process. firejail needs the binary to be
# setuid root (it is, via the apt package), but the server itself runs
# unprivileged.
RUN useradd --create-home --shell /bin/bash runner
WORKDIR /app

# Copy the built compiler.
COPY --from=builder /src/build/main /app/build/main
RUN chmod +x /app/build/main

# Copy the web frontend + server.
COPY web/server.py /app/server.py
COPY web/static    /app/static

# HuggingFace Spaces expects port 7860 by default. Render/Fly set $PORT.
ENV PORT=7860
EXPOSE 7860

# firejail requires /dev/shm to exist and be writable by runner.
RUN mkdir -p /home/runner && chown -R runner:runner /home/runner

USER runner
CMD ["python3", "/app/server.py"]
