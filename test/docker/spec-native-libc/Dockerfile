# To build use:
# docker build -t bpowers/spec:native .
FROM bpowers/spec:v1
MAINTAINER Bobby Powers <bpowers@cs.umass.edu>

LABEL description="Native SPEC"

COPY ./Linux-libc.cfg /spec/config/

RUN cd /spec && . ./shrc && runspec --config=Linux-libc.cfg --action=build --tune=base int fp
