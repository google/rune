FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true \
    LC_ALL=C.UTF-8 LANG=C.UTF-8 LANGUAGE=C.UTF-8

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    # "aclocal" is needed by "autogen.sh"
    automake \
    autotools-dev \
    bison \
    build-essential \
    ca-certificates \
    clang \
    flex \
    git \
    libgmp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp

# Build and install DataDraw
RUN git clone --depth 1 https://github.com/waywardgeek/datadraw.git \
    && cd datadraw \
    && ./autogen.sh \
    && ./configure \
    && make \
    && make install

# Fetch CTTK.  The Rune Makefile expects it in ../CTTK relative to the rune
# source directory.
RUN git clone --depth 1 https://github.com/pornin/CTTK.git \
    && cp CTTK/inc/cttk.h CTTK

# Build, test, and install Rune from this checkout.  Run `docker build .` from
# the root of the repository.
COPY . /tmp/rune
RUN cd rune \
    && make \
    # tests/getenv_test.rn expects USER to be set, which it is not in Docker.
    && USER=root ./runtests.sh \
    # Will install under /usr/local
    && make install

# Test that rune is working
RUN echo "TESTING RUNE" \
    && echo 'println "Hello, World!"' > hello.rn \
    && rune -g hello.rn \
    && ./hello \
    && echo "Should have printed 'Hello, World!'"
