# To build use:
# docker build -t bpowers/spec:mesh .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="Mesh SPEC"

COPY ./libmesh.so /usr/local/lib/
COPY ./Linux-mesh.cfg /spec/config/

RUN ldconfig && cd /spec && . ./shrc && runspec --config=Linux-mesh.cfg --action=build --tune=base int fp
