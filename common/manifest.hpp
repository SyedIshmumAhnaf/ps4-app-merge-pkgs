#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace manifest {

constexpr size_t MAX_MANIFEST_FILE_SIZE = 64 * 1024; // 64 KiB maximum manifest file size
constexpr uint32_t CURRENT_SCHEMA_VERSION = 1;

struct PkgManifest {
    uint32_t schema_version = CURRENT_SCHEMA_VERSION;
    std::string original_filename;
    std::string package_base_name;
    uint64_t total_size_bytes = 0;
    uint64_t chunk_size_bytes = 0;
    uint64_t chunk_count = 0;
    std::string sha256; // 64-character lowercase hex string
};

enum class ManifestStatus {
    OK,
    FILE_NOT_FOUND,
    FILE_READ_ERROR,
    FILE_TOO_LARGE,
    INVALID_JSON_SYNTAX,
    DUPLICATE_KEY,
    UNKNOWN_KEY,
    MISSING_REQUIRED_KEY,
    TYPE_MISMATCH,
    INTEGER_OVERFLOW,
    INVALID_ESCAPE_SEQUENCE,
    INVALID_HASH_FORMAT,
    UNSUPPORTED_SCHEMA_VERSION,
    BASE_NAME_MISMATCH,
    CHUNK_COUNT_MISMATCH,
    TOTAL_SIZE_MISMATCH,
    GEOMETRY_MISMATCH,
    FILE_WRITE_ERROR
};

// Returns standard manifest filename: <base_name>.manifest.json
std::string get_manifest_filename(const std::string& base_name);

// Combines directory and manifest filename
std::string get_manifest_path(const std::string& directory, const std::string& base_name);

// Validates whether a hash string is strictly 64 lowercase hexadecimal characters
bool is_valid_sha256_hex(const std::string& hash);

// Serializes a PkgManifest struct to a formatted JSON string
std::string serialize_manifest_json(const PkgManifest& manifest);

// Parses JSON content into a PkgManifest struct with strict hardening:
// - Rejects duplicate keys
// - Rejects unknown Version 1 keys
// - Rejects integer overflow
// - Rejects invalid escapes or unescaped control chars
// - Rejects trailing non-whitespace content
// - Validates required fields and SHA-256 hex format
ManifestStatus parse_manifest_json(const std::string& json_str, PkgManifest& out_manifest, std::string& out_error);

// Reads and parses a manifest file from disk, enforcing the 64 KiB size cap
ManifestStatus read_manifest_file(const std::string& file_path, PkgManifest& out_manifest, std::string& out_error);

// Atomically writes a manifest file to disk using a temporary file and rename
bool write_manifest_file_atomic(const std::string& target_path, const PkgManifest& manifest, std::string& out_error);

// Validates chunk geometry against actual part sizes on disk:
// 1. chunk_count == ceil(total_size_bytes / chunk_size_bytes)
// 2. actual_part_sizes.size() == chunk_count
// 3. sum(actual_part_sizes) == total_size_bytes
// 4. Every non-final part == chunk_size_bytes
// 5. Final part > 0 and <= chunk_size_bytes (exact expected remainder)
ManifestStatus validate_chunk_geometry(
    const PkgManifest& manifest,
    const std::vector<uint64_t>& actual_part_sizes,
    std::string& out_error
);

} // namespace manifest
