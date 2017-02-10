// Copyright 2016 University of Massachusetts, Amherst

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "bitmap.h"

#include "internal.h"

using std::vector;
using std::string;
using std::unique_ptr;
using std::make_unique;
using mesh::Bitmap;

DEFINE_bool(v, false, "verbose debugging output");

class MeshTestcase {
public:
  explicit MeshTestcase(int length, int occupancy, int nStrings, string method)
      : length(length), occupancy(occupancy), nStrings(nStrings), method(method) {
  }
  vector<Bitmap<MallocHeap>> bitmaps{};
  vector<string> strings{};

  int length;
  int occupancy;
  int nStrings;
  string method;

  int expectedResult{-1};
};

unique_ptr<MeshTestcase> openTestcase(const char *path) {
  return make_unique<MeshTestcase>(-1, -1, -1, "unknown");
}

int main(int argc, char *argv[]) {
  string usage("Reads in string dumps and attempts to mesh.  Sample usage:\n");
  usage += argv[0];
  usage += " <DUMP_FILE>";
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (argc <= 1) {
    fprintf(stderr, "ERROR: expected at least one filename pointing to a dump.\n");
    fprintf(stderr, "%s: %s\n", basename(argv[0]), gflags::ProgramUsage());
    exit(1);
  }

  for (auto i = 1; i < argc; ++i) {
    if (FLAGS_v) {
      printf("meshing strings from %s\n", argv[i]);
    }
  }

  return 0;
}
