#include "merger_core.hpp"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <set>

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

} // namespace merger
