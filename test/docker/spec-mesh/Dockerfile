# To build use:
# docker build -t bpowers/spec:mesh .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="SPEC w/ mesh"

COPY ./libmesh.so /usr/local/lib/
COPY ./mstat /usr/local/bin
COPY ./mstatgcc /usr/local/bin/mstatgcc
COPY ./mstatgcc /usr/local/bin/mstatg++
COPY ./Linux-mesh.cfg /spec/config/
COPY ./Linux-mesh-mstat.cfg /spec/config/

RUN chmod 755 /usr/local/bin/mstatgcc /usr/local/bin/mstatg++ /usr/local/bin/mstat && \
    chmod u+s /usr/local/bin/mstat && \
    cd /spec && . ./shrc && \
    runspec --config=Linux-mesh.cfg       --action=build --tune=base int && \
    runspec --config=Linux-mesh-mstat.cfg --action=build --tune=base int
