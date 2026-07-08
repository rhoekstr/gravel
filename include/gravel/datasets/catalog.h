#pragma once
/// @file catalog.h
/// @brief The dataset catalog behind the info-pull (`gravel.datasets`).

#include "gravel/core/dataset_info.h"

#include <vector>

namespace gravel {

/// The catalog of natively-supported datasets: the graph substrates, boundary
/// sets, and hazard overlays Gravel knows how to work with. Pure,
/// build-independent metadata — every entry appears regardless of whether its
/// optional backing (libosmium for OSM) is compiled in or its Python fetcher is
/// importable; the Python surface (`gravel.datasets`) annotates each entry with
/// runtime availability and renders it. The authoritative field dictionary for
/// each source is linked via `DatasetInfo::field_docs_url`, never reproduced.
///
/// Defined once, here in `gravel-datasets`, to avoid an ODR collision under the
/// flat `gravel::` namespace; when a later phase adds a network-substrate module
/// it contributes its own accessor and the binding layer aggregates.
std::vector<DatasetInfo> dataset_catalog();

}  // namespace gravel
