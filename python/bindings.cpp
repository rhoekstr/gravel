#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "gravel/core/types.h"
#include "gravel/core/array_graph.h"
#include "gravel/core/csv_graph.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/core/dijkstra.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/validation/validator.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/fragility/via_path.h"
#include "gravel/fragility/hershberger_suri.h"
#include "gravel/fragility/bernstein_approx.h"
#include "gravel/validation/fragility_validator.h"
#include "gravel/core/subgraph.h"
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/analysis/kirchhoff.h"
#include "gravel/analysis/natural_connectivity.h"
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/od_sampling.h"
#include "gravel/ch/landmarks.h"
#include "gravel/fragility/fragility_filter.h"
#include "gravel/analysis/location_fragility.h"
#include "gravel/geo/elevation.h"
#include "gravel/analysis/closure_risk.h"
#include "gravel/simplify/simplify.h"
#include "gravel/core/edge_labels.h"
#include "gravel/simplify/bridge_classification.h"
#include "gravel/analysis/scenario_fragility.h"
#include "gravel/analysis/uncertainty.h"
#include "gravel/geo/edge_confidence.h"
#include "gravel/analysis/tiled_fragility.h"
#include "gravel/analysis/progressive_fragility.h"
#include "gravel/core/edge_sampler.h"
#include "gravel/core/incremental_sssp.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/geo/geojson_loader.h"
#include "gravel/geo/boundary_nodes.h"
#include "gravel/us/tiger_loader.h"
#include "gravel/geo/border_edges.h"
#include "gravel/geo/graph_coarsening.h"

namespace py = pybind11;
using namespace gravel;

PYBIND11_MODULE(_gravel, m) {
    m.doc() = "Gravel: Graph Routing and Vulnerability Analysis Library";

    // RouteResult
    py::class_<RouteResult>(m, "RouteResult")
        .def_readonly("distance", &RouteResult::distance)
        .def_readonly("path", &RouteResult::path)
        .def("__repr__", [](const RouteResult& r) {
            if (r.distance == INF_WEIGHT) return std::string("RouteResult(no path)");
            return "RouteResult(distance=" + std::to_string(r.distance)
                   + ", path_len=" + std::to_string(r.path.size()) + ")";
        });

    // ValidationReport
    py::class_<ValidationReport>(m, "ValidationReport")
        .def_readonly("passed", &ValidationReport::passed)
        .def_readonly("pairs_tested", &ValidationReport::pairs_tested)
        .def_readonly("mismatches", &ValidationReport::mismatches)
        .def_readonly("max_absolute_error", &ValidationReport::max_absolute_error)
        .def("__repr__", [](const ValidationReport& r) {
            return "ValidationReport(passed=" + std::string(r.passed ? "True" : "False")
                   + ", tested=" + std::to_string(r.pairs_tested)
                   + ", mismatches=" + std::to_string(r.mismatches) + ")";
        });

    // ArrayGraph (exposed as "Graph")
    py::class_<ArrayGraph, std::shared_ptr<ArrayGraph>>(m, "Graph")
        .def(py::init([](py::array_t<uint32_t> py_offsets,
                         py::array_t<uint32_t> py_targets,
                         py::array_t<double> py_weights) {
            auto off = py_offsets.unchecked<1>();
            auto tgt = py_targets.unchecked<1>();
            auto wgt = py_weights.unchecked<1>();

            if (tgt.shape(0) != wgt.shape(0)) {
                throw std::runtime_error("targets and weights must have same length");
            }

            std::vector<uint32_t> offsets(off.shape(0));
            for (py::ssize_t i = 0; i < off.shape(0); ++i) offsets[i] = off(i);

            std::vector<NodeID> targets(tgt.shape(0));
            std::vector<Weight> weights(wgt.shape(0));
            for (py::ssize_t i = 0; i < tgt.shape(0); ++i) {
                targets[i] = tgt(i);
                weights[i] = wgt(i);
            }

            return std::make_shared<ArrayGraph>(std::move(offsets), std::move(targets), std::move(weights));
        }), py::arg("offsets"), py::arg("targets"), py::arg("weights"),
           "Create graph from CSR arrays (offsets, targets, weights)")

        .def_static("from_csv", [](const std::string& path,
                                    const std::string& source_col,
                                    const std::string& target_col,
                                    const std::string& weight_col,
                                    bool bidirectional) -> std::shared_ptr<ArrayGraph> {
            CSVConfig cfg;
            cfg.path = path;
            cfg.source_col = source_col;
            cfg.target_col = target_col;
            cfg.weight_col = weight_col;
            cfg.bidirectional = bidirectional;
            return load_csv_graph(cfg);
        }, py::arg("path"),
           py::arg("source_col") = "source",
           py::arg("target_col") = "target",
           py::arg("weight_col") = "weight",
           py::arg("bidirectional") = false)

        .def_static("from_edges", [](uint32_t num_nodes, py::list edge_list) {
            std::vector<Edge> edges;
            for (auto item : edge_list) {
                auto t = item.cast<py::tuple>();
                edges.push_back({t[0].cast<NodeID>(), t[1].cast<NodeID>(),
                                 t[2].cast<Weight>()});
            }
            return std::make_shared<ArrayGraph>(num_nodes, std::move(edges));
        }, py::arg("num_nodes"), py::arg("edges"),
           "Create from list of (source, target, weight) tuples")

        .def_static("load", [](const std::string& path) -> std::shared_ptr<ArrayGraph> {
            return load_graph(path);
        }, py::arg("path"))

        .def("save", [](const ArrayGraph& g, const std::string& path) {
            save_graph(g, path);
        }, py::arg("path"))

        .def_property_readonly("node_count", &ArrayGraph::node_count)
        .def_property_readonly("edge_count", &ArrayGraph::edge_count)

        .def("__repr__", [](const ArrayGraph& g) {
            return "Graph(nodes=" + std::to_string(g.node_count())
                   + ", edges=" + std::to_string(g.edge_count()) + ")";
        });

    // ContractionResult (exposed as "CH")
    py::class_<ContractionResult>(m, "CH")
        .def_readonly("num_nodes", &ContractionResult::num_nodes)
        .def("save", [](const ContractionResult& ch, const std::string& path) {
            save_ch(ch, path);
        }, py::arg("path"))
        .def_static("load", [](const std::string& path) {
            return load_ch(path);
        }, py::arg("path"))
        .def("__repr__", [](const ContractionResult& ch) {
            return "CH(nodes=" + std::to_string(ch.num_nodes)
                   + ", up_edges=" + std::to_string(ch.up_targets.size())
                   + ", down_edges=" + std::to_string(ch.down_targets.size()) + ")";
        });

    // CHQuery
    py::class_<CHQuery>(m, "CHQuery")
        .def(py::init<const ContractionResult&>(), py::arg("ch"),
             py::keep_alive<1, 2>())  // CHQuery keeps CH alive
        .def("route", static_cast<RouteResult (CHQuery::*)(NodeID, NodeID) const>(&CHQuery::route),
             py::arg("source"), py::arg("target"))
        .def("distance", static_cast<Weight (CHQuery::*)(NodeID, NodeID) const>(&CHQuery::distance),
             py::arg("source"), py::arg("target"))
        .def("distance_matrix", [](const CHQuery& q,
                                    const std::vector<NodeID>& origins,
                                    const std::vector<NodeID>& destinations) {
            auto flat = q.distance_matrix(origins, destinations);
            size_t n_orig = origins.size();
            size_t n_dest = destinations.size();
            py::array_t<double> result({n_orig, n_dest});
            auto buf = result.mutable_unchecked<2>();
            for (size_t i = 0; i < n_orig; ++i) {
                for (size_t j = 0; j < n_dest; ++j) {
                    buf(i, j) = flat[i * n_dest + j];
                }
            }
            return result;
        }, py::arg("origins"), py::arg("destinations"),
           "Compute distance matrix, returns numpy array (origins x destinations)");

    // Free functions
    m.def("build_ch", [](const ArrayGraph& g) {
        return build_ch(g);
    }, py::arg("graph"), "Build contraction hierarchy from graph");

    m.def("route", [](const CHQuery& q, NodeID src, NodeID tgt) {
        return q.route(src, tgt);
    }, py::arg("ch_query"), py::arg("source"), py::arg("target"));

    m.def("validate", [](const ArrayGraph& graph, const CHQuery& query,
                          const std::string& mode, uint32_t pairs) {
        ValidationConfig cfg;
        cfg.mode = (mode == "exhaustive") ? ValidationConfig::EXHAUSTIVE
                                          : ValidationConfig::SAMPLED;
        cfg.sample_count = pairs;
        return validate_ch(graph, query, cfg);
    }, py::arg("graph"), py::arg("ch_query"),
       py::arg("mode") = "sampled", py::arg("pairs") = 10000);

    // Synthetic graph generators (wrap unique_ptr -> shared_ptr)
    m.def("make_grid_graph", [](uint32_t rows, uint32_t cols) -> std::shared_ptr<ArrayGraph> {
        return make_grid_graph(rows, cols);
    }, py::arg("rows"), py::arg("cols"));
    m.def("make_random_graph", [](uint32_t n, uint32_t m, Weight min_w, Weight max_w, uint64_t seed) -> std::shared_ptr<ArrayGraph> {
        return make_random_graph(n, m, min_w, max_w, seed);
    }, py::arg("n"), py::arg("m"),
       py::arg("min_w") = 1.0, py::arg("max_w") = 100.0,
       py::arg("seed") = 42);
    m.def("make_tree_with_bridges", [](uint32_t n, uint32_t extra_edges, uint64_t seed) -> std::shared_ptr<ArrayGraph> {
        return make_tree_with_bridges(n, extra_edges, seed);
    }, py::arg("n"), py::arg("extra_edges"), py::arg("seed") = 42);

    // Reference Dijkstra
    m.def("dijkstra_pair", [](const ArrayGraph& g, NodeID src, NodeID tgt) {
        return dijkstra_pair(g, src, tgt);
    }, py::arg("graph"), py::arg("source"), py::arg("target"));

    // --- Fragility types ---

    py::class_<EdgeFragility>(m, "EdgeFragility")
        .def_readonly("source", &EdgeFragility::source)
        .def_readonly("target", &EdgeFragility::target)
        .def_readonly("replacement_distance", &EdgeFragility::replacement_distance)
        .def_readonly("fragility_ratio", &EdgeFragility::fragility_ratio)
        .def("__repr__", [](const EdgeFragility& ef) {
            return "EdgeFragility(edge=(" + std::to_string(ef.source) + ","
                   + std::to_string(ef.target) + "), ratio="
                   + (std::isinf(ef.fragility_ratio) ? "inf" : std::to_string(ef.fragility_ratio)) + ")";
        });

    py::class_<FragilityResult>(m, "FragilityResult")
        .def_readonly("primary_distance", &FragilityResult::primary_distance)
        .def_readonly("primary_path", &FragilityResult::primary_path)
        .def_readonly("edge_fragilities", &FragilityResult::edge_fragilities)
        .def("valid", &FragilityResult::valid)
        .def("bottleneck_index", &FragilityResult::bottleneck_index)
        .def("bottleneck", &FragilityResult::bottleneck, py::return_value_policy::reference_internal)
        .def("__repr__", [](const FragilityResult& r) {
            if (!r.valid()) return std::string("FragilityResult(no path)");
            return "FragilityResult(distance=" + std::to_string(r.primary_distance)
                   + ", path_len=" + std::to_string(r.primary_path.size())
                   + ", edges=" + std::to_string(r.edge_fragilities.size()) + ")";
        });

    py::class_<AlternateRouteResult>(m, "AlternateRouteResult")
        .def_readonly("distance", &AlternateRouteResult::distance)
        .def_readonly("path", &AlternateRouteResult::path)
        .def_readonly("sharing", &AlternateRouteResult::sharing)
        .def_readonly("stretch", &AlternateRouteResult::stretch)
        .def("__repr__", [](const AlternateRouteResult& a) {
            return "AlternateRouteResult(distance=" + std::to_string(a.distance)
                   + ", stretch=" + std::to_string(a.stretch)
                   + ", sharing=" + std::to_string(a.sharing) + ")";
        });

    // ShortcutIndex
    py::class_<ShortcutIndex>(m, "ShortcutIndex")
        .def(py::init<const ContractionResult&>(), py::arg("ch"),
             py::keep_alive<1, 2>())
        .def("size", &ShortcutIndex::size)
        .def("__repr__", [](const ShortcutIndex& idx) {
            return "ShortcutIndex(original_edges=" + std::to_string(idx.size()) + ")";
        });

    // ViaPathConfig
    py::class_<ViaPathConfig>(m, "ViaPathConfig")
        .def(py::init<>())
        .def_readwrite("max_stretch", &ViaPathConfig::max_stretch)
        .def_readwrite("max_sharing", &ViaPathConfig::max_sharing)
        .def_readwrite("max_alternatives", &ViaPathConfig::max_alternatives);

    // BernsteinConfig
    py::class_<BernsteinConfig>(m, "BernsteinConfig")
        .def(py::init<>())
        .def_readwrite("epsilon", &BernsteinConfig::epsilon);

    // FragilityValidationReport
    py::class_<FragilityValidationReport>(m, "FragilityValidationReport")
        .def_readonly("passed", &FragilityValidationReport::passed)
        .def_readonly("pairs_tested", &FragilityValidationReport::pairs_tested)
        .def_readonly("edges_tested", &FragilityValidationReport::edges_tested)
        .def_readonly("mismatches", &FragilityValidationReport::mismatches)
        .def_readonly("max_absolute_error", &FragilityValidationReport::max_absolute_error)
        .def("__repr__", [](const FragilityValidationReport& r) {
            return "FragilityValidationReport(passed=" + std::string(r.passed ? "True" : "False")
                   + ", pairs=" + std::to_string(r.pairs_tested)
                   + ", edges=" + std::to_string(r.edges_tested)
                   + ", mismatches=" + std::to_string(r.mismatches) + ")";
        });

    // --- Fragility functions ---

    m.def("route_fragility", [](const ContractionResult& ch, const ShortcutIndex& idx,
                                 const ArrayGraph& graph, NodeID source, NodeID target) {
        return route_fragility(ch, idx, graph, source, target);
    }, py::arg("ch"), py::arg("shortcut_index"), py::arg("graph"),
       py::arg("source"), py::arg("target"),
       "Compute edge fragility for every edge on the shortest s-t path");

    m.def("batch_fragility", [](const ContractionResult& ch, const ShortcutIndex& idx,
                                 const ArrayGraph& graph,
                                 const std::vector<std::pair<NodeID, NodeID>>& pairs) {
        return batch_fragility(ch, idx, graph, pairs);
    }, py::arg("ch"), py::arg("shortcut_index"), py::arg("graph"), py::arg("pairs"),
       "Batch fragility for multiple O-D pairs (parallelized with OpenMP)");

    m.def("find_alternative_routes", [](const ContractionResult& ch, NodeID source, NodeID target,
                                         const ViaPathConfig& config) {
        return find_alternative_routes(ch, source, target, config);
    }, py::arg("ch"), py::arg("source"), py::arg("target"),
       py::arg("config") = ViaPathConfig{},
       "Find alternative routes via the via-path method (Abraham et al.)");

    m.def("hershberger_suri", [](const ArrayGraph& graph, NodeID source, NodeID target) {
        return hershberger_suri(graph, source, target);
    }, py::arg("graph"), py::arg("source"), py::arg("target"),
       "Exact replacement paths via Hershberger-Suri SPT sensitivity analysis");

    m.def("bernstein_approx", [](const ArrayGraph& graph, NodeID source, NodeID target,
                                   const BernsteinConfig& config) {
        return bernstein_approx(graph, source, target, config);
    }, py::arg("graph"), py::arg("source"), py::arg("target"),
       py::arg("config") = BernsteinConfig{},
       "(1+epsilon)-approximate replacement paths via Bernstein's method");

    m.def("validate_fragility", [](const ContractionResult& ch, const ShortcutIndex& idx,
                                    const ArrayGraph& graph, uint32_t sample_count, double tolerance) {
        FragilityValidationConfig cfg;
        cfg.sample_count = sample_count;
        cfg.tolerance = tolerance;
        return validate_fragility(ch, idx, graph, cfg);
    }, py::arg("ch"), py::arg("shortcut_index"), py::arg("graph"),
       py::arg("sample_count") = 100, py::arg("tolerance") = 1e-6,
       "Validate route_fragility against naive leave-one-out Dijkstra");

    m.def("validate_shortcut_interaction", [](const ContractionResult& ch, const ShortcutIndex& idx,
                                               const ArrayGraph& graph, uint32_t sample_count, double tolerance) {
        FragilityValidationConfig cfg;
        cfg.sample_count = sample_count;
        cfg.tolerance = tolerance;
        return validate_shortcut_interaction(ch, idx, graph, cfg);
    }, py::arg("ch"), py::arg("shortcut_index"), py::arg("graph"),
       py::arg("sample_count") = 100, py::arg("tolerance") = 1e-6,
       "Validate blocked CH query against reference Dijkstra on uncontracted graph");

    // --- v0.3+v0.4: Network-level analysis ---

    // Polygon / SubgraphResult
    py::class_<Polygon>(m, "Polygon")
        .def(py::init<>())
        .def_readwrite("vertices", &Polygon::vertices);

    py::class_<Coord>(m, "Coord")
        .def(py::init<>())
        .def(py::init<double, double>())
        .def_readwrite("lat", &Coord::lat)
        .def_readwrite("lon", &Coord::lon);

    py::class_<SubgraphResult>(m, "SubgraphResult")
        .def_readonly("graph", &SubgraphResult::graph)
        .def_readonly("new_to_original", &SubgraphResult::new_to_original)
        .def_readonly("original_to_new", &SubgraphResult::original_to_new);

    m.def("extract_subgraph", &extract_subgraph,
          py::arg("graph"), py::arg("boundary"),
          "Extract subgraph within polygon boundary");

    // Algebraic connectivity
    m.def("algebraic_connectivity", &algebraic_connectivity,
          py::arg("graph"), "Compute Fiedler value (second smallest eigenvalue of Laplacian)");

    // Edge betweenness
    py::class_<BetweennessConfig>(m, "BetweennessConfig")
        .def(py::init<>())
        .def_readwrite("sample_sources", &BetweennessConfig::sample_sources)
        .def_readwrite("range_limit", &BetweennessConfig::range_limit)
        .def_readwrite("seed", &BetweennessConfig::seed);

    py::class_<BetweennessResult>(m, "BetweennessResult")
        .def_readonly("edge_scores", &BetweennessResult::edge_scores)
        .def_readonly("sources_used", &BetweennessResult::sources_used);

    m.def("edge_betweenness", &edge_betweenness,
          py::arg("graph"), py::arg("config") = BetweennessConfig{},
          "Compute edge betweenness centrality via Brandes' algorithm");

    // Kirchhoff index
    py::class_<KirchhoffConfig>(m, "KirchhoffConfig")
        .def(py::init<>())
        .def_readwrite("num_probes", &KirchhoffConfig::num_probes)
        .def_readwrite("seed", &KirchhoffConfig::seed);

    m.def("kirchhoff_index", &kirchhoff_index,
          py::arg("graph"), py::arg("config") = KirchhoffConfig{},
          "Compute Kirchhoff index via stochastic trace estimation");

    // Natural connectivity
    m.def("natural_connectivity", &natural_connectivity,
          py::arg("graph"), py::arg("num_probes") = 20,
          py::arg("lanczos_steps") = 50, py::arg("seed") = 42,
          "Compute natural connectivity via stochastic Lanczos quadrature");

    // County fragility
    py::class_<CountyFragilityWeights>(m, "CountyFragilityWeights")
        .def(py::init<>())
        .def_readwrite("bridge_weight", &CountyFragilityWeights::bridge_weight)
        .def_readwrite("connectivity_weight", &CountyFragilityWeights::connectivity_weight)
        .def_readwrite("accessibility_weight", &CountyFragilityWeights::accessibility_weight)
        .def_readwrite("fragility_weight", &CountyFragilityWeights::fragility_weight);

    py::class_<CountyFragilityConfig>(m, "CountyFragilityConfig")
        .def(py::init<>())
        .def_readwrite("boundary", &CountyFragilityConfig::boundary)
        .def_readwrite("betweenness_samples", &CountyFragilityConfig::betweenness_samples)
        .def_readwrite("od_sample_count", &CountyFragilityConfig::od_sample_count)
        .def_readwrite("seed", &CountyFragilityConfig::seed)
        .def_readwrite("weights", &CountyFragilityConfig::weights);

    py::class_<CountyFragilityResult>(m, "CountyFragilityResult")
        .def_readonly("composite_index", &CountyFragilityResult::composite_index)
        .def_readonly("bridges", &CountyFragilityResult::bridges)
        .def_readonly("betweenness", &CountyFragilityResult::betweenness)
        .def_readonly("algebraic_connectivity", &CountyFragilityResult::algebraic_connectivity)
        .def_readonly("kirchhoff_index_value", &CountyFragilityResult::kirchhoff_index_value)
        .def_readonly("sampled_fragilities", &CountyFragilityResult::sampled_fragilities)
        .def_readonly("fragility_p25", &CountyFragilityResult::fragility_p25)
        .def_readonly("fragility_p50", &CountyFragilityResult::fragility_p50)
        .def_readonly("fragility_p75", &CountyFragilityResult::fragility_p75)
        .def_readonly("fragility_p90", &CountyFragilityResult::fragility_p90)
        .def_readonly("fragility_p99", &CountyFragilityResult::fragility_p99)
        .def_readonly("subgraph_nodes", &CountyFragilityResult::subgraph_nodes)
        .def_readonly("subgraph_edges", &CountyFragilityResult::subgraph_edges)
        .def_readonly("entry_point_count", &CountyFragilityResult::entry_point_count);

    m.def("county_fragility_index", [](const ArrayGraph& g, const ContractionResult& ch,
                                        const ShortcutIndex& idx, const CountyFragilityConfig& cfg) {
        return county_fragility_index(g, ch, idx, cfg);
    }, py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
       "Compute county-level fragility index within polygon");

    // AnalysisContext for cached analysis
    py::class_<AnalysisContextConfig>(m, "AnalysisContextConfig")
        .def(py::init<>())
        .def_readwrite("boundary", &AnalysisContextConfig::boundary)
        .def_readwrite("simplify", &AnalysisContextConfig::simplify);

    py::class_<AnalysisContext>(m, "AnalysisContext")
        .def("valid", &AnalysisContext::valid)
        .def_readonly("simplified", &AnalysisContext::simplified);

    m.def("build_analysis_context", &build_analysis_context,
          py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
          "Build cached analysis context for repeated fragility computations");

    m.def("county_fragility_with_context", [](const ArrayGraph& g, const ContractionResult& ch,
                                               const ShortcutIndex& idx, const CountyFragilityConfig& cfg,
                                               const AnalysisContext& ctx) {
        return county_fragility_index(g, ch, idx, cfg, &ctx);
    }, py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"),
       py::arg("config"), py::arg("context"),
       "County fragility using pre-built analysis context");

    // BridgeResult (already used in county fragility)
    py::class_<BridgeResult>(m, "BridgeResult")
        .def_readonly("bridges", &BridgeResult::bridges);

    // Landmarks
    py::class_<LandmarkData>(m, "LandmarkData")
        .def_readonly("num_landmarks", &LandmarkData::num_landmarks)
        .def("lower_bound", &LandmarkData::lower_bound,
             py::arg("source"), py::arg("target"));

    m.def("precompute_landmarks", &precompute_landmarks,
          py::arg("graph"), py::arg("num_landmarks") = 16, py::arg("seed") = 42,
          "Precompute landmark distances for ALT lower bounds");

    // Filtered fragility
    py::class_<FilterConfig>(m, "FilterConfig")
        .def(py::init<>())
        .def_readwrite("ch_level_percentile", &FilterConfig::ch_level_percentile)
        .def_readwrite("skip_ratio_threshold", &FilterConfig::skip_ratio_threshold)
        .def_readwrite("use_ch_level_filter", &FilterConfig::use_ch_level_filter)
        .def_readwrite("use_alt_filter", &FilterConfig::use_alt_filter);

    py::class_<FilteredFragilityResult, FragilityResult>(m, "FilteredFragilityResult")
        .def_readonly("edges_screened", &FilteredFragilityResult::edges_screened)
        .def_readonly("edges_computed", &FilteredFragilityResult::edges_computed);

    m.def("filtered_route_fragility", &filtered_route_fragility,
          py::arg("ch"), py::arg("shortcut_index"), py::arg("graph"),
          py::arg("landmarks"), py::arg("source"), py::arg("target"),
          py::arg("config") = FilterConfig{},
          "Route fragility with filter-then-verify pipeline");

    // O-D sampling
    py::class_<SamplingConfig>(m, "SamplingConfig")
        .def(py::init<>())
        .def_readwrite("total_samples", &SamplingConfig::total_samples)
        .def_readwrite("distance_strata", &SamplingConfig::distance_strata)
        .def_readwrite("long_distance_weight", &SamplingConfig::long_distance_weight)
        .def_readwrite("long_distance_threshold", &SamplingConfig::long_distance_threshold)
        .def_readwrite("seed", &SamplingConfig::seed);

    m.def("stratified_sample", &stratified_sample,
          py::arg("graph"), py::arg("ch"), py::arg("config") = SamplingConfig{},
          "Generate stratified O-D pairs for network-level fragility analysis");

    // CHBuildConfig — expose parallel flag
    py::class_<CHBuildConfig>(m, "CHBuildConfig")
        .def(py::init<>())
        .def_readwrite("parallel", &CHBuildConfig::parallel)
        .def_readwrite("batch_size", &CHBuildConfig::batch_size);

    m.def("build_ch_with_config", [](const ArrayGraph& g, const CHBuildConfig& config) {
        return build_ch(g, config);
    }, py::arg("graph"), py::arg("config"),
       "Build CH with custom configuration");

    // --- v0.5: Location Fragility ---

    // SelectionStrategy must be registered before LocationFragilityConfig which uses it
    py::enum_<SelectionStrategy>(m, "SelectionStrategy")
        .value("GREEDY_FRAGILITY", SelectionStrategy::GREEDY_FRAGILITY)
        .value("GREEDY_BETWEENNESS", SelectionStrategy::GREEDY_BETWEENNESS)
        .value("MONTE_CARLO", SelectionStrategy::MONTE_CARLO);

    py::class_<LocationFragilityConfig>(m, "LocationFragilityConfig")
        .def(py::init<>())
        .def_readwrite("center", &LocationFragilityConfig::center)
        .def_readwrite("radius_meters", &LocationFragilityConfig::radius_meters)
        .def_readwrite("angular_bins", &LocationFragilityConfig::angular_bins)
        .def_readwrite("removal_fraction", &LocationFragilityConfig::removal_fraction)
        .def_readwrite("sample_count", &LocationFragilityConfig::sample_count)
        .def_readwrite("strategy", &LocationFragilityConfig::strategy)
        .def_readwrite("monte_carlo_runs", &LocationFragilityConfig::monte_carlo_runs)
        .def_readwrite("betweenness_samples", &LocationFragilityConfig::betweenness_samples)
        .def_readwrite("seed", &LocationFragilityConfig::seed);

    py::class_<LocationKLevel>(m, "LocationKLevel")
        .def_readonly("k", &LocationKLevel::k)
        .def_readonly("mean_isolation_risk", &LocationKLevel::mean_isolation_risk)
        .def_readonly("std_isolation_risk", &LocationKLevel::std_isolation_risk)
        .def_readonly("p50", &LocationKLevel::p50)
        .def_readonly("p90", &LocationKLevel::p90)
        .def_readonly("mean_disconnected_frac", &LocationKLevel::mean_disconnected_frac)
        .def_readonly("mean_distance_inflation", &LocationKLevel::mean_distance_inflation)
        .def_readonly("run_values", &LocationKLevel::run_values);

    py::class_<LocationFragilityResult>(m, "LocationFragilityResult")
        .def_readonly("isolation_risk", &LocationFragilityResult::isolation_risk)
        .def_readonly("curve", &LocationFragilityResult::curve)
        .def_readonly("auc_normalized", &LocationFragilityResult::auc_normalized)
        .def_readonly("baseline_isolation_risk", &LocationFragilityResult::baseline_isolation_risk)
        .def_readonly("directional_coverage", &LocationFragilityResult::directional_coverage)
        .def_readonly("directional_fragility", &LocationFragilityResult::directional_fragility)
        .def_readonly("directional_asymmetry", &LocationFragilityResult::directional_asymmetry)
        .def_readonly("removal_sequence", &LocationFragilityResult::removal_sequence)
        .def_readonly("strategy_used", &LocationFragilityResult::strategy_used)
        .def_readonly("reachable_nodes", &LocationFragilityResult::reachable_nodes)
        .def_readonly("sp_edges_total", &LocationFragilityResult::sp_edges_total)
        .def_readonly("sp_edges_removed", &LocationFragilityResult::sp_edges_removed)
        .def_readonly("subgraph_nodes", &LocationFragilityResult::subgraph_nodes)
        .def_readonly("subgraph_edges", &LocationFragilityResult::subgraph_edges)
        .def("__repr__", [](const LocationFragilityResult& r) {
            return "LocationFragilityResult(isolation_risk=" + std::to_string(r.isolation_risk)
                   + ", reachable=" + std::to_string(r.reachable_nodes)
                   + ", sp_edges=" + std::to_string(r.sp_edges_total) + ")";
        });

    m.def("location_fragility", &location_fragility,
          py::arg("graph"), py::arg("ch"), py::arg("config"),
          "Compute isolation risk for a location using Dijkstra + incremental SSSP");

    // --- v0.6: Elevation + Closure Risk ---

    py::class_<ElevationData>(m, "ElevationData")
        .def_readonly("node_elevation", &ElevationData::node_elevation)
        .def("has_elevation", &ElevationData::has_elevation, py::arg("node"))
        .def("edge_max_elevation", &ElevationData::edge_max_elevation,
             py::arg("u"), py::arg("v"));

    m.def("elevation_from_array", &elevation_from_array, py::arg("elevations"),
          "Create ElevationData from a vector of per-node elevations");

    m.def("load_srtm_elevation", &load_srtm_elevation,
          py::arg("graph"), py::arg("srtm_dir"),
          "Load SRTM HGT elevation data for graph nodes");

    m.def("save_elevation", &save_elevation, py::arg("elevation"), py::arg("path"));
    m.def("load_elevation", &load_elevation, py::arg("path"));

    py::enum_<ClosureRiskTier>(m, "ClosureRiskTier")
        .value("LOW", ClosureRiskTier::LOW)
        .value("MODERATE", ClosureRiskTier::MODERATE)
        .value("HIGH", ClosureRiskTier::HIGH)
        .value("SEVERE", ClosureRiskTier::SEVERE);

    py::class_<ClosureRiskConfig>(m, "ClosureRiskConfig")
        .def(py::init<>())
        .def_readwrite("moderate_elevation", &ClosureRiskConfig::moderate_elevation)
        .def_readwrite("high_elevation", &ClosureRiskConfig::high_elevation)
        .def_readwrite("severe_elevation", &ClosureRiskConfig::severe_elevation)
        .def_static("defaults", &ClosureRiskConfig::defaults);

    py::class_<ClosureRiskData>(m, "ClosureRiskData")
        .def_readonly("edge_tiers", &ClosureRiskData::edge_tiers)
        .def("tier_fraction", &ClosureRiskData::tier_fraction, py::arg("tier"));

    m.def("classify_closure_risk", &classify_closure_risk,
          py::arg("graph"), py::arg("elevation"),
          py::arg("edge_labels") = std::vector<std::string>{},
          py::arg("config") = ClosureRiskConfig::defaults(),
          "Classify closure risk tier for every edge");

    m.def("seasonal_weight_multipliers", &seasonal_weight_multipliers,
          py::arg("risk"),
          py::arg("tier1_multiplier") = 1.0,
          py::arg("tier2_multiplier") = 1.5,
          py::arg("tier3_multiplier") = 3.0,
          "Compute seasonal weight multipliers from closure risk tiers");

    // --- v0.7: Snap quality report ---

    py::class_<SnapQualityReport>(m, "SnapQualityReport")
        .def_readonly("total", &SnapQualityReport::total)
        .def_readonly("succeeded", &SnapQualityReport::succeeded)
        .def_readonly("failed", &SnapQualityReport::failed)
        .def_readonly("exact_node", &SnapQualityReport::exact_node)
        .def_readonly("warned", &SnapQualityReport::warned)
        .def_readonly("p50_distance_m", &SnapQualityReport::p50_distance_m)
        .def_readonly("p90_distance_m", &SnapQualityReport::p90_distance_m)
        .def_readonly("p95_distance_m", &SnapQualityReport::p95_distance_m)
        .def_readonly("p99_distance_m", &SnapQualityReport::p99_distance_m)
        .def_readonly("max_distance_m", &SnapQualityReport::max_distance_m);

    m.def("snap_quality", &snap_quality,
          py::arg("results"), py::arg("warn_threshold_m") = 200.0,
          "Generate snap quality report from batch results");

    // --- Graph Simplification ---

    py::class_<DegradationReport::StageReport>(m, "StageReport")
        .def_readonly("stage_name", &DegradationReport::StageReport::stage_name)
        .def_readonly("nodes_before", &DegradationReport::StageReport::nodes_before)
        .def_readonly("nodes_after", &DegradationReport::StageReport::nodes_after)
        .def_readonly("edges_before", &DegradationReport::StageReport::edges_before)
        .def_readonly("edges_after", &DegradationReport::StageReport::edges_after);

    py::class_<DegradationReport>(m, "DegradationReport")
        .def_readonly("od_pairs_sampled", &DegradationReport::od_pairs_sampled)
        .def_readonly("max_stretch", &DegradationReport::max_stretch)
        .def_readonly("p99_stretch", &DegradationReport::p99_stretch)
        .def_readonly("p95_stretch", &DegradationReport::p95_stretch)
        .def_readonly("p90_stretch", &DegradationReport::p90_stretch)
        .def_readonly("median_stretch", &DegradationReport::median_stretch)
        .def_readonly("mean_stretch", &DegradationReport::mean_stretch)
        .def_readonly("pairs_connected_before", &DegradationReport::pairs_connected_before)
        .def_readonly("pairs_connected_after", &DegradationReport::pairs_connected_after)
        .def_readonly("pairs_disconnected", &DegradationReport::pairs_disconnected)
        .def_readonly("connectivity_ratio", &DegradationReport::connectivity_ratio)
        .def_readonly("original_bridges", &DegradationReport::original_bridges)
        .def_readonly("preserved_bridges", &DegradationReport::preserved_bridges)
        .def_readonly("all_bridges_preserved", &DegradationReport::all_bridges_preserved)
        .def_readonly("stages", &DegradationReport::stages);

    py::class_<SimplificationConfig>(m, "SimplificationConfig")
        .def(py::init<>())
        .def_readwrite("contract_degree2", &SimplificationConfig::contract_degree2)
        .def_readwrite("ch_level_keep_fraction", &SimplificationConfig::ch_level_keep_fraction)
        .def_readwrite("preserve_bridges", &SimplificationConfig::preserve_bridges)
        .def_readwrite("estimate_degradation", &SimplificationConfig::estimate_degradation)
        .def_readwrite("degradation_samples", &SimplificationConfig::degradation_samples)
        .def_readwrite("seed", &SimplificationConfig::seed);

    py::class_<SimplificationResult>(m, "SimplificationResult")
        .def_readonly("graph", &SimplificationResult::graph)
        .def_readonly("new_to_original", &SimplificationResult::new_to_original)
        .def_readonly("original_nodes", &SimplificationResult::original_nodes)
        .def_readonly("original_edges", &SimplificationResult::original_edges)
        .def_readonly("simplified_nodes", &SimplificationResult::simplified_nodes)
        .def_readonly("simplified_edges", &SimplificationResult::simplified_edges)
        .def_readonly("degradation", &SimplificationResult::degradation)
        .def("__repr__", [](const SimplificationResult& r) {
            return "SimplificationResult(nodes=" + std::to_string(r.original_nodes)
                   + " -> " + std::to_string(r.simplified_nodes)
                   + ", edges=" + std::to_string(r.original_edges)
                   + " -> " + std::to_string(r.simplified_edges) + ")";
        });

    m.def("simplify_graph", &simplify_graph,
          py::arg("graph"),
          py::arg("ch") = nullptr,
          py::arg("shortcut_index") = nullptr,
          py::arg("config") = SimplificationConfig{},
          "Simplify graph with configurable pruning and degradation estimation");

    // Edge labels for category filtering
    py::class_<EdgeCategoryLabels>(m, "EdgeCategoryLabels")
        .def(py::init<>())
        .def_readwrite("categories", &EdgeCategoryLabels::categories)
        .def_static("from_strings", &EdgeCategoryLabels::from_strings,
                     py::arg("labels"), py::arg("rank_map"), py::arg("default_rank") = 255)
        .def_static("osm_road_ranks", &EdgeCategoryLabels::osm_road_ranks);

    m.def("make_category_filter", &make_category_filter,
          py::arg("labels"), py::arg("max_category"),
          "Create edge filter predicate from category labels");

    // --- Bridge Classification ---

    py::enum_<BridgeType>(m, "BridgeType")
        .value("STRUCTURAL", BridgeType::STRUCTURAL)
        .value("FILTER_INDUCED", BridgeType::FILTER_INDUCED);

    py::class_<BridgeClassification>(m, "BridgeClassification")
        .def_readonly("bridges", &BridgeClassification::bridges)
        .def_readonly("types", &BridgeClassification::types)
        .def_readonly("structural_count", &BridgeClassification::structural_count)
        .def_readonly("filter_induced_count", &BridgeClassification::filter_induced_count);

    m.def("classify_bridges", &classify_bridges,
          py::arg("graph"), py::arg("edge_filter"),
          "Classify bridges as structural or filter-induced");

    // --- Scenario Fragility ---

    py::class_<ScenarioConfig>(m, "ScenarioConfig")
        .def(py::init<>())
        .def_readwrite("baseline", &ScenarioConfig::baseline)
        .def_readwrite("blocked_edges", &ScenarioConfig::blocked_edges)
        .def_readwrite("hazard_footprint", &ScenarioConfig::hazard_footprint);

    py::class_<ScenarioResult>(m, "ScenarioResult")
        .def_readonly("baseline", &ScenarioResult::baseline)
        .def_readonly("scenario", &ScenarioResult::scenario)
        .def_readonly("delta_composite", &ScenarioResult::delta_composite)
        .def_readonly("relative_change", &ScenarioResult::relative_change)
        .def_readonly("edges_blocked", &ScenarioResult::edges_blocked)
        .def_readonly("bridges_blocked", &ScenarioResult::bridges_blocked);

    m.def("scenario_fragility", &scenario_fragility,
          py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
          "Compare baseline vs scenario fragility with blocked edges");

    m.def("edges_in_polygon", &edges_in_polygon,
          py::arg("graph"), py::arg("polygon"),
          "Find graph edges within a polygon (for hazard footprint conversion)");

    // --- Uncertainty Quantification ---

    py::class_<EnsembleConfig>(m, "EnsembleConfig")
        .def(py::init<>())
        .def_readwrite("base_config", &EnsembleConfig::base_config)
        .def_readwrite("num_runs", &EnsembleConfig::num_runs)
        .def_readwrite("base_seed", &EnsembleConfig::base_seed);

    py::class_<UncertaintyResult>(m, "UncertaintyResult")
        .def_readonly("ensemble", &UncertaintyResult::ensemble)
        .def_readonly("mean_composite", &UncertaintyResult::mean_composite)
        .def_readonly("std_composite", &UncertaintyResult::std_composite)
        .def_readonly("min_composite", &UncertaintyResult::min_composite)
        .def_readonly("max_composite", &UncertaintyResult::max_composite)
        .def_readonly("composite_p25", &UncertaintyResult::composite_p25)
        .def_readonly("composite_p50", &UncertaintyResult::composite_p50)
        .def_readonly("composite_p75", &UncertaintyResult::composite_p75)
        .def_readonly("coefficient_of_variation", &UncertaintyResult::coefficient_of_variation);

    m.def("ensemble_fragility", &ensemble_fragility,
          py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
          "Run county_fragility_index N times with different seeds for variance estimation");

    py::class_<WeightSensitivityConfig>(m, "WeightSensitivityConfig")
        .def(py::init<>())
        .def_readwrite("base_config", &WeightSensitivityConfig::base_config)
        .def_readwrite("perturbation_range", &WeightSensitivityConfig::perturbation_range)
        .def_readwrite("grid_points", &WeightSensitivityConfig::grid_points);

    py::class_<WeightSensitivityResult>(m, "WeightSensitivityResult")
        .def_readonly("sensitivity_bridge", &WeightSensitivityResult::sensitivity_bridge)
        .def_readonly("sensitivity_connectivity", &WeightSensitivityResult::sensitivity_connectivity)
        .def_readonly("sensitivity_accessibility", &WeightSensitivityResult::sensitivity_accessibility)
        .def_readonly("sensitivity_fragility", &WeightSensitivityResult::sensitivity_fragility)
        .def_readonly("composite_min", &WeightSensitivityResult::composite_min)
        .def_readonly("composite_max", &WeightSensitivityResult::composite_max)
        .def_readonly("weight_grid", &WeightSensitivityResult::weight_grid)
        .def_readonly("composite_values", &WeightSensitivityResult::composite_values);

    m.def("weight_sensitivity", &weight_sensitivity,
          py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
          "One-at-a-time weight perturbation analysis for composite index stability");

    // --- Edge Confidence ---

    py::class_<EdgeConfidence>(m, "EdgeConfidence")
        .def_readonly("scores", &EdgeConfidence::scores)
        .def("weight_multiplier", &EdgeConfidence::weight_multiplier, py::arg("edge_index"));

    m.def("estimate_osm_confidence", &estimate_osm_confidence,
          py::arg("graph"), py::arg("metadata"),
          "Estimate per-edge confidence from OSM metadata heuristics");

    m.def("confidence_from_array", &confidence_from_array,
          py::arg("values"), "Create EdgeConfidence from a flat array");

    // --- Tiled Fragility ---

    py::class_<TileConfig>(m, "TileConfig")
        .def(py::init<>())
        .def_readwrite("min_lat", &TileConfig::min_lat)
        .def_readwrite("max_lat", &TileConfig::max_lat)
        .def_readwrite("min_lon", &TileConfig::min_lon)
        .def_readwrite("max_lon", &TileConfig::max_lon)
        .def_readwrite("tile_size_meters", &TileConfig::tile_size_meters)
        .def_readwrite("location_config", &TileConfig::location_config);

    py::class_<TileResult>(m, "TileResult")
        .def_readonly("center", &TileResult::center)
        .def_readonly("fragility", &TileResult::fragility);

    py::class_<TiledFragilityResult>(m, "TiledFragilityResult")
        .def_readonly("tiles", &TiledFragilityResult::tiles)
        .def_readonly("rows", &TiledFragilityResult::rows)
        .def_readonly("cols", &TiledFragilityResult::cols)
        .def_readonly("mean_isolation_risk", &TiledFragilityResult::mean_isolation_risk)
        .def_readonly("max_isolation_risk", &TiledFragilityResult::max_isolation_risk)
        .def_readonly("min_isolation_risk", &TiledFragilityResult::min_isolation_risk)
        .def_readonly("max_risk_location", &TiledFragilityResult::max_risk_location);

    m.def("tiled_fragility_analysis", &tiled_fragility_analysis,
          py::arg("graph"), py::arg("ch"), py::arg("config"),
          "Run location_fragility across a spatial grid for fragility field mapping");

    // --- Progressive Elimination Fragility ---

    py::enum_<RecomputeMode>(m, "RecomputeMode")
        .value("FULL_RECOMPUTE", RecomputeMode::FULL_RECOMPUTE)
        .value("FAST_APPROXIMATE", RecomputeMode::FAST_APPROXIMATE);

    py::class_<ProgressiveFragilityConfig>(m, "ProgressiveFragilityConfig")
        .def(py::init<>())
        .def_readwrite("base_config", &ProgressiveFragilityConfig::base_config)
        .def_readwrite("selection_strategy", &ProgressiveFragilityConfig::selection_strategy)
        .def_readwrite("k_max", &ProgressiveFragilityConfig::k_max)
        .def_readwrite("k_max_fraction", &ProgressiveFragilityConfig::k_max_fraction)
        .def_readwrite("monte_carlo_runs", &ProgressiveFragilityConfig::monte_carlo_runs)
        .def_readwrite("base_seed", &ProgressiveFragilityConfig::base_seed)
        .def_readwrite("recompute_mode", &ProgressiveFragilityConfig::recompute_mode)
        .def_readwrite("critical_k_threshold", &ProgressiveFragilityConfig::critical_k_threshold)
        .def_readwrite("jump_detection_delta", &ProgressiveFragilityConfig::jump_detection_delta);

    py::class_<KLevelResult>(m, "KLevelResult")
        .def_readonly("k", &KLevelResult::k)
        .def_readonly("mean_composite", &KLevelResult::mean_composite)
        .def_readonly("std_composite", &KLevelResult::std_composite)
        .def_readonly("p25", &KLevelResult::p25)
        .def_readonly("p50", &KLevelResult::p50)
        .def_readonly("p75", &KLevelResult::p75)
        .def_readonly("p90", &KLevelResult::p90)
        .def_readonly("iqr", &KLevelResult::iqr)
        .def_readonly("fraction_disconnected", &KLevelResult::fraction_disconnected)
        .def_readonly("run_values", &KLevelResult::run_values)
        .def_readonly("removed_edge", &KLevelResult::removed_edge);

    py::class_<ProgressiveFragilityResult>(m, "ProgressiveFragilityResult")
        .def_readonly("curve", &ProgressiveFragilityResult::curve)
        .def_readonly("auc_raw", &ProgressiveFragilityResult::auc_raw)
        .def_readonly("auc_normalized", &ProgressiveFragilityResult::auc_normalized)
        .def_readonly("auc_excess", &ProgressiveFragilityResult::auc_excess)
        .def_readonly("critical_k", &ProgressiveFragilityResult::critical_k)
        .def_readonly("jump_detected", &ProgressiveFragilityResult::jump_detected)
        .def_readonly("jump_at_k", &ProgressiveFragilityResult::jump_at_k)
        .def_readonly("jump_magnitude", &ProgressiveFragilityResult::jump_magnitude)
        .def_readonly("removal_sequence", &ProgressiveFragilityResult::removal_sequence)
        .def_readonly("strategy_used", &ProgressiveFragilityResult::strategy_used)
        .def_readonly("subgraph_nodes", &ProgressiveFragilityResult::subgraph_nodes)
        .def_readonly("subgraph_edges", &ProgressiveFragilityResult::subgraph_edges)
        .def_readonly("k_max_used", &ProgressiveFragilityResult::k_max_used);

    m.def("progressive_fragility", &progressive_fragility,
          py::arg("graph"), py::arg("ch"), py::arg("shortcut_index"), py::arg("config"),
          "Progressive elimination fragility: degradation curve under sequential edge removal");

    // --- EdgeSampler ---

    py::enum_<SamplingStrategy>(m, "SamplingStrategy")
        .value("UNIFORM_RANDOM", SamplingStrategy::UNIFORM_RANDOM)
        .value("STRATIFIED_BY_CLASS", SamplingStrategy::STRATIFIED_BY_CLASS)
        .value("IMPORTANCE_WEIGHTED", SamplingStrategy::IMPORTANCE_WEIGHTED)
        .value("SPATIALLY_STRATIFIED", SamplingStrategy::SPATIALLY_STRATIFIED)
        .value("CLUSTER_DISPERSED", SamplingStrategy::CLUSTER_DISPERSED);

    py::class_<SamplerConfig>(m, "SamplerConfig")
        .def(py::init<>())
        .def_readwrite("strategy", &SamplerConfig::strategy)
        .def_readwrite("target_count", &SamplerConfig::target_count)
        .def_readwrite("weights", &SamplerConfig::weights)
        .def_readwrite("proportional", &SamplerConfig::proportional)
        .def_readwrite("grid_rows", &SamplerConfig::grid_rows)
        .def_readwrite("grid_cols", &SamplerConfig::grid_cols)
        .def_readwrite("min_spacing_m", &SamplerConfig::min_spacing_m)
        .def_readwrite("seed", &SamplerConfig::seed);

    py::class_<EdgeSampler>(m, "EdgeSampler")
        .def(py::init<const ArrayGraph&>(), py::arg("graph"))
        .def("sample", &EdgeSampler::sample, py::arg("config"))
        .def("sample_pairs", &EdgeSampler::sample_pairs, py::arg("config"));

    // --- RegionAssignment ---

    py::class_<AssignmentConfig>(m, "AssignmentConfig")
        .def(py::init<>())
        .def_readwrite("seed", &AssignmentConfig::seed);

    py::class_<RegionSpec>(m, "RegionSpec")
        .def(py::init<>())
        .def_readwrite("region_id", &RegionSpec::region_id)
        .def_readwrite("label", &RegionSpec::label)
        .def_readwrite("boundary", &RegionSpec::boundary);

    py::class_<RegionAssignment>(m, "RegionAssignment")
        .def_readonly("region_index", &RegionAssignment::region_index)
        .def_readonly("regions", &RegionAssignment::regions)
        .def_readonly("region_node_counts", &RegionAssignment::region_node_counts)
        .def_readonly("unassigned_count", &RegionAssignment::unassigned_count)
        .def("region_id_of", &RegionAssignment::region_id_of, py::arg("node"));

    py::class_<GeoJSONLoadConfig>(m, "GeoJSONLoadConfig")
        .def(py::init<>())
        .def_readwrite("region_id_property", &GeoJSONLoadConfig::region_id_property)
        .def_readwrite("label_property", &GeoJSONLoadConfig::label_property);

    m.def("assign_nodes_to_regions", &assign_nodes_to_regions,
          py::arg("graph"), py::arg("regions"), py::arg("config") = AssignmentConfig{},
          "Assign graph nodes to geographic boundary regions");

    m.def("load_regions_geojson", &load_regions_geojson,
          py::arg("geojson_path"), py::arg("config") = GeoJSONLoadConfig{},
          "Load boundary regions from a GeoJSON file");

    m.def("boundary_nodes", &boundary_nodes,
          py::arg("graph"), py::arg("assignment"),
          "Find nodes at region boundaries");

    // --- TIGER loaders ---

    m.def("load_tiger_counties", &load_tiger_counties, py::arg("geojson_path"),
          "Load TIGER county boundaries (GEOID/NAMELSAD)");
    m.def("load_tiger_states", &load_tiger_states, py::arg("geojson_path"),
          "Load TIGER state boundaries (STATEFP/NAME)");
    m.def("load_tiger_cbsas", &load_tiger_cbsas, py::arg("geojson_path"),
          "Load TIGER CBSA boundaries (CBSAFP/NAME)");
    m.def("load_tiger_places", &load_tiger_places, py::arg("geojson_path"),
          "Load TIGER place boundaries (GEOID/NAMELSAD)");
    m.def("load_tiger_urban_areas", &load_tiger_urban_areas, py::arg("geojson_path"),
          "Load TIGER urban area boundaries (UACE10/NAME10)");

    // --- Border Edges ---

    py::class_<RegionPair>(m, "RegionPair")
        .def(py::init<>())
        .def_readwrite("region_a", &RegionPair::region_a)
        .def_readwrite("region_b", &RegionPair::region_b);

    py::class_<BorderEdgeSummary>(m, "BorderEdgeSummary")
        .def_readonly("regions", &BorderEdgeSummary::regions)
        .def_readonly("edge_count", &BorderEdgeSummary::edge_count)
        .def_readonly("total_weight", &BorderEdgeSummary::total_weight)
        .def_readonly("min_weight", &BorderEdgeSummary::min_weight)
        .def_readonly("max_weight", &BorderEdgeSummary::max_weight);

    py::class_<BorderEdgeResult>(m, "BorderEdgeResult")
        .def_readonly("total_border_edges", &BorderEdgeResult::total_border_edges)
        .def_readonly("connected_pairs", &BorderEdgeResult::connected_pairs)
        .def_readonly("unassigned_edges", &BorderEdgeResult::unassigned_edges);

    m.def("summarize_border_edges", &summarize_border_edges,
          py::arg("graph"), py::arg("assignment"),
          "Summarize edges crossing region boundaries");

    // --- Graph Coarsening ---

    py::class_<CoarseningConfig>(m, "CoarseningConfig")
        .def(py::init<>())
        .def_readwrite("compute_centroids", &CoarseningConfig::compute_centroids)
        .def_readwrite("min_border_edges", &CoarseningConfig::min_border_edges);

    py::class_<CoarseningResult>(m, "CoarseningResult")
        .def_property_readonly("graph", [](const CoarseningResult& r) -> const ArrayGraph* {
            return r.graph.get();
        }, py::return_value_policy::reference_internal)
        .def_readonly("region_labels", &CoarseningResult::region_labels)
        .def_readonly("region_ids", &CoarseningResult::region_ids)
        .def_readonly("node_counts", &CoarseningResult::node_counts)
        .def_readonly("internal_edge_counts", &CoarseningResult::internal_edge_counts);

    m.def("coarsen_graph", &coarsen_graph,
          py::arg("graph"), py::arg("assignment"), py::arg("border_edges"),
          py::arg("config") = CoarseningConfig{},
          "Coarsen graph by collapsing regions to single nodes");

}
