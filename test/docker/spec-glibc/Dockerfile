# To build use:
# docker build -t bpowers/spec:glibc .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="SPEC w/ glibc"

COPY ./mstat /usr/local/bin
COPY ./mstatgcc /usr/local/bin/mstatgcc
COPY ./mstatgcc /usr/local/bin/mstatg++
COPY ./Linux-glibc.cfg /spec/config/
COPY ./Linux-glibc-mstat.cfg /spec/config/

RUN chmod 755 /usr/local/bin/mstatgcc /usr/local/bin/mstatg++ /usr/local/bin/mstat && \
    chmod u+s /usr/local/bin/mstat && \
    cd /spec && . ./shrc && \
    runspec --config=Linux-glibc.cfg       --action=build --tune=base int && \
    runspec --config=Linux-glibc-mstat.cfg --action=build --tune=base int
