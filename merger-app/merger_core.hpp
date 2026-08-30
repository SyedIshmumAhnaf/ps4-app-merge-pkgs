#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace merger {

struct PkgPartInfo {
    std::string filename;
    std::string base_name;
    uint32_t part_index; // 1-based, e.g. 1 for _001.pkgpart
};

enum class ValidationStatus {
    OK,
    EMPTY_INPUT,
    MULTIPLE_BASE_NAMES,
    NON_CONTIGUOUS_PARTS,
    DUPLICATE_PARTS,
    INVALID_NAMING
};

struct ValidationResult {
    ValidationStatus status;
    std::string error_message;
    std::vector<std::string> detected_base_names;
    std::vector<std::string> sorted_files;
    std::string single_base_name;
};

// Parses a filename like "Game_001.pkgpart" into PkgPartInfo.
// Returns true if valid, false if not matching <basename>_<3+digits>.pkgpart
bool parse_pkgpart_filename(const std::string& filename, PkgPartInfo& out_info);

// Filters a raw directory listing to only valid .pkgpart files
std::vector<std::string> filter_pkgpart_files(const std::vector<std::string>& all_files);

// Analyzes and validates a list of raw filenames:
// 1. Filters non-pkgpart files.
// 2. Checks for multi-game / multiple basenames (Fix #2).
// 3. Checks for contiguous run starting at 1 with no missing or duplicate parts (Fix #3).
// 4. Returns sorted filenames ready for merge.
ValidationResult validate_and_prepare_parts(const std::vector<std::string>& files);

} // namespace merger
