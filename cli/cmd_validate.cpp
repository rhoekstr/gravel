#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/ch_query.h"
#include "gravel/validation/validator.h"
#include <iostream>
#include <nlohmann/json.hpp>

int cmd_validate(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("graph") || !args.has("ch")) {
        std::cerr << "Usage: gravel validate --graph <.gravel.meta> --ch <.gravel.ch>"
                  << " [--mode sampled|exhaustive] [--pairs <n>]\n";
        return 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::CHQuery query(ch);

    gravel::ValidationConfig cfg;
    std::string mode = args.get("mode", "sampled");
    cfg.mode = (mode == "exhaustive") ? gravel::ValidationConfig::EXHAUSTIVE
                                      : gravel::ValidationConfig::SAMPLED;
    cfg.sample_count = static_cast<uint32_t>(args.get_int("pairs", 10000));

    std::cerr << "Validating CH (" << mode << ", "
              << (cfg.mode == gravel::ValidationConfig::EXHAUSTIVE
                  ? "all pairs" : std::to_string(cfg.sample_count) + " pairs")
              << ")...\n";

    auto report = gravel::validate_ch(*graph, query, cfg);

    nlohmann::json j;
    j["passed"] = report.passed;
    j["pairs_tested"] = report.pairs_tested;
    j["mismatches"] = report.mismatches;
    j["max_absolute_error"] = report.max_absolute_error;

    if (!report.failures.empty()) {
        auto& failures = j["failures"];
        for (size_t i = 0; i < std::min<size_t>(report.failures.size(), 10); ++i) {
            auto [s, t, expected, got] = report.failures[i];
            failures.push_back({{"from", s}, {"to", t},
                               {"expected", expected}, {"got", got}});
        }
    }

    std::cout << j.dump(2) << "\n";
    return report.passed ? 0 : 1;
}
