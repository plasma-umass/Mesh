#!/usr/bin/env ruby

mb = 1024 * 1024

def fragment(n, total_memory)
  available_memory = total_memory

  retained = []

  loop do
    break if available_memory < n

    obj_count = available_memory / n
    # adjust slightly to account for the ruby object header
    str_size = n - 16

    # allocate a bunch of strings of the requested size
    table = (0...obj_count).map do
      's' * str_size
    end

    # drop 3/4 of the strings we just allocated
    table = table.each_slice(2).map(&:first)
    table = table.each_slice(2).map(&:first)

    # hold onto pointers to these strings
    retained.concat(table)

    # simulate doing some work
    sleep 0.2

    GC.start(full_mark: true, immediate_sweep: true)

    n *= 2
    available_memory -= obj_count * n / 4.0
  end

  sleep 0.2

  GC.start(full_mark: true, immediate_sweep: true)
end

fragment(512, 128 * mb)
