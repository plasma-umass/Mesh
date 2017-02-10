// Copyright 2016 University of Massachusetts, Amherst

#include <cstdio>
#include <string>

#include <gflags/gflags.h>

using std::string;

DEFINE_bool(v, false, "verbose debugging output");

int main(int argc, char *argv[]) {
  string usage("Reads in string dumps and attempts to mesh.  Sample usage:\n");
  usage += argv[0];
  usage += " <DUMP_FILE>";
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  for (auto i = 1; i < argc; ++i) {
    if (FLAGS_v) {
      printf("meshing strings from %s\n", argv[i]);
    }
  }

  return 0;
}
