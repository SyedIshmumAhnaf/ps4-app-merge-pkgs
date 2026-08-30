#include "splitter_core.hpp"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cctype>
#include <cerrno>
#include <algorithm>

namespace splitter {

std::string extract_base_name(const std::string& file_path) {
    if (file_path.empty()) {
        return "";
    }

    std::string::size_type last_slash = file_path.find_last_of("/\\");
    std::string filename = (last_slash == std::string::npos) ? file_path : file_path.substr(last_slash + 1);

    std::string::size_type last_dot = filename.find_last_of('.');
    if (last_dot == std::string::npos || last_dot == 0) {
        return filename;
    }

    return filename.substr(0, last_dot);
}

std::string extract_directory(const std::string& file_path) {
    std::string::size_type last_slash = file_path.find_last_of("/\\");
    if (last_slash == std::string::npos) {
        return "";
    }
    return file_path.substr(0, last_slash + 1);
}

std::string get_chunk_file_name(const std::string& base_name, std::size_t part_num) {
    std::ostringstream oss;
    oss << base_name << "_" << std::setw(3) << std::setfill('0') << part_num << ".pkgpart";
    return oss.str();
}

std::string get_chunk_file_path(const std::string& output_dir, const std::string& base_name, std::size_t part_num) {
    std::string chunk_name = get_chunk_file_name(base_name, part_num);
    if (output_dir.empty()) {
        return chunk_name;
    }

    char last_char = output_dir.back();
    if (last_char == '/' || last_char == '\\') {
        return output_dir + chunk_name;
    } else {
        return output_dir + "/" + chunk_name;
    }
}

bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

bool mb_to_bytes(uint64_t mb, uint64_t& out_bytes) {
    constexpr uint64_t BYTES_PER_MB = 1000000ULL; // Decimal MB as used by upstream
    if (mb > UINT64_MAX / BYTES_PER_MB) {
        return false;
    }
    out_bytes = mb * BYTES_PER_MB;
    return true;
}

std::vector<std::string> find_existing_part_files(const std::string& output_dir, const std::string& base_name) {
    std::vector<std::string> existing;
    std::string dir_to_open = output_dir.empty() ? "." : output_dir;

    DIR* dir = opendir(dir_to_open.c_str());
    if (!dir) {
        return existing;
    }

    std::string prefix = base_name + "_";
    std::string suffix = ".pkgpart";

    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;
        if (fname.size() > prefix.size() + suffix.size() &&
            fname.compare(0, prefix.size(), prefix) == 0 &&
            fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
            
            std::string part_digits = fname.substr(prefix.size(), fname.size() - prefix.size() - suffix.size());
            bool all_digits = (part_digits.size() >= 3);
            for (char c : part_digits) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    all_digits = false;
                    break;
                }
            }

            if (all_digits) {
                try {
                    unsigned long idx = std::stoul(part_digits);
                    if (idx > 0) {
                        std::string full_path = output_dir.empty() ? fname :
                            (output_dir.back() == '/' || output_dir.back() == '\\' ? output_dir + fname : output_dir + "/" + fname);
                        existing.push_back(full_path);
                    }
                } catch (...) {
                    // Ignore arithmetic overflow in part number string
                }
            }
        }
    }
    closedir(dir);
    std::sort(existing.begin(), existing.end());
    return existing;
}

static void cleanup_generated_parts(const std::vector<std::string>& files) {
    for (const auto& path : files) {
        std::remove(path.c_str());
    }
}

SplitResult split_file(
    const std::string& input_file_path,
    const SplitOptions& options,
    void (*progress_callback)(uint64_t processed, uint64_t total)
) {
    SplitResult result;

    if (options.chunk_size_bytes == 0) {
        result.status = SplitStatus::INVALID_CHUNK_SIZE;
        result.error_message = "Invalid chunk size: must be greater than 0 bytes.";
        return result;
    }

    std::string base_name = extract_base_name(input_file_path);
    if (base_name.empty()) {
        result.status = SplitStatus::INPUT_OPEN_ERROR;
        result.error_message = "Could not extract base name from input file path: " + input_file_path;
        return result;
    }

    std::string effective_output_dir = options.output_dir;

    // Check for existing output part files if force_overwrite is false (Fix #9)
    if (!options.force_overwrite) {
        std::vector<std::string> conflicts = find_existing_part_files(effective_output_dir, base_name);
        if (!conflicts.empty()) {
            result.status = SplitStatus::OUTPUT_ALREADY_EXISTS;
            result.error_message = "One or more output part files already exist for " + base_name;
            result.existing_conflicts = conflicts;
            return result;
        }
    }

    std::ifstream input_file(input_file_path, std::ios::binary);
    if (!input_file.is_open()) {
        result.status = SplitStatus::INPUT_OPEN_ERROR;
        result.error_message = "Could not open input file for reading: " + input_file_path;
        return result;
    }

    // Determine total file size
    input_file.seekg(0, std::ios::end);
    std::streampos end_pos = input_file.tellg();
    if (end_pos < 0) {
        result.status = SplitStatus::READ_ERROR;
        result.error_message = "Failed to seek input file: " + input_file_path;
        input_file.close();
        return result;
    }
    uint64_t total_file_size = static_cast<uint64_t>(end_pos);
    input_file.seekg(0, std::ios::beg);

    if (total_file_size == 0) {
        input_file.close();
        result.status = SplitStatus::EMPTY_INPUT;
        result.error_message = "Input file is empty (0 bytes): " + input_file_path;
        return result;
    }

    constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1 MB buffer
    std::vector<char> buffer(BUFFER_SIZE);

    std::vector<std::string> created_parts;
    std::size_t part_num = 1;
    uint64_t total_bytes_read = 0;
    uint64_t current_chunk_written = 0;
    std::ofstream current_out;

    while (total_bytes_read < total_file_size && input_file) {
        uint64_t chunk_remaining = options.chunk_size_bytes - current_chunk_written;
        uint64_t to_read = std::min(static_cast<uint64_t>(buffer.size()), chunk_remaining);

        input_file.read(buffer.data(), static_cast<std::streamsize>(to_read));
        std::streamsize bytes_read = input_file.gcount();

        if (bytes_read <= 0) {
            break;
        }

        // Lazily open new chunk file on first byte of chunk
        if (!current_out.is_open()) {
            std::string part_path = get_chunk_file_path(effective_output_dir, base_name, part_num);
            current_out.open(part_path, std::ios::binary | std::ios::trunc);
            if (!current_out.is_open()) {
                cleanup_generated_parts(created_parts);
                input_file.close();
                result.status = SplitStatus::OUTPUT_OPEN_ERROR;
                result.error_message = "Failed to open output chunk for writing: " + part_path;
                return result;
            }
            created_parts.push_back(part_path);
            current_chunk_written = 0;
        }

        current_out.write(buffer.data(), bytes_read);
        if (!current_out.good() || current_out.fail()) {
            current_out.close();
            cleanup_generated_parts(created_parts);
            input_file.close();
            result.status = SplitStatus::WRITE_ERROR;
            result.error_message = "Failed writing chunk data to: " + created_parts.back();
            return result;
        }

        current_chunk_written += static_cast<uint64_t>(bytes_read);
        total_bytes_read += static_cast<uint64_t>(bytes_read);

        if (progress_callback) {
            progress_callback(total_bytes_read, total_file_size);
        }

        // If current chunk boundary reached, finalize and close it (Fix #8)
        if (current_chunk_written == options.chunk_size_bytes) {
            current_out.flush();
            if (!current_out.good() || current_out.fail()) {
                current_out.close();
                cleanup_generated_parts(created_parts);
                input_file.close();
                result.status = SplitStatus::WRITE_ERROR;
                result.error_message = "Failed to flush chunk: " + created_parts.back();
                return result;
            }
            current_out.close();
            if (current_out.fail()) {
                cleanup_generated_parts(created_parts);
                input_file.close();
                result.status = SplitStatus::WRITE_ERROR;
                result.error_message = "Failed to close chunk: " + created_parts.back();
                return result;
            }
            part_num++;
            current_chunk_written = 0;
        }
    }

    // Check for bad input stream state or premature EOF (Fix review: [P2])
    if (input_file.bad() || total_bytes_read < total_file_size) {
        if (current_out.is_open()) {
            current_out.close();
        }
        cleanup_generated_parts(created_parts);
        input_file.close();
        result.status = SplitStatus::READ_ERROR;
        if (input_file.bad()) {
            result.error_message = "I/O error while reading input file: " + input_file_path;
        } else {
            result.error_message = "Premature end of file: read " + std::to_string(total_bytes_read) +
                                   " bytes, expected " + std::to_string(total_file_size) + " bytes from: " + input_file_path;
        }
        return result;
    }

    input_file.close();

    // Finalize trailing partial chunk if still open
    if (current_out.is_open()) {
        current_out.flush();
        if (!current_out.good() || current_out.fail()) {
            current_out.close();
            cleanup_generated_parts(created_parts);
            result.status = SplitStatus::WRITE_ERROR;
            result.error_message = "Failed to flush final chunk: " + created_parts.back();
            return result;
        }
        current_out.close();
        if (current_out.fail()) {
            cleanup_generated_parts(created_parts);
            result.status = SplitStatus::WRITE_ERROR;
            result.error_message = "Failed to close final chunk: " + created_parts.back();
            return result;
        }
    }

    // If overwrite was forced, clean up any obsolete higher-numbered or leftover parts from previous runs (Fix review: [P1] & [P2])
    if (options.force_overwrite) {
        std::vector<std::string> current_matching = find_existing_part_files(effective_output_dir, base_name);
        for (const auto& existing_file : current_matching) {
            if (std::find(created_parts.begin(), created_parts.end(), existing_file) == created_parts.end()) {
                if (std::remove(existing_file.c_str()) != 0) {
                    if (errno != ENOENT) {
                        result.status = SplitStatus::WRITE_ERROR;
                        result.error_message = "Failed to remove obsolete part file: " + existing_file;
                        return result;
                    }
                }
            }
        }
    }

    result.status = SplitStatus::SUCCESS;
    result.generated_parts = created_parts;
    result.total_bytes_read = total_bytes_read;
    result.parts_count = created_parts.size();
    return result;
}

} // namespace splitter
