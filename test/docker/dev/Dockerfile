# To build use:
# docker build -t bpowers/dev:v2 .
FROM ubuntu:18.04
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="Ubuntu Dev"

RUN apt-get update && apt-get install -y \
  build-essential \
  git \
  gcc \
  llvm \
  clang \
  cmake \
  g++ \
  linux-tools-common \
  linux-tools-generic \
  libtool \
  libevent-dev \
  make \
  gdb \
  lcov \
  gfortran \
 && rm -rf /var/lib/apt/lists/*
