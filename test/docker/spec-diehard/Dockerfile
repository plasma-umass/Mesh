# To build use:
# docker build -t bpowers/spec:diehard .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="SPEC w/ diehard"

COPY ./libdiehard.so /usr/local/lib/
COPY ./mstat /usr/local/bin
COPY ./mstatgcc /usr/local/bin/mstatgcc
COPY ./mstatgcc /usr/local/bin/mstatg++
COPY ./Linux-diehard.cfg /spec/config/
COPY ./Linux-diehard-mstat.cfg /spec/config/

RUN ldconfig && \
    chmod 755 /usr/local/bin/mstatgcc /usr/local/bin/mstatg++ /usr/local/bin/mstat && \
    chmod u+s /usr/local/bin/mstat && \
    cd /spec && . ./shrc && \
    runspec --config=Linux-diehard-mstat.cfg --action=build --tune=base int
