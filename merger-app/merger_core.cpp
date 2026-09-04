#include "merger_core.hpp"
#include "manifest.hpp"
#include "sha256.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

namespace merger {

bool parse_pkgpart_filename(const std::string& filename, PkgPartInfo& out_info) {
    std::string fname = filename;
    size_t last_slash = fname.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        fname = fname.substr(last_slash + 1);
    }

    const std::string suffix = ".pkgpart";
    if (fname.length() <= suffix.length()) {
        return false;
    }

    if (fname.compare(fname.length() - suffix.length(), suffix.length(), suffix) != 0) {
        return false;
    }

    std::string stem = fname.substr(0, fname.length() - suffix.length());
    size_t last_underscore = stem.find_last_of('_');
    if (last_underscore == std::string::npos || last_underscore == 0 || last_underscore == stem.length() - 1) {
        return false;
    }

    std::string base = stem.substr(0, last_underscore);
    std::string part_digits = stem.substr(last_underscore + 1);

    if (part_digits.length() < 3) {
        return false;
    }

    for (char c : part_digits) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    try {
        unsigned long idx = std::stoul(part_digits);
        if (idx == 0 || idx > UINT32_MAX) {
            return false;
        }
        out_info.filename = fname;
        out_info.base_name = base;
        out_info.part_index = static_cast<uint32_t>(idx);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> filter_pkgpart_files(const std::vector<std::string>& all_files) {
    std::vector<std::string> filtered;
    for (const auto& file : all_files) {
        PkgPartInfo dummy;
        if (parse_pkgpart_filename(file, dummy)) {
            filtered.push_back(file);
        }
    }
    return filtered;
}

ValidationResult validate_and_prepare_parts(const std::vector<std::string>& files) {
    ValidationResult result;
    result.status = ValidationStatus::OK;

    std::vector<std::string> valid_pkgparts = filter_pkgpart_files(files);
    if (valid_pkgparts.empty()) {
        result.status = ValidationStatus::EMPTY_INPUT;
        result.error_message = "No valid .pkgpart files found in directory.";
        return result;
    }

    std::map<std::string, std::vector<PkgPartInfo>> groups;
    for (const auto& fname : valid_pkgparts) {
        PkgPartInfo info;
        if (parse_pkgpart_filename(fname, info)) {
            groups[info.base_name].push_back(info);
        }
    }

    for (const auto& kv : groups) {
        result.detected_base_names.push_back(kv.first);
    }

    if (groups.size() > 1) {
        result.status = ValidationStatus::MULTIPLE_BASE_NAMES;
        std::ostringstream oss;
        oss << "Multiple distinct package base names detected (" << groups.size() << "): ";
        for (size_t i = 0; i < result.detected_base_names.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << result.detected_base_names[i];
        }
        oss << ". Please ensure only one game's parts are present in the directory.";
        result.error_message = oss.str();
        return result;
    }

    const auto& single_group = groups.begin()->second;
    result.single_base_name = groups.begin()->first;

    std::vector<PkgPartInfo> sorted_parts = single_group;
    std::sort(sorted_parts.begin(), sorted_parts.end(), [](const PkgPartInfo& a, const PkgPartInfo& b) {
        return a.part_index < b.part_index;
    });

    uint32_t expected_idx = 1;
    for (const auto& part : sorted_parts) {
        if (part.part_index < expected_idx) {
            result.status = ValidationStatus::DUPLICATE_PARTS;
            result.error_message = "Duplicate part detected for part number " + std::to_string(part.part_index) +
                                   " (" + part.filename + ").";
            result.sorted_files.clear();
            return result;
        }

        if (part.part_index > expected_idx) {
            result.status = ValidationStatus::NON_CONTIGUOUS_PARTS;
            result.error_message = "Non-contiguous part sequence. Expected part " + std::to_string(expected_idx) +
                                   ", but found part " + std::to_string(part.part_index) + " (" + part.filename + ").";
            result.sorted_files.clear();
            return result;
        }

        result.sorted_files.push_back(part.filename);
        expected_idx++;
    }

    return result;
}

bool file_exists(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0);
}

std::string get_temporary_merge_path(const std::string& output_path) {
    return output_path + ".tmp.merging";
}

bool clean_stale_temp_file(const std::string& output_path) {
    std::string temp_path = get_temporary_merge_path(output_path);
    if (std::remove(temp_path.c_str()) != 0) {
        if (errno == ENOENT) {
            return true; // File did not exist, clean state
        }
        return false; // Permission, I/O, or lookup failure (e.g. EACCES, EIO)
    }
    return true; // Successfully unlinked
}

std::string get_collision_safe_failed_path(const std::string& output_path) {
    std::string base_failed = output_path + ".checksum-failed";
    if (!file_exists(base_failed)) {
        return base_failed;
    }

    uint64_t counter = 1;
    while (true) {
        std::string candidate = base_failed + "." + std::to_string(counter);
        if (!file_exists(candidate)) {
            return candidate;
        }
        counter++;
    }
}

bool compute_required_space(uint64_t total_parts_size, uint64_t multiplier, uint64_t& out_required_bytes) {
    if (multiplier == 0) {
        out_required_bytes = total_parts_size;
        return true;
    }
    if (total_parts_size > UINT64_MAX / multiplier) {
        return false; // overflow
    }
    out_required_bytes = total_parts_size * multiplier;
    return true;
}

bool get_available_space(const std::string& target_path, uint64_t& out_free_bytes) {
    struct statvfs stat;
    // If target_path doesn't exist yet, inspect its parent directory
    std::string check_path = target_path;
    if (!file_exists(check_path)) {
        size_t last_slash = check_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            check_path = (last_slash == 0) ? "/" : check_path.substr(0, last_slash);
        } else {
            check_path = ".";
        }
    }

    if (statvfs(check_path.c_str(), &stat) != 0) {
        return false;
    }

    out_free_bytes = static_cast<uint64_t>(stat.f_bavail) * static_cast<uint64_t>(stat.f_frsize);
    return true;
}

uint64_t calculate_total_parts_size(const std::string& input_dir, const std::vector<std::string>& files) {
    uint64_t total = 0;
    for (const auto& file : files) {
        std::string full_path = input_dir;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += file;

        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            total += static_cast<uint64_t>(st.st_size);
        }
    }
    return total;
}

static bool sync_parent_directory(const std::string& path, std::string& err) {
    std::string dir_path = path;
    size_t last_slash = dir_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        dir_path = (last_slash == 0) ? "/" : dir_path.substr(0, last_slash);
    } else {
        dir_path = ".";
    }

    int dir_fd = open(dir_path.c_str(), O_RDONLY);
    if (dir_fd < 0) {
        err = "Failed to open parent directory for sync: " + dir_path;
        return false;
    }

    if (fsync(dir_fd) != 0) {
        close(dir_fd);
        err = "Failed to fsync parent directory: " + dir_path;
        return false;
    }
    close(dir_fd);
    return true;
}

MergeResult perform_merge(
    const std::string& input_dir,
    const std::vector<std::string>& files,
    const std::string& output_path,
    void (*progress_callback)(uint64_t, uint64_t)
) {
    MergeResult result;
    result.status = MergeStatus::SUCCESS;
    result.bytes_written = 0;

    if (files.empty()) {
        result.status = MergeStatus::INPUT_OPEN_ERROR;
        result.error_message = "No input files provided for merge.";
        return result;
    }

    // Determine package base name from parts
    PkgPartInfo first_part_info;
    if (!parse_pkgpart_filename(files.front(), first_part_info)) {
        result.status = MergeStatus::INPUT_OPEN_ERROR;
        result.error_message = "Invalid input part filename: " + files.front();
        return result;
    }
    std::string base_name = first_part_info.base_name;

    // Locate and validate integrity manifest (Fix #12)
    std::string manifest_path = manifest::get_manifest_path(input_dir, base_name);
    result.manifest_path = manifest_path;

    manifest::PkgManifest manifest_data;
    std::string manifest_err;
    manifest::ManifestStatus m_status = manifest::read_manifest_file(manifest_path, manifest_data, manifest_err);

    if (m_status == manifest::ManifestStatus::FILE_NOT_FOUND) {
        result.status = MergeStatus::MANIFEST_NOT_FOUND;
        result.error_message = "Integrity manifest not found: " + manifest_path + ". Ensure " +
                               manifest::get_manifest_filename(base_name) + " is present alongside .pkgpart files.";
        return result;
    } else if (m_status != manifest::ManifestStatus::OK) {
        result.status = MergeStatus::MANIFEST_INVALID;
        result.error_message = "Integrity manifest validation failed: " + manifest_err;
        return result;
    }

    if (manifest_data.package_base_name != base_name) {
        result.status = MergeStatus::MANIFEST_MISMATCH;
        result.error_message = "Manifest package_base_name ('" + manifest_data.package_base_name +
                               "') does not match parts base name ('" + base_name + "').";
        return result;
    }

    result.expected_sha256 = manifest_data.sha256;

    // Collect actual part sizes on disk
    std::vector<uint64_t> actual_part_sizes;
    actual_part_sizes.reserve(files.size());
    for (const auto& file : files) {
        std::string full_path = input_dir;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += file;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) {
            result.status = MergeStatus::INPUT_OPEN_ERROR;
            result.error_message = "Failed to access input part file: " + full_path;
            return result;
        }
        actual_part_sizes.push_back(static_cast<uint64_t>(st.st_size));
    }

    // Validate chunk geometry upfront against manifest (Fix #12)
    std::string geom_err;
    manifest::ManifestStatus g_status = manifest::validate_chunk_geometry(manifest_data, actual_part_sizes, geom_err);
    if (g_status != manifest::ManifestStatus::OK) {
        result.status = (g_status == manifest::ManifestStatus::CHUNK_COUNT_MISMATCH ||
                         g_status == manifest::ManifestStatus::TOTAL_SIZE_MISMATCH)
                            ? MergeStatus::MANIFEST_MISMATCH
                            : MergeStatus::GEOMETRY_MISMATCH;
        result.error_message = "Chunk geometry verification against manifest failed: " + geom_err;
        return result;
    }

    uint64_t total_expected_bytes = manifest_data.total_size_bytes;

    // Clean any prior stale temporary file before merging
    std::string temp_output_path = get_temporary_merge_path(output_path);
    std::remove(temp_output_path.c_str());

    std::ofstream output_file(temp_output_path, std::ios::binary);
    if (!output_file.is_open()) {
        result.status = MergeStatus::OUTPUT_OPEN_ERROR;
        result.error_message = "Failed to open temporary output file for writing: " + temp_output_path;
        return result;
    }

    crypto::SHA256 sha_ctx;

    const size_t BUFFER_SIZE = 1024 * 1024; // 1 MB buffer
    std::vector<char> buffer(BUFFER_SIZE);

    for (const auto& file : files) {
        std::string full_path = input_dir;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += file;

        std::ifstream input_file(full_path, std::ios::binary);
        if (!input_file.is_open()) {
            output_file.close();
            std::remove(temp_output_path.c_str());
            result.status = MergeStatus::INPUT_OPEN_ERROR;
            result.error_message = "Failed to open input part: " + full_path;
            return result;
        }

        while (input_file.read(buffer.data(), buffer.size()) || input_file.gcount() > 0) {
            std::streamsize bytes_read = input_file.gcount();
            if (bytes_read <= 0) break;

            output_file.write(buffer.data(), bytes_read);
            if (!output_file.good()) {
                input_file.close();
                output_file.close();
                std::remove(temp_output_path.c_str());
                result.status = MergeStatus::WRITE_ERROR;
                result.error_message = "Write error occurred while merging " + file + " (likely disk full or I/O error).";
                return result;
            }

            // On-the-fly streaming SHA-256 update (Fix #12)
            sha_ctx.update(buffer.data(), static_cast<size_t>(bytes_read));

            result.bytes_written += static_cast<uint64_t>(bytes_read);
            if (progress_callback) {
                progress_callback(result.bytes_written, total_expected_bytes);
            }
        }

        if (input_file.bad()) {
            input_file.close();
            output_file.close();
            std::remove(temp_output_path.c_str());
            result.status = MergeStatus::READ_ERROR;
            result.error_message = "Read error occurred while reading " + full_path;
            return result;
        }

        input_file.close();
    }

    output_file.flush();
    if (!output_file.good()) {
        output_file.close();
        std::remove(temp_output_path.c_str());
        result.status = MergeStatus::WRITE_ERROR;
        result.error_message = "Failed to flush output stream to " + temp_output_path;
        return result;
    }

    output_file.close();
    if (output_file.fail()) {
        std::remove(temp_output_path.c_str());
        result.status = MergeStatus::WRITE_ERROR;
        result.error_message = "Failed to cleanly close output file: " + temp_output_path;
        return result;
    }

    // Sync temporary file blocks and metadata to physical storage
    int temp_fd = open(temp_output_path.c_str(), O_RDONLY);
    if (temp_fd >= 0) {
        if (fsync(temp_fd) != 0) {
            close(temp_fd);
            std::remove(temp_output_path.c_str());
            result.status = MergeStatus::WRITE_ERROR;
            result.error_message = "Failed to sync temporary file to durable storage: " + temp_output_path;
            return result;
        }
        close(temp_fd);
    } else {
        std::remove(temp_output_path.c_str());
        result.status = MergeStatus::WRITE_ERROR;
        result.error_message = "Failed to open temporary file for sync: " + temp_output_path;
        return result;
    }

    // Finalize computed SHA-256
    std::string computed_hex = sha_ctx.final_hex();
    result.computed_sha256 = computed_hex;

    // Checksum verification against manifest (Fix #12)
    if (computed_hex != manifest_data.sha256) {
        // Retain output durably and safely as <output_path>.checksum-failed(.N)
        std::string failed_retention_path = get_collision_safe_failed_path(output_path);

        if (std::rename(temp_output_path.c_str(), failed_retention_path.c_str()) != 0) {
            // If rename fails, leave temp_output_path as the retained path
            result.retained_failed_path = temp_output_path;
        } else {
            result.retained_failed_path = failed_retention_path;
            std::string sync_err;
            sync_parent_directory(failed_retention_path, sync_err);
        }

        result.status = MergeStatus::CHECKSUM_MISMATCH;
        result.error_message = "Checksum mismatch: computed SHA-256 is " + computed_hex +
                               ", but manifest recorded " + manifest_data.sha256 +
                               ". Output retained for inspection at: " + result.retained_failed_path;
        return result;
    }

    // Checksum matched! Atomically publish to target destination path
    if (std::rename(temp_output_path.c_str(), output_path.c_str()) != 0) {
        std::remove(temp_output_path.c_str());
        result.status = MergeStatus::WRITE_ERROR;
        result.error_message = "Failed to rename temporary file to destination path: " + output_path;
        return result;
    }

    // Durably sync parent directory
    std::string sync_err;
    if (!sync_parent_directory(output_path, sync_err)) {
        result.status = MergeStatus::POST_RENAME_SYNC_ERROR;
        result.error_message = "Output file was written and moved to " + output_path +
                               ", but parent directory synchronization failed: " + sync_err;
        return result;
    }

    return result;
}

} // namespace merger
