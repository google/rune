FROM ubuntu:23.04

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
    clang-14 \
    flex \
    git \
    libgmp-dev \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-14 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-14 100

WORKDIR /tmp

# Build and install DataDraw
RUN git clone https://github.com/waywardgeek/datadraw.git \
    && cd datadraw \
    && ./autogen.sh \
    && ./configure \
    && make \
    && make install

# Build and install Rune
RUN git clone https://github.com/google/rune.git \
    && git clone https://github.com/pornin/CTTK.git \
    && cp CTTK/inc/cttk.h CTTK \
    && cd rune \
    && make \
    # Some of these tests are expected to fail, that's OK
    && ./runtests.sh \
    # Will install under /usr/local/rune
    && make install

# Test that rune is working
RUN echo "TESTING RUNE" \
    && echo 'println "Hello, World!"' > hello.rn \
    && rune -g hello.rn \
    && ./hello \
    && echo "Should have printed 'Hello, World!'"