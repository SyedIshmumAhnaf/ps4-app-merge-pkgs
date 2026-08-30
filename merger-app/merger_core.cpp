#include "merger_core.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <set>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
#include <sys/statvfs.h>


namespace merger {

namespace {

const std::string PKGPART_EXT = ".pkgpart";

bool is_all_digits(const std::string& str) {
    if (str.empty()) return false;
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

} // namespace

bool parse_pkgpart_filename(const std::string& filename, PkgPartInfo& out_info) {
    // Expected format: <basename>_<NNN>.pkgpart
    // Minimum length: "a_001.pkgpart" -> 1 + 1 + 3 + 8 = 13 chars
    if (filename.size() < 13) {
        return false;
    }

    if (filename.size() < PKGPART_EXT.size()) {
        return false;
    }

    // Check extension case-insensitively / exactly
    if (filename.rfind(PKGPART_EXT) != (filename.size() - PKGPART_EXT.size())) {
        return false;
    }

    std::string stem = filename.substr(0, filename.size() - PKGPART_EXT.size());
    size_t last_underscore = stem.find_last_of('_');
    if (last_underscore == std::string::npos || last_underscore == 0 || last_underscore == stem.size() - 1) {
        return false;
    }

    std::string base = stem.substr(0, last_underscore);
    std::string part_str = stem.substr(last_underscore + 1);

    if (!is_all_digits(part_str) || part_str.size() < 3) {
        return false;
    }

    try {
        unsigned long val = std::stoul(part_str);
        if (val == 0 || val > 0xFFFFFFFF) {
            return false;
        }
        out_info.filename = filename;
        out_info.base_name = base;
        out_info.part_index = static_cast<uint32_t>(val);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> filter_pkgpart_files(const std::vector<std::string>& all_files) {
    std::vector<std::string> filtered;
    PkgPartInfo info;
    for (const auto& file : all_files) {
        if (parse_pkgpart_filename(file, info)) {
            filtered.push_back(file);
        }
    }
    return filtered;
}

ValidationResult validate_and_prepare_parts(const std::vector<std::string>& files) {
    ValidationResult result;
    result.status = ValidationStatus::OK;

    std::vector<PkgPartInfo> valid_parts;
    std::set<std::string> base_names;

    for (const auto& file : files) {
        PkgPartInfo info;
        if (parse_pkgpart_filename(file, info)) {
            valid_parts.push_back(info);
            base_names.insert(info.base_name);
        }
    }

    for (const auto& bn : base_names) {
        result.detected_base_names.push_back(bn);
    }

    if (valid_parts.empty()) {
        result.status = ValidationStatus::EMPTY_INPUT;
        result.error_message = "No valid .pkgpart files found in directory.";
        return result;
    }

    if (base_names.size() > 1) {
        result.status = ValidationStatus::MULTIPLE_BASE_NAMES;
        std::ostringstream oss;
        oss << "Found parts from multiple different packages (" << base_names.size() << " detected):\n";
        for (const auto& bn : base_names) {
            oss << " - " << bn << "\n";
        }
        oss << "Please keep only one package's parts in the directory at a time.";
        result.error_message = oss.str();
        return result;
    }

    result.single_base_name = *base_names.begin();

    // Sort parts by part_index ascending
    std::sort(valid_parts.begin(), valid_parts.end(), [](const PkgPartInfo& a, const PkgPartInfo& b) {
        return a.part_index < b.part_index;
    });

    // Check for starting index == 1, duplicate parts, and missing sequence parts
    std::set<uint32_t> seen_indices;
    uint32_t expected_idx = 1;

    for (const auto& part : valid_parts) {
        if (seen_indices.count(part.part_index) > 0) {
            result.status = ValidationStatus::DUPLICATE_PARTS;
            std::ostringstream oss;
            oss << "Duplicate part index detected: " << part.part_index << " (" << part.filename << ")";
            result.error_message = oss.str();
            return result;
        }
        seen_indices.insert(part.part_index);

        if (part.part_index != expected_idx) {
            result.status = ValidationStatus::NON_CONTIGUOUS_PARTS;
            std::ostringstream oss;
            oss << "Missing part sequence! Expected part " << expected_idx << ", but encountered " << part.part_index << " (" << part.filename << ").";
            result.error_message = oss.str();
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

MergeResult perform_merge(
    const std::string& input_dir,
    const std::vector<std::string>& files,
    const std::string& output_path,
    void (*progress_callback)(uint64_t, uint64_t)
) {
    MergeResult result;
    result.status = MergeStatus::SUCCESS;
    result.bytes_written = 0;

    uint64_t total_expected_bytes = calculate_total_parts_size(input_dir, files);

    // [P1] Write to a temporary path until the merge completes.
    // This ensures power loss, crash, or early abort leaves no partial file at output_path,
    // and preserves any existing output_path until the new file is fully written, flushed, and closed.
    std::string temp_output_path = output_path + ".tmp.merging";
    std::remove(temp_output_path.c_str());

    std::ofstream output_file(temp_output_path, std::ios::binary);
    if (!output_file.is_open()) {
        result.status = MergeStatus::OUTPUT_OPEN_ERROR;
        result.error_message = "Failed to open temporary output file for writing: " + temp_output_path;
        return result;
    }

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

    // Atomic move/rename from temp path to destination path
    if (std::rename(temp_output_path.c_str(), output_path.c_str()) != 0) {
        std::remove(temp_output_path.c_str());
        result.status = MergeStatus::WRITE_ERROR;
        result.error_message = "Failed to rename temporary file to destination path: " + output_path;
        return result;
    }

    return result;

}

} // namespace merger

