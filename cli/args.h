#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// Minimal argument parser — no external dependencies.
struct Args {
    std::unordered_map<std::string, std::string> flags;
    std::vector<std::string> positional;

    static Args parse(int argc, char* argv[]) {
        Args args;
        for (int i = 1; i < argc; ++i) {
            std::string s = argv[i];
            if (s.substr(0, 2) == "--" && i + 1 < argc) {
                args.flags[s.substr(2)] = argv[++i];
            } else {
                args.positional.push_back(s);
            }
        }
        return args;
    }

    std::string get(const std::string& key, const std::string& def = "") const {
        auto it = flags.find(key);
        return it != flags.end() ? it->second : def;
    }

    bool has(const std::string& key) const {
        return flags.count(key) > 0;
    }

    int get_int(const std::string& key, int def = 0) const {
        auto it = flags.find(key);
        return it != flags.end() ? std::stoi(it->second) : def;
    }

    double get_double(const std::string& key, double def = 0.0) const {
        auto it = flags.find(key);
        return it != flags.end() ? std::stod(it->second) : def;
    }

    bool wants_help() const {
        return flags.count("help") > 0 ||
               (!positional.empty() && (positional[0] == "--help" || positional[0] == "-h"));
    }
};
