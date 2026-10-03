#include "Driver.h"
#include "Name.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#else
#include <climits>
#include <cstdlib>
#endif

// A file made by this run, by its full name: C:\\...\\hello.exe or /Users/.../hello.
static std::string fullName(const std::string &file) {
#ifdef _WIN32
    char full[_MAX_PATH];
    return _fullpath(full, file.c_str(), sizeof full) ? std::string(full) : file;
#else
    char full[PATH_MAX];
    return realpath(file.c_str(), full) ? std::string(full) : file;
#endif
}

#include <string>
#include <vector>

int main(int argc, char **argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) arguments.push_back(argv[i]);
    shalimar::Driver driver;
    int status = driver.run(arguments);
    // Said in so many words, as other compilers' IDEs do: on stderr beside the banner, and like it
    // left out by -nologo. Only for a run that compiled and finished with nothing wrong.
    if (status == 0 && driver.saysDone()) {
        for (const std::string &file : driver.produced())
            if (!file.empty()) std::fprintf(stderr, "%s\n", fullName(file).c_str());
        std::fprintf(stderr, "%s: compilation completed successfully - 0 errors\n", program::kName);
    }
    return status;
}
