#!/usr/bin/env python3
import time

retained = []

for i in range(10):
    size = 2 ** (5 + i)
    count = 2 ** (18 - i)
    print('size: %5d count: %6d // %d' % (size, count, size * count))
    allocs = ['.' * size for i in range(count)]
    allocs = allocs[::2]   # drop every other allocation
    retained.append(allocs)
    time.sleep(.15)
    # for smaller objects, make the retained set even more sparse
    for j in range(i):
        if j < i - 3:
            continue
        retained[j] = retained[j][::2]

for allocs in retained:
    print('%5d / %6d' % (len(allocs[0]), len(allocs)))
    time.sleep(.15)

allocs = None

print('done.')
