#pragma once
#include "gravel/ch/contraction.h"
#include <string>

namespace gravel {

// Serialize a ContractionResult to .gravel.ch format
void save_ch(const ContractionResult& ch, const std::string& path);

// Load a ContractionResult from .gravel.ch format
ContractionResult load_ch(const std::string& path);

// Load a ContractionResult via mmap (avoids iostream buffering)
ContractionResult load_ch_mmap(const std::string& path);

}  // namespace gravel
