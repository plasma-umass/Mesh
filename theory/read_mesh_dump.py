#!/usr/bin/env python
# -*- coding: utf-8 -*-

import json
import sys
import argparse
from collections import defaultdict

import meshers

MB = 1/1024.0/1024.0

class Span(object):
    def __init__(self, obj):
        self.name = obj['name']
        self.size = obj['object-size']
        self.length = obj['length']
        self.bitmap = obj['bitmap']


def read_data(json_path):
    '''
    Reads a dict of size -> span list from a mesh dump file at `json_path`
    '''
    size_classes = defaultdict(list)

    with open(json_path) as f:
        for l in f.readlines():
            obj = json.loads(l)
            span = Span(obj)
            size_classes[span.size].append(span)

    return size_classes


def count_meshes(mesher, spans):
    bitmaps = [s.bitmap for s in spans]
    if len(bitmaps) % 2 == 1:
        bitmaps.append('1' * len(s.bitmap))

    return mesher(bitmaps)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--size', type=int, help='size to dump graph')
    parser.add_argument('json_file', nargs=1, help='A JSON dump from libmesh')
    args = parser.parse_args()

    if not args.json_file:
        parser.print_help()
        return 1

    size_classes = read_data(args.json_file[0])

    sizes = sorted(size_classes.keys(), reverse=True)

    total_size = 0
    for size in sizes:
        spans = size_classes[size]
        total_size += sum([s.size * s.length for s in spans])

    print('Total heap size: %.1f MiB' % (total_size * MB,))

    saved = 0
    for size in sizes:
        if size >= 4096:
            continue
        spans = size_classes[size]
        # n = count_meshes(meshers.optimalMesher, spans)
        # n = count_meshes(meshers.optimalMesher, spans)
        n = count_meshes(meshers.greedyMesher, spans)
        print('\t%5d: %d spans (%d meshes)' % (size, len(spans), len(n)))
        saved += (size * spans[0].length) * len(n)

    print('Saved size: %.1f MiB' % (saved * MB,))

    return 0


if __name__ == '__main__':
    sys.exit(main())
