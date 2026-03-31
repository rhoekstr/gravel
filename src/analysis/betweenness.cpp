#include "gravel/analysis/betweenness.h"
#include <vector>
#include <queue>
#include <stack>
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace gravel {

BetweennessResult edge_betweenness(const ArrayGraph& graph, BetweennessConfig config) {
    NodeID n = graph.node_count();
    EdgeID m = graph.edge_count();
    BetweennessResult result;
    result.edge_scores.resize(m, 0.0);

    if (n == 0) return result;

    // Select source nodes
    std::vector<NodeID> sources;
    if (config.sample_sources == 0 || config.sample_sources >= n) {
        sources.resize(n);
        std::iota(sources.begin(), sources.end(), 0);
        result.sources_used = n;
    } else {
        sources.resize(n);
        std::iota(sources.begin(), sources.end(), 0);
        std::mt19937_64 rng(config.seed);
        std::shuffle(sources.begin(), sources.end(), rng);
        sources.resize(config.sample_sources);
        result.sources_used = config.sample_sources;
    }

    int num_sources = static_cast<int>(sources.size());

    #pragma omp parallel if(num_sources > 4)
    {
        // Per-thread workspace
        std::vector<double> dist(n);
        std::vector<double> sigma(n);
        std::vector<double> delta(n);
        std::vector<std::vector<NodeID>> pred(n);
        std::vector<double> local_scores(m, 0.0);

        #pragma omp for schedule(dynamic)
        for (int si = 0; si < num_sources; ++si) {
            NodeID s = sources[si];

            std::fill(dist.begin(), dist.end(), std::numeric_limits<double>::infinity());
            std::fill(sigma.begin(), sigma.end(), 0.0);
            std::fill(delta.begin(), delta.end(), 0.0);
            for (auto& p : pred) p.clear();

            dist[s] = 0.0;
            sigma[s] = 1.0;
            std::stack<NodeID> order;

            // Dijkstra
            using PQItem = std::pair<double, NodeID>;
            std::priority_queue<PQItem, std::vector<PQItem>, std::greater<>> pq;
            pq.push({0.0, s});

            while (!pq.empty()) {
                auto [d, u] = pq.top();
                pq.pop();
                if (d > dist[u]) continue;
                if (config.range_limit > 0.0 && d > config.range_limit) continue;
                order.push(u);

                auto targets = graph.outgoing_targets(u);
                auto weights = graph.outgoing_weights(u);
                for (size_t i = 0; i < targets.size(); ++i) {
                    NodeID v = targets[i];
                    double new_dist = dist[u] + weights[i];
                    if (new_dist < dist[v]) {
                        dist[v] = new_dist;
                        sigma[v] = sigma[u];
                        pred[v].clear();
                        pred[v].push_back(u);
                        pq.push({new_dist, v});
                    } else if (std::abs(new_dist - dist[v]) < 1e-10) {
                        sigma[v] += sigma[u];
                        pred[v].push_back(u);
                    }
                }
            }

            // Back-propagation
            while (!order.empty()) {
                NodeID w = order.top();
                order.pop();
                for (NodeID v : pred[w]) {
                    double coeff = (sigma[v] / sigma[w]) * (1.0 + delta[w]);
                    delta[v] += coeff;

                    auto targets = graph.outgoing_targets(v);
                    uint32_t base = graph.raw_offsets()[v];
                    for (size_t i = 0; i < targets.size(); ++i) {
                        if (targets[i] == w) {
                            local_scores[base + i] += coeff;
                            break;
                        }
                    }
                }
            }
        }

        // Reduce per-thread scores into result
        #pragma omp critical
        {
            for (EdgeID e = 0; e < m; ++e) {
                result.edge_scores[e] += local_scores[e];
            }
        }
    }

    // Scale if sampling
    if (config.sample_sources > 0 && config.sample_sources < n) {
        double scale = static_cast<double>(n) / config.sample_sources;
        for (auto& s : result.edge_scores) s *= scale;
    }

    return result;
}

}  // namespace gravel
