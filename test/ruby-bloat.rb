=begin
This script attempts to reproduce poor glibc allocator behavior within Ruby, leading
to extreme memory fragmentation and process RSS bloat.

glibc allocates memory using per-thread "arenas".  These blocks can easily fragment when
some objects are free'd and others are long-lived.

Our script runs multiple threads, all allocating randomly sized "large" Strings between 4,000
and 40,000 bytes in size.  This simulates Rails views with ERB creating large chunks of HTML
to output to the browser. Some of these strings are kept around and some are discarded.

With the builds below and the frag.rb script, jemalloc and MALLOC_ARENA_MAX=2 both show a noticeable reduction in RSS.
=end


=begin
# This script is run on a 4-core 8GB instance.
# system ruby is only necessary to compile our own ruby
#
# with a blank ubuntu 18.04 image...
apt-get update
apt-get install -y autoconf bison build-essential libssl-dev libyaml-dev libreadline-dev zlib1g-dev libncurses5-dev libffi-dev libgdbm-compat-dev libgdbm-dev libjemalloc-dev ruby


curl -LO https://cache.ruby-lang.org/pub/ruby/2.5/ruby-2.5.1.tar.bz2
tar xvf ruby-2.5.1.tar.bz2
cd ruby-2.5.1
./configure --disable-install-doc --prefix=/root/versions/2.5.1
make -j4
make install

make distclean
./configure --with-jemalloc --disable-install-doc --prefix=/root/versions/2.5.1j
make -j4
make install

/root/versions/2.5.1/bin/ruby -v -rrbconfig -e 'p RbConfig::CONFIG["configure_args"]'
/root/versions/2.5.1j/bin/ruby -v -rrbconfig -e 'p RbConfig::CONFIG["configure_args"]'


##### RESULTS

# note on a 4-core machine, MALLOC_ARENA_MAX defaults to (cores * 8) so we are merely being explicit here.
#

> MALLOC_ARENA_MAX=32 /root/versions/2.5.1/bin/ruby -v frag.rb

ruby 2.5.1p57 (2018-03-29 revision 63029) [x86_64-linux]
 '--disable-install-doc' '--prefix=/root/versions/2.5.1'
Total string size: 1903MB
VmRSS:	 2831832 kB

> MALLOC_ARENA_MAX=2 /root/versions/2.5.1/bin/ruby -v frag.rb

ruby 2.5.1p57 (2018-03-29 revision 63029) [x86_64-linux]
 '--disable-install-doc' '--prefix=/root/versions/2.5.1'
Total string size: 1917MB
VmRSS:	 2311052 kB

> /root/versions/2.5.1j/bin/ruby -v frag.rb

ruby 2.5.1p57 (2018-03-29 revision 63029) [x86_64-linux]
 '--with-jemalloc' '--disable-install-doc' '--prefix=/root/versions/2.5.1j'
Total string size: 1908MB
VmRSS:	 2306372 kB

=end


Thread.abort_on_exception = true

require 'rbconfig'

puts RbConfig::CONFIG["configure_args"]

THREAD_COUNT = (ENV["T"] || 10).to_i
Threads = []
Strings = []
Threads.clear

srand(1234)

def frag
  saver = []
  # requests
  100.times do |idx|

    # each request allocates 1000 strings, then discards 90%
    1_000.times do |x|
      # allocate a random sized heap string
      s = 'a' * ((rand(4000) * 10) + 97)
      saver << s
    end

    # now delete random elements to create holes in the heap
    1000.times do
      saver.delete_at(rand(saver.size)) if rand < 0.9
    end

    GC.start if idx % 100 == 0
  end

  total = saver.inject(0) {|memo, str| memo += str.bytesize }
  #print "Thread #{Thread.current.object_id.to_s(36)} has #{total / (1024 * 1024)}MB\n"
  Strings << saver
  total
end

THREAD_COUNT.times do
  Threads << Thread.new(&method(:frag))
end

strsize = 0
Threads.each do|t|
  strsize += t.value
end
Threads.clear
GC.start

puts "Total string size: #{strsize / (1024 * 1024)}MB"
puts `ps aux | grep #{$$}` if RUBY_PLATFORM =~ /darwin/

IO.foreach("/proc/#{$$}/status") do |line|
  print line if line =~ /VmRSS/
end if RUBY_PLATFORM =~ /linux/
