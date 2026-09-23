#pragma once

// **The program's name, once**: what it calls itself in messages and temporary
// files, and the prefix of every variable it reads (SHALIMAR_AS, SHALIMAR_LD, ...). The
// Makefile's PROGRAM and RIDE's tools/make-projects.py spell it the same.

#include <string>

namespace program {

constexpr const char *kName = "shalimar";

// The environment variable named for this program: env("AS") is SHALIMAR_AS.
inline std::string env(const char *what) { return std::string("SHALIMAR_") + what; }

}
