# docker build -t bpowers/mesh .
FROM ubuntu:18.04 as builder
MAINTAINER Bobby Powers <bobbypowers@gmail.com>

RUN apt-get update && apt-get install -y \
  build-essential \
  git \
  gcc-8 \
  g++-8 \
  python3 \
  sudo \
 && rm -rf /var/lib/apt/lists/* \
 && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 800 --slave /usr/bin/g++ g++ /usr/bin/g++-8 \
 && update-alternatives --install /usr/bin/python python /usr/bin/python3 10 \
 && rm -rf /usr/local/lib/python3.6

WORKDIR /src

COPY . .

ENV PREFIX /usr/local

RUN git submodule update --init --recursive \
 && support/install_all_configs PREFIX=/usr/local


FROM ubuntu:18.04

COPY --from=builder /usr/local/lib/libmesh* /usr/local/lib/

RUN ldconfig
