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

// Checks if a file exists at the given path (Fix #5)
bool file_exists(const std::string& path);

// Queries available free disk space on the volume containing target_path (Fix #7)
// Returns available bytes in out_free_bytes. Returns false on error.
bool get_available_space(const std::string& target_path, uint64_t& out_free_bytes);

// Calculates the total size in bytes of the listed input files within input_dir
uint64_t calculate_total_parts_size(const std::string& input_dir, const std::vector<std::string>& files);

enum class MergeStatus {
    SUCCESS,
    INPUT_OPEN_ERROR,
    OUTPUT_OPEN_ERROR,
    WRITE_ERROR,
    READ_ERROR,
    INSUFFICIENT_SPACE
};

struct MergeResult {
    MergeStatus status;
    std::string error_message;
    uint64_t bytes_written = 0;
};

// Merges files from input_dir into output_path with per-chunk write validation (Fix #6)
// If a write or read fails, cleans up the partial output file.
// If progress_callback is provided, it is invoked with (bytes_processed, total_bytes).
MergeResult perform_merge(
    const std::string& input_dir,
    const std::vector<std::string>& files,
    const std::string& output_path,
    void (*progress_callback)(uint64_t processed, uint64_t total) = nullptr
);

} // namespace merger

