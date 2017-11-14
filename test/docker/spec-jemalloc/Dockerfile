# To build use:
# docker build -t bpowers/spec:jemalloc .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="SPEC w/ jemalloc"

COPY ./mstat /usr/local/bin
COPY ./mstatgcc /usr/local/bin/mstatgcc
COPY ./mstatgcc /usr/local/bin/mstatg++
COPY ./Linux-jemalloc.cfg /spec/config/
COPY ./Linux-jemalloc-mstat.cfg /spec/config/

RUN apt-get install -y libjemalloc-dev && ldconfig && \
    chmod 755 /usr/local/bin/mstatgcc /usr/local/bin/mstatg++ /usr/local/bin/mstat && \
    chmod u+s /usr/local/bin/mstat && \
    cd /spec && . ./shrc && \
    runspec --config=Linux-jemalloc-mstat.cfg --action=build --tune=base int
