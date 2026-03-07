#include <climits>
#include <exception>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <format>
#include <tuple>
#include <map>

namespace test {
    int main();
    extern std::string expected_stdout, expected_stderr;
}

using ArgCommands = std::map<std::string, std::tuple<int, std::string, std::function<void(std::vector<std::string>)>>>;

void usage(const ArgCommands &rules)
{
    std::string shortargs;
    for (const auto &rule : rules)
        if (rule.first.size() == 2)
            shortargs.push_back(rule.first[1]);

    std::cerr << std::format("usage: test [-{}] ...", shortargs) << std::endl;
    for (const auto &rule : rules)
        std::cerr << std::format("  {:<7} {}", rule.first, std::get<1>(rule.second)) << std::endl;
}

std::vector<std::string> parseargs(ArgCommands rules, unsigned pos_min, unsigned pos_max, int argc, char **argv)
{
    rules["--help"] = rules["-h"] = { 0, "Displays this help message", [&rules](auto)
    {
        usage(rules);
        exit(0);
    } };

    std::vector<std::string> positional;
    std::vector<std::string> args(argv, argv+argc);
    int i;
    for (i = 1; i < args.size(); ++i) {
        if (args[i] == "--") {
            ++i;
            break;

        } else if (args[i][0] == '-') {
            if (!rules.contains(args[i])) {
                std::cerr << std::format("error: unknown option {}", args[i]) << std::endl;
                usage(rules);
                exit(1);
            }
            auto [ nargs, _, f ] = rules.at(args[i]);
            if (i + nargs >= args.size()) {
                std::cerr << std::format("error: not enough parameters for {}", args[i]) << std::endl;
                usage(rules);
                exit(1);
            }
            f(std::vector(args.begin() + i + 1, args.begin() + i + 1 + nargs));
            i += nargs;

        } else {
            positional.push_back(args[i]);
        }
    }
    for (; i < args.size(); ++i)
        positional.push_back(args[i]);

    if (positional.size() < pos_min || positional.size() > pos_max) {
        if (pos_min == 0 && pos_max != INT_MAX) {
            std::cerr << std::format("error: this test takes at most {} parameters", pos_max);
        } else if (pos_min != 0 && pos_max == INT_MAX) {
            std::cerr << std::format("error: this test takes at least {} parameters", pos_min);
        } else if (pos_min == 0 && pos_max == 0) {
            std::cerr << std::format("error: this test takes no parameters", pos_min);
        } else {
            std::cerr << std::format("error: this test takes between {} and {} parameters", pos_min, pos_max);
        }
        std::cerr << "; got " << positional.size() << std::endl;
        usage(rules);
        exit(1);
    }
    return positional;
}

#ifndef RUNNER
int main(int argc, char **argv)
{
    int mode = 0;
    parseargs({
        { "-O", { 0, "Outputs the expected stdout and exits without running the test.", [&mode](auto) {
            mode = 1;
        } } },
        { "-E", { 0, "Outputs the expected stderr and exits without running the test.", [&mode](auto) {
            mode = 2;
        } } }
    }, 0, 0, argc, argv);

    switch (mode) {
    case 0:
        try {
            return test::main();
        } catch (std::exception &e) {
            std::cerr << "EXCEPTION: " << e.what() << std::endl;
            return 0;
        }
    case 1:
        std::cout << test::expected_stdout;
        return 0;
    case 2:
        std::cout << test::expected_stderr;
        return 0;
    }
}

#include "../squeakws.hpp"
#endif  // RUNNER
