FROM docker.io/ubuntu:26.04
LABEL maintainer "DeftDawg <DeftDawg@gmail.com>"

# Caps the parallelism cmake/ninja use for every build step (deps + main
# target), so RAM usage stays bounded regardless of how many cores the
# host/container has available. Overridable via --build-arg NCORES=N.
ARG NCORES=14
ENV CMAKE_BUILD_PARALLEL_LEVEL=${NCORES}

# deps/CMakeLists.txt (and some vendored third-party sources) declare
# cmake_minimum_required(VERSION 3.2), which CMake 4.x (shipped with
# Ubuntu 26.04) refuses to configure. This env var makes CMake treat any
# minimum below 3.5 as 3.5, without touching the vendored CMakeLists files.
ENV CMAKE_POLICY_VERSION_MINIMUM=3.5

# GCC 15 (Ubuntu 26.04's default gcc/g++) switched its default C dialect
# to gnu23, under which an empty parameter list `foo(){}` now means "zero
# arguments" instead of "unspecified/unchecked" (the old C17-and-earlier
# meaning). Several vendored deps' autotools ./configure scripts (e.g.
# GMP) rely on the old meaning and fail to even detect a working compiler
# under gnu23. Pin the whole build to gcc-13/g++-13 (still gnu17-default)
# instead of patching every affected vendored configure script.
ENV CC=gcc-13
ENV CXX=g++-13

# Disable interactive package configuration
RUN apt-get update && \
    echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# Add a deb-src
RUN echo deb-src http://archive.ubuntu.com/ubuntu \
    $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2) main universe>> /etc/apt/sources.list 

RUN apt-get update && apt-get install  -y \
    autoconf \
    bc \
    bsdextrautils \
    build-essential \
    cmake \
    curl \
    eglexternalplatform-dev \
    extra-cmake-modules \
    file \
    g++-13 \
    gcc-13 \
    git \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    libbz2-dev \
    libcairo2-dev \
    libcurl4-openssl-dev \
    libdbus-1-dev \
    libglew-dev \ 
    libglu1-mesa-dev \
    libglu1-mesa-dev \
    libgstreamer1.0-dev \
    libgstreamerd-3-dev \ 
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-extra1.0-dev \
    libgtk-3-dev \
    libgtk-3-dev \
    libosmesa6-dev \
    libsecret-1-dev \
    libsoup2.4-dev \
    libssl3 \
    libssl-dev \
    libtool \
    libudev-dev \
    libwayland-dev \
    libwebkit2gtk-4.1-dev \
    libxkbcommon-dev \
    locales \
    locales-all \
    m4 \
    pkgconf \
    sudo \
    util-linux \
    util-linux-extra \
    wayland-protocols \
    wget

# Change your locale here if you want.  See the output
# of `locale -a` to pick the correct string formatting.
ENV LC_ALL=en_US.utf8
RUN locale-gen $LC_ALL

# Set this so that Orca Slicer doesn't complain about
# the CA cert path on every startup
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt

WORKDIR CrealityPrint

# Copy only what -u/-d need first, so editing src/ (the vast majority of
# commits) doesn't invalidate the deps build cache below - deps compile
# from scratch takes far longer than the main build.
COPY BuildLinux.sh version.inc ./
COPY linux.d/ ./linux.d/
COPY deps/ ./deps/
# deps/CMakeLists.txt and several deps/*/*.cmake files
# (include(${CMAKE_SOURCE_DIR}/../cmake/modules/CheckUos.cmake)) reach
# outside deps/ into the repo-root cmake/ dir - needed at -d time too.
COPY cmake/ ./cmake/

# These can run together, but we run them seperate for podman caching
# Update System dependencies
RUN ./BuildLinux.sh -u

# Build dependencies in ./deps
RUN ./BuildLinux.sh -d

# Now bring in the rest of the source tree for the main build/AppImage
# steps below.
COPY ./ ./

# gcc-13's own linker-time libstdc++.so (from libstdc++-13-dev) only
# exports up to CXXABI_1.3.14, but Ubuntu 26.04's system libraries (e.g.
# libicui18n.so.78, pulled in via webkit2gtk/gstreamer) were built with
# gcc-15 and need CXXABI_1.3.15, so linking CrealityPrint with gcc-13
# fails with "undefined reference to __cxa_call_terminate@CXXABI_1.3.15".
# CrealityPrint's own C++ sources compile fine under gcc-15 (unlike the
# old-style C in vendored deps like GMP) and linking with gcc-15 matches
# the ABI of the system libraries it links against, so switch back to the
# distro default compiler for the main build and AppImage steps.
ENV CC=gcc
ENV CXX=g++

# Build slic3r
RUN ./BuildLinux.sh -s

# Build AppImage
ENV container podman
RUN ./BuildLinux.sh -i

# It's easier to run Orca Slicer as the same username,
# UID and GID as your workstation.  Since we bind mount
# your home directory into the container, it's handy
# to keep permissions the same.  Just in case, defaults
# are root.
SHELL ["/bin/bash", "-l", "-c"]
ARG USER=root
ARG UID=0
ARG GID=0
RUN [[ "$UID" != "0" ]] \
  && groupadd -f -g $GID $USER \
  && (useradd -u $UID -g $GID $USER || getent passwd $UID)

# Using an entrypoint instead of CMD because the binary
# accepts several command line arguments.
ENTRYPOINT ["/CrealityPrint/build/package/bin/CrealityPrint"]
