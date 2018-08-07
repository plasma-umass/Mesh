#!/bin/bash
set -euo pipefail
set -x

TEST=fragmentation.py
#TEST=big3.py
#TEST=python_memleak.py

/bin/time mstat -o glibc.tsv -freq 127 -env PYTHON_MALLOC=malloc python3 $TEST
sleep 5
/bin/time mstat -o glibc2.tsv -freq 127 -env PYTHON_MALLOC=malloc -env MALLOC_ARENA_MAX=2 python3 $TEST
sleep 5
/bin/time mstat -o pymalloc.tsv -freq 127 -env PYTHON_MALLOC=pymalloc python3 $TEST
sleep 5
/bin/time mstat -o mesh_mesh.tsv -freq 127 -env PYTHON_MALLOC=malloc -env LD_PRELOAD=libmesh.so python3 $TEST
sleep 5
/bin/time mstat -o mesh_nomesh.tsv -freq 127 -env PYTHON_MALLOC=malloc -env MESH_PERIOD_SECS=0 -env LD_PRELOAD=libmesh.so python3 $TEST
sleep 5
/bin/time mstat -o hoard.tsv -freq 127 -env PYTHON_MALLOC=malloc -env LD_PRELOAD=libhoard.so python3 $TEST
sleep 5
/bin/time mstat -o jemalloc.tsv -freq 127 -env PYTHON_MALLOC=malloc -env LD_PRELOAD=libjemalloc.so python3 $TEST
sleep 5
/bin/time mstat -o tcmalloc.tsv -freq 127 -env PYTHON_MALLOC=malloc -env LD_PRELOAD=libtcmalloc.so python3 $TEST
