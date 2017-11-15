# To build use:
# docker build -t bpowers/spec:hoard .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="SPEC w/ hoard"

COPY ./libhoard.so /usr/local/lib/
COPY ./mstat /usr/local/bin
COPY ./mstatgcc /usr/local/bin/mstatgcc
COPY ./mstatgcc /usr/local/bin/mstatg++
COPY ./Linux-hoard.cfg /spec/config/
COPY ./Linux-hoard-mstat.cfg /spec/config/

RUN ldconfig && \
    chmod 755 /usr/local/bin/mstatgcc /usr/local/bin/mstatg++ /usr/local/bin/mstat && \
    chmod u+s /usr/local/bin/mstat && \
    cd /spec && . ./shrc && \
    runspec --config=Linux-hoard-mstat.cfg --action=build --tune=base int
