#include "gravel/io/arrow_output.h"
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

#include <memory>
#include <utility>
#ifdef GRAVEL_HAS_ARROW
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>
#include <stdexcept>
#include <vector>
#include <string>

namespace gravel {

void write_fragility_parquet(const std::vector<FragilityResult>& results,
                              const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
                              const std::string& path) {
    arrow::UInt32Builder source_b, target_b, path_len_b;
    arrow::DoubleBuilder primary_dist_b, bottleneck_ratio_b, replacement_dist_b;
    arrow::UInt32Builder bn_source_b, bn_target_b;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        NodeID s = i < od_pairs.size() ? od_pairs[i].first : 0;
        NodeID t = i < od_pairs.size() ? od_pairs[i].second : 0;

        (void)source_b.Append(s);
        (void)target_b.Append(t);
        (void)primary_dist_b.Append(r.primary_distance);
        (void)path_len_b.Append(static_cast<uint32_t>(r.primary_path.size()));

        if (r.valid() && !r.edge_fragilities.empty()) {
            const auto& bn = r.bottleneck();
            (void)bn_source_b.Append(bn.source);
            (void)bn_target_b.Append(bn.target);
            double ratio = std::isinf(bn.fragility_ratio) ? -1.0 : bn.fragility_ratio;
            (void)bottleneck_ratio_b.Append(ratio);
            double repl = bn.replacement_distance >= INF_WEIGHT ? -1.0 : bn.replacement_distance;
            (void)replacement_dist_b.Append(repl);
        } else {
            (void)bn_source_b.Append(0);
            (void)bn_target_b.Append(0);
            (void)bottleneck_ratio_b.Append(-1.0);
            (void)replacement_dist_b.Append(-1.0);
        }
    }

    std::shared_ptr<arrow::Array> source_a, target_a, primary_a, path_len_a;
    std::shared_ptr<arrow::Array> bn_src_a, bn_tgt_a, ratio_a, repl_a;
    (void)source_b.Finish(&source_a);
    (void)target_b.Finish(&target_a);
    (void)primary_dist_b.Finish(&primary_a);
    (void)path_len_b.Finish(&path_len_a);
    (void)bn_source_b.Finish(&bn_src_a);
    (void)bn_target_b.Finish(&bn_tgt_a);
    (void)bottleneck_ratio_b.Finish(&ratio_a);
    (void)replacement_dist_b.Finish(&repl_a);

    auto schema = arrow::schema({
        arrow::field("source", arrow::uint32()),
        arrow::field("target", arrow::uint32()),
        arrow::field("primary_distance", arrow::float64()),
        arrow::field("path_length", arrow::uint32()),
        arrow::field("bottleneck_source", arrow::uint32()),
        arrow::field("bottleneck_target", arrow::uint32()),
        arrow::field("bottleneck_ratio", arrow::float64()),
        arrow::field("replacement_distance", arrow::float64()),
    });

    auto table = arrow::Table::Make(schema, {
        source_a, target_a, primary_a, path_len_a,
        bn_src_a, bn_tgt_a, ratio_a, repl_a
    });

    auto outfile = arrow::io::FileOutputStream::Open(path).ValueOrDie();
    (void)parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 65536);
}

void write_county_fragility_parquet(const std::vector<CountyFragilityResult>& results,
                                      const std::vector<std::string>& labels,
                                      const std::string& path) {
    arrow::StringBuilder label_b;
    arrow::DoubleBuilder composite_b, ac_b, kirchhoff_b, accessibility_b;
    arrow::UInt32Builder nodes_b, edges_b, bridges_b, entry_b;

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        (void)label_b.Append(i < labels.size() ? labels[i] : "");
        (void)composite_b.Append(r.composite_index);
        (void)nodes_b.Append(r.subgraph_nodes);
        (void)edges_b.Append(r.subgraph_edges);
        (void)bridges_b.Append(static_cast<uint32_t>(r.bridges.bridges.size()));
        (void)ac_b.Append(r.algebraic_connectivity);
        (void)kirchhoff_b.Append(r.kirchhoff_index_value);
        (void)accessibility_b.Append(r.accessibility.accessibility_score);
        (void)entry_b.Append(r.entry_point_count);
    }

    std::shared_ptr<arrow::Array> la, ca, na, ea, ba, aa, ka, xa, pa;
    (void)label_b.Finish(&la);
    (void)composite_b.Finish(&ca);
    (void)nodes_b.Finish(&na);
    (void)edges_b.Finish(&ea);
    (void)bridges_b.Finish(&ba);
    (void)ac_b.Finish(&aa);
    (void)kirchhoff_b.Finish(&ka);
    (void)accessibility_b.Finish(&xa);
    (void)entry_b.Finish(&pa);

    auto schema = arrow::schema({
        arrow::field("label", arrow::utf8()),
        arrow::field("composite_index", arrow::float64()),
        arrow::field("subgraph_nodes", arrow::uint32()),
        arrow::field("subgraph_edges", arrow::uint32()),
        arrow::field("bridges_count", arrow::uint32()),
        arrow::field("algebraic_connectivity", arrow::float64()),
        arrow::field("kirchhoff_index", arrow::float64()),
        arrow::field("accessibility_score", arrow::float64()),
        arrow::field("entry_point_count", arrow::uint32()),
    });

    auto table = arrow::Table::Make(schema, {la, ca, na, ea, ba, aa, ka, xa, pa});
    auto outfile = arrow::io::FileOutputStream::Open(path).ValueOrDie();
    (void)parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 65536);
}

void write_betweenness_parquet(const BetweennessResult& result,
                                const ArrayGraph& graph,
                                const std::string& path) {
    arrow::UInt32Builder idx_b, src_b, tgt_b;
    arrow::DoubleBuilder score_b;

    NodeID n = graph.node_count();
    uint32_t edge_idx = 0;
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            (void)idx_b.Append(edge_idx);
            (void)src_b.Append(u);
            (void)tgt_b.Append(targets[i]);
            double score = edge_idx < result.edge_scores.size() ? result.edge_scores[edge_idx] : 0.0;
            (void)score_b.Append(score);
            edge_idx++;
        }
    }

    std::shared_ptr<arrow::Array> ia, sa, ta, va;
    (void)idx_b.Finish(&ia);
    (void)src_b.Finish(&sa);
    (void)tgt_b.Finish(&ta);
    (void)score_b.Finish(&va);

    auto schema = arrow::schema({
        arrow::field("edge_index", arrow::uint32()),
        arrow::field("source", arrow::uint32()),
        arrow::field("target", arrow::uint32()),
        arrow::field("betweenness_score", arrow::float64()),
    });

    auto table = arrow::Table::Make(schema, {ia, sa, ta, va});
    auto outfile = arrow::io::FileOutputStream::Open(path).ValueOrDie();
    (void)parquet::arrow::WriteTable(*table, arrow::default_memory_pool(), outfile, 65536);
}

}  // namespace gravel

#else  // !GRAVEL_HAS_ARROW

namespace gravel {
// Arrow stubs not needed — only the JSONL fallback is compiled without Arrow
}  // namespace gravel

#endif  // GRAVEL_HAS_ARROW

namespace gravel {

void write_fragility_jsonl(const std::vector<FragilityResult>& results,
                            const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
                            const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        nlohmann::json j;
        j["source"] = i < od_pairs.size() ? od_pairs[i].first : 0;
        j["target"] = i < od_pairs.size() ? od_pairs[i].second : 0;
        j["primary_distance"] = r.primary_distance < INF_WEIGHT
                                ? nlohmann::json(r.primary_distance)
                                : nlohmann::json(nullptr);
        j["path_length"] = r.primary_path.size();

        if (r.valid() && !r.edge_fragilities.empty()) {
            const auto& bn = r.bottleneck();
            j["bottleneck_source"] = bn.source;
            j["bottleneck_target"] = bn.target;
            j["bottleneck_ratio"] = std::isinf(bn.fragility_ratio) ? nullptr : nlohmann::json(bn.fragility_ratio);
            j["replacement_distance"] = bn.replacement_distance >= INF_WEIGHT ? nullptr : nlohmann::json(bn.replacement_distance);
        }

        out << j.dump() << "\n";
    }
}

}  // namespace gravel
