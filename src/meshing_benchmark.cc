// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gflags/gflags.h>

#include "internal.h"

#include "bitmap.h"
#include "meshing.h"

using std::vector;
using std::string;
using std::unique_ptr;
using std::make_unique;
using mesh::Bitmap;
using std::stoi;

typedef std::basic_string<char, std::char_traits<char>, STLAllocator<char, MallocHeap>> internal_string;

DEFINE_bool(v, false, "verbose debugging output");

// https://stackoverflow.com/questions/236129/split-a-string-in-c
template <typename Out>
void split(const std::string &s, char delim, Out result) {
  std::stringstream ss;
  ss.str(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    *(result++) = item;
  }
}

// https://stackoverflow.com/questions/236129/split-a-string-in-c
std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> elems;
  split(s, delim, std::back_inserter(elems));
  return elems;
}

class MeshTestcase {
public:
  explicit MeshTestcase(int length, int occupancy, int nStrings, string method)
      : length(static_cast<size_t>(length)),
        occupancy(static_cast<size_t>(occupancy)),
        nStrings(static_cast<size_t>(nStrings)),
        method(method) {
    d_assert(length > 0);
    d_assert(occupancy > 0);
    d_assert(nStrings > 0);
  }
  vector<Bitmap<MallocHeap>> bitmaps{};
  vector<internal_string> strings{};

  size_t length;  // string length
  size_t occupancy;
  size_t nStrings;
  string method;

  ssize_t expectedResult{-1};
};

unique_ptr<MeshTestcase> openTestcase(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == nullptr) {
    fprintf(stderr, "ERROR: couldn't open testcase %s: %s\n", path, strerror(errno));
    exit(1);
  }

  char *fname = strdup(basename(path));
  char *dotPos = strrchr(fname, '.');
  // remove the extension if it exists
  if (dotPos) {
    *dotPos = 0;
  }

  vector<string> parts = split(fname, ',');
  if (parts.size() != 4) {
    fprintf(stderr, "ERROR: expected filename to have 4 parts before extension: %s\n", path);
    exit(1);
  }
  free(fname);

  auto testcase = make_unique<MeshTestcase>(stoi(parts[0]), stoi(parts[1]), stoi(parts[2]), parts[3]);

  bool loop = true;
  while (loop) {
    char *line = nullptr;
    size_t n = 0;
    ssize_t len = getline(&line, &n, f);
    if ((len < 0 && !errno) || len == 0) {
      if (line)
        free(line);
      fprintf(stderr, "ERROR: unexpected end of testcase\n");
      exit(1);
    } else if (len < 0) {
      fprintf(stderr, "ERROR: getline: %s\n", strerror(errno));
      exit(1);
    }
    d_assert(line != nullptr);

    d_assert(len > 0);
    if (line[len - 1] == '\n') {
      line[len - 1] = 0;
      len--;
    }

    d_assert(len > 0);
    if (line[len - 1] == '\r') {
      line[len - 1] = 0;
      len--;
    }

    d_assert(len > 0);
    if (line[0] == '-') {
      testcase->expectedResult = stoi(&line[1]);
      loop = false;
    } else {
      internal_string sline{line};
      d_assert(sline.length() == testcase->length);

      testcase->bitmaps.emplace_back(sline);
      testcase->strings.emplace_back(sline);

      d_assert_msg(sline == testcase->bitmaps.back().to_string(sline.length()), "expected roundtrip '%s' == '%s'", line,
                   testcase->bitmaps.back().to_string().c_str());
    }
    free(line);
  }
  fclose(f);

  d_assert_msg(testcase->strings.size() == testcase->nStrings, "%zu == %zu", testcase->strings.size(),
               testcase->nStrings);
  d_assert(testcase->expectedResult >= 0);

  return testcase;
}

bool validate(const unique_ptr<MeshTestcase> &testcase) {
  ssize_t result = 0;
  if (testcase->method == "dumb") {
    result = mesh::method::simple<MallocHeap>(testcase->bitmaps);
    if (result == testcase->expectedResult)
      return true;
  } else {
    printf("ERROR: unimplemented method '%s'\n", testcase->method.c_str());
    return false;
  }

  if (result != testcase->expectedResult) {
    printf("ERROR: unexpected result %zu (expected %zu) for %s method\n", result, testcase->expectedResult,
           testcase->method.c_str());
  }
  return false;
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

    auto testcase = openTestcase(argv[i]);

    if (!validate(testcase)) {
      printf("%s: failed to validate.\n", argv[i]);
      exit(1);
    }
  }

  return 0;
}
