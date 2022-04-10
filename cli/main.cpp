#include <cstdlib>
#include <cstring>

#include <fstream>
#include <iostream>
#include <lyra/lyra.hpp>

#include "thorin/config.h"

#include "cli/dialects.h"
#include "thorin/be/dot/dot.h"
#include "thorin/be/ll/ll.h"
#include "thorin/fe/parser.h"
#include "thorin/pass/pass.h"
#include "thorin/util/stream.h"

#ifdef _WIN32
#    include <windows.h>
#    define popen  _popen
#    define pclose _pclose
#else
#    include <dlfcn.h>
#endif

using namespace thorin;
using namespace std::literals;

static const auto version = "thorin command-line utility version " THORIN_VER "\n";

/// see https://stackoverflow.com/a/478960
static std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) { throw std::runtime_error("error: popen() failed!"); }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) { result += buffer.data(); }
    return result;
}

static std::string get_clang_from_path() {
    std::string clang;
#ifndef _WIN32
    clang = exec("which clang");
#else
    clang = exec("where clang");
#endif
    clang.erase(std::remove(clang.begin(), clang.end(), '\n'), clang.end());
    return clang;
}

int main(int argc, char** argv) {
    try {
        std::string input, prefix;
        std::string clang     = get_clang_from_path();

        bool emit_ll     = false;
        bool emit_md     = false;
        bool emit_dot    = false;
        bool emit_thorin = false;
        bool show_help   = false;

        std::vector<std::string> dialects, dialect_paths, emitters;
        std::vector<size_t> breakpoints;

        auto print_version = [](bool) {
            std::cerr << version;
            std::exit(EXIT_SUCCESS);
        };

        int verbose = 0;
        auto inc_verbose = [&](bool) { ++verbose; };
        static constexpr const char* Backends = "thorin|md|ll|dot";

        // clang-format off
        auto cli = lyra::cli()
            | lyra::help(show_help)
            | lyra::opt(print_version            )["-v"]["--version"     ]("Display version info and exit.")
            | lyra::opt(clang,         "clang"   )["-c"]["--clang"       ]("Path to clang executable (default: " + clang + ").")
            | lyra::opt(dialects,      "dialect" )["-d"]["--dialect"     ]("Dynamically load dialect [WIP].")
            | lyra::opt(dialect_paths, "path"    )["-D"]["--dialect-path"]("Path to search dialects in.")
            | lyra::opt(emitters,      Backends  )["-e"]["--emit"        ]("Select emitter.").choices("thorin", "md", "ll", "dot")
            | lyra::opt(inc_verbose              )["-V"]["--verbose"     ]("Verbose mode. Multiple -V options increase the verbosity. The maximum is 4.").cardinality(0, 4)
#ifndef NDEBUG
            | lyra::opt(breakpoints,   "gid"     )["-b"]["--break"       ]("Trigger break-point upon construction of node with global id <gid>. Useful when running in a debugger.")
#endif
            | lyra::opt(prefix,        "prefix"  )["-o"]["--output"      ]("Prefix used for various output files.")
            | lyra::arg(input,         "file"    )                        ("Input file.");

        if (auto result = cli.parse({argc, argv}); !result) throw std::invalid_argument(result.message());

        if (show_help) {
            std::cerr << cli << "\n";
            return EXIT_SUCCESS;
        }

        for (const auto& e : emitters) {
            if (false) {}
            else if (e == "thorin") emit_thorin = true;
            else if (e == "ll")     emit_ll     = true;
            else if (e == "md")     emit_md     = true;
            else if (e == "dot")    emit_dot    = true;
            else unreachable();
        }
        // clang-format on

        if (!dialects.empty()) {
            for (const auto& dialect : dialects) test_plugin(dialect, dialect_paths);
            return EXIT_SUCCESS;
        }

        if (input.empty()) throw std::invalid_argument("error: no input given");
        if (prefix.empty()) {
            auto i = input.rfind('.');
            if (i == std::string::npos) throw std::invalid_argument("error: cannot derive prefix for output files");
            prefix = input.substr(0, i);
        }

        World world;
        world.set_log_stream(std::make_shared<thorin::Stream>(std::cerr));
        world.set_log_level((LogLevel)verbose);
#ifndef NDEBUG
        for (auto b : breakpoints) world.breakpoint(b);
#endif

        std::ifstream ifs(input);
        if (!ifs) {
            errln("error: cannot read file '{}'", input);
            return EXIT_FAILURE;
        }

        std::ofstream ofs;
        if (emit_md) ofs.open(prefix + ".md");

        Parser parser(world, input, ifs, emit_md ? &ofs : nullptr);
        parser.parse_module();

        if (emit_thorin) world.dump();

        if (emit_dot) {
            std::ofstream of(prefix + ".dot");
            Stream s(of);
            dot::emit(world, s);
        }
        if (emit_ll) {
            std::ofstream of(prefix + ".ll");
            Stream s(of);
            ll::emit(world, s);
        }
    } catch (const std::exception& e) {
        errln("{}", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        errln("error: unknown exception");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
