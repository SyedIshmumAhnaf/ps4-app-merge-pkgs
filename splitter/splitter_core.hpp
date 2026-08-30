#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace splitter {

enum class SplitStatus {
    SUCCESS,
    EMPTY_INPUT,
    INPUT_OPEN_ERROR,
    OUTPUT_OPEN_ERROR,
    WRITE_ERROR,
    READ_ERROR,
    OUTPUT_ALREADY_EXISTS,
    INVALID_CHUNK_SIZE
};

struct SplitOptions {
    // Default chunk size is 15 GB (15,000 MB in decimal notation as original)
    uint64_t chunk_size_bytes = 15000ULL * 1000000ULL;
    bool force_overwrite = false;
    std::string output_dir = ""; // Empty string uses the input file's directory / current directory
};

struct SplitResult {
    SplitStatus status = SplitStatus::SUCCESS;
    std::string error_message;
    std::vector<std::string> generated_parts;
    std::vector<std::string> existing_conflicts;
    uint64_t total_bytes_read = 0;
    std::size_t parts_count = 0;
};

// Extracts the file base name without path or final extension (e.g. "/path/to/Game.pkg" -> "Game")
std::string extract_base_name(const std::string& file_path);

// Extracts directory path from file_path including trailing separator (e.g. "/path/to/Game.pkg" -> "/path/to/")
std::string extract_directory(const std::string& file_path);

// Formats a chunk filename: <base_name>_<3+digit part_num>.pkgpart (e.g. "Game_001.pkgpart")
std::string get_chunk_file_name(const std::string& base_name, std::size_t part_num);

// Joins output directory and chunk filename
std::string get_chunk_file_path(const std::string& output_dir, const std::string& base_name, std::size_t part_num);

// Checks for existing .pkgpart files matching the target base name in output_dir
std::vector<std::string> find_existing_part_files(const std::string& output_dir, const std::string& base_name);

// Checks if a specific file exists on disk
bool file_exists(const std::string& path);

// Converts MB to bytes with overflow checking. Returns false if overflow occurs.
bool mb_to_bytes(uint64_t mb, uint64_t& out_bytes);

// Core splitting function (Fix #8, #9, #10):
// - Prevents trailing empty chunk on exact chunk boundaries (Fix #8)
// - Guards against silent overwrites unless force_overwrite is true (Fix #9)
// - Verifies all write and flush/close operations, cleaning up partial files on failure (Fix #10)
SplitResult split_file(
    const std::string& input_file_path,
    const SplitOptions& options,
    void (*progress_callback)(uint64_t processed, uint64_t total) = nullptr
);

} // namespace splitter
