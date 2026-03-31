#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/snap/snapper.h"
#include <iostream>
#include <nlohmann/json.hpp>

int cmd_snap(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph") || !args.has("lat") || !args.has("lon")) {
        std::cerr << "Snap a geographic coordinate to the nearest road edge.\n\n"
                  << "Usage: gravel snap --graph <.gravel.meta> --lat <lat> --lon <lon>\n"
                  << "                  [--max-distance <meters>]\n\n"
                  << "Options:\n"
                  << "  --max-distance  Maximum snap distance in meters (default: 500)\n\n"
                  << "Output: JSON with snapped coordinate, edge, distance, and t parameter.\n";
        return args.wants_help() ? 0 : 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    gravel::Snapper snapper(*graph);

    double lat = std::stod(args.get("lat"));
    double lon = std::stod(args.get("lon"));
    double max_dist = args.has("max-distance") ? std::stod(args.get("max-distance")) : 500.0;

    auto result = snapper.snap({lat, lon}, max_dist);

    nlohmann::json j;
    j["input_lat"] = lat;
    j["input_lon"] = lon;

    if (result.valid()) {
        j["snapped_lat"] = result.snapped_coord.lat;
        j["snapped_lon"] = result.snapped_coord.lon;
        j["snap_distance_meters"] = result.snap_distance_m;
        j["edge_source"] = result.edge_source;
        j["edge_target"] = result.edge_target;
        j["t"] = result.t;
        j["is_exact_node"] = result.is_exact_node;
        j["dist_to_source"] = result.dist_to_source;
        j["dist_to_target"] = result.dist_to_target;
    } else {
        j["error"] = "no edge within " + std::to_string(max_dist) + " meters";
    }

    std::cout << j.dump(2) << "\n";
    return 0;
}
