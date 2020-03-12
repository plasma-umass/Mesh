# docker build -t bpowers/mesh .
FROM ubuntu:18.04 as builder
MAINTAINER Bobby Powers <bobbypowers@gmail.com>

RUN apt-get update && apt-get install -y \
  build-essential \
  git \
  python3 \
  sudo \
  libxml2 \
 && rm -rf /var/lib/apt/lists/* \
 && update-alternatives --install /usr/bin/python python /usr/bin/python3 10 \
 && rm -rf /usr/local/lib/python3.6

WORKDIR /src

COPY . .

ENV PREFIX /usr/local

RUN make test

RUN support/install_all_configs


FROM ubuntu:18.04

COPY --from=builder /usr/lib/libmesh* /usr/local/lib/

RUN ldconfig
