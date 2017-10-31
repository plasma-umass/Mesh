# To build use:
# docker build -t bpowers/dev:v1 .
FROM ubuntu:17.10
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="Ubuntu Dev"

RUN apt-get update && apt-get install -y \
  build-essential \
  git \
  gcc \
  clang \
  cmake \
  g++ \
  libtool \
  libevent-dev \
  make \
  gfortran

