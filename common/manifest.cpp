#include "manifest.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
#include <cctype>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

namespace manifest {

std::string get_manifest_filename(const std::string& base_name) {
    return base_name + ".manifest.json";
}

std::string get_manifest_path(const std::string& directory, const std::string& base_name) {
    std::string fname = get_manifest_filename(base_name);
    if (directory.empty()) {
        return fname;
    }
    char last = directory.back();
    if (last == '/' || last == '\\') {
        return directory + fname;
    }
    return directory + "/" + fname;
}

bool is_valid_sha256_hex(const std::string& hash) {
    if (hash.size() != 64) {
        return false;
    }
    for (char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

static std::string escape_json_string(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string serialize_manifest_json(const PkgManifest& manifest) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"schema_version\": " << manifest.schema_version << ",\n";
    ss << "  \"original_filename\": \"" << escape_json_string(manifest.original_filename) << "\",\n";
    ss << "  \"package_base_name\": \"" << escape_json_string(manifest.package_base_name) << "\",\n";
    ss << "  \"total_size_bytes\": " << manifest.total_size_bytes << ",\n";
    ss << "  \"chunk_size_bytes\": " << manifest.chunk_size_bytes << ",\n";
    ss << "  \"chunk_count\": " << manifest.chunk_count << ",\n";
    ss << "  \"sha256\": \"" << manifest.sha256 << "\"\n";
    ss << "}\n";
    return ss.str();
}

namespace {

class JsonParser {
public:
    JsonParser(const std::string& src) : src_(src), pos_(0), len_(src.size()) {}

    ManifestStatus parse(PkgManifest& out, std::string& err) {
        skip_whitespace();
        if (pos_ >= len_ || src_[pos_] != '{') {
            err = "Expected '{' at beginning of JSON root object";
            return ManifestStatus::INVALID_JSON_SYNTAX;
        }
        pos_++; // Consume '{'

        std::set<std::string> seen_keys;
        bool has_schema = false;
        bool has_orig_fn = false;
        bool has_base_name = false;
        bool has_total_size = false;
        bool has_chunk_size = false;
        bool has_chunk_count = false;
        bool has_sha256 = false;

        PkgManifest m;

        while (true) {
            skip_whitespace();
            if (pos_ >= len_) {
                err = "Unexpected end of input inside JSON object";
                return ManifestStatus::INVALID_JSON_SYNTAX;
            }
            if (src_[pos_] == '}') {
                pos_++; // Consume '}'
                break;
            }

            // Expect string key
            if (src_[pos_] != '"') {
                err = "Expected string key in JSON object at position " + std::to_string(pos_);
                return ManifestStatus::INVALID_JSON_SYNTAX;
            }

            std::string key;
            ManifestStatus ks = parse_string(key, err);
            if (ks != ManifestStatus::OK) return ks;

            if (seen_keys.find(key) != seen_keys.end()) {
                err = "Duplicate key in JSON manifest: '" + key + "'";
                return ManifestStatus::DUPLICATE_KEY;
            }
            seen_keys.insert(key);

            skip_whitespace();
            if (pos_ >= len_ || src_[pos_] != ':') {
                err = "Expected ':' after key '" + key + "'";
                return ManifestStatus::INVALID_JSON_SYNTAX;
            }
            pos_++; // Consume ':'
            skip_whitespace();

            if (key == "schema_version") {
                uint64_t v = 0;
                ManifestStatus ns = parse_uint64(v, err);
                if (ns != ManifestStatus::OK) return ns;
                if (v > UINT32_MAX) {
                    err = "schema_version exceeds uint32 range";
                    return ManifestStatus::INTEGER_OVERFLOW;
                }
                m.schema_version = static_cast<uint32_t>(v);
                has_schema = true;
            } else if (key == "original_filename") {
                ManifestStatus ss = parse_string(m.original_filename, err);
                if (ss != ManifestStatus::OK) return ss;
                has_orig_fn = true;
            } else if (key == "package_base_name") {
                ManifestStatus ss = parse_string(m.package_base_name, err);
                if (ss != ManifestStatus::OK) return ss;
                has_base_name = true;
            } else if (key == "total_size_bytes") {
                ManifestStatus ns = parse_uint64(m.total_size_bytes, err);
                if (ns != ManifestStatus::OK) return ns;
                has_total_size = true;
            } else if (key == "chunk_size_bytes") {
                ManifestStatus ns = parse_uint64(m.chunk_size_bytes, err);
                if (ns != ManifestStatus::OK) return ns;
                has_chunk_size = true;
            } else if (key == "chunk_count") {
                ManifestStatus ns = parse_uint64(m.chunk_count, err);
                if (ns != ManifestStatus::OK) return ns;
                has_chunk_count = true;
            } else if (key == "sha256") {
                ManifestStatus ss = parse_string(m.sha256, err);
                if (ss != ManifestStatus::OK) return ss;
                has_sha256 = true;
            } else {
                err = "Unknown key in Version 1 manifest: '" + key + "'";
                return ManifestStatus::UNKNOWN_KEY;
            }

            skip_whitespace();
            if (pos_ < len_ && src_[pos_] == ',') {
                pos_++; // Consume ','
                skip_whitespace();
                if (pos_ < len_ && src_[pos_] == '}') {
                    err = "Trailing comma in JSON object is not permitted";
                    return ManifestStatus::INVALID_JSON_SYNTAX;
                }
            } else if (pos_ < len_ && src_[pos_] == '}') {
                pos_++; // Consume '}'
                break;
            } else {
                err = "Expected ',' or '}' in JSON object at position " + std::to_string(pos_);
                return ManifestStatus::INVALID_JSON_SYNTAX;
            }
        }

        // Check for trailing content after root object
        skip_whitespace();
        if (pos_ < len_) {
            err = "Trailing non-whitespace characters after JSON root object";
            return ManifestStatus::INVALID_JSON_SYNTAX;
        }

        // Check required fields
        if (!has_schema) {
            err = "Missing required key: 'schema_version'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (m.schema_version != CURRENT_SCHEMA_VERSION) {
            err = "Unsupported schema_version: " + std::to_string(m.schema_version) + " (expected 1)";
            return ManifestStatus::UNSUPPORTED_SCHEMA_VERSION;
        }
        if (!has_orig_fn) {
            err = "Missing required key: 'original_filename'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (!has_base_name) {
            err = "Missing required key: 'package_base_name'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (m.package_base_name.empty()) {
            err = "'package_base_name' cannot be empty";
            return ManifestStatus::BASE_NAME_MISMATCH;
        }
        if (!has_total_size) {
            err = "Missing required key: 'total_size_bytes'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (!has_chunk_size) {
            err = "Missing required key: 'chunk_size_bytes'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (!has_chunk_count) {
            err = "Missing required key: 'chunk_count'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (!has_sha256) {
            err = "Missing required key: 'sha256'";
            return ManifestStatus::MISSING_REQUIRED_KEY;
        }
        if (!is_valid_sha256_hex(m.sha256)) {
            err = "Invalid sha256 format (expected 64 lowercase hex chars): '" + m.sha256 + "'";
            return ManifestStatus::INVALID_HASH_FORMAT;
        }

        out = m;
        return ManifestStatus::OK;
    }

private:
    void skip_whitespace() {
        while (pos_ < len_ && (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\r' || src_[pos_] == '\n')) {
            pos_++;
        }
    }

    ManifestStatus parse_string(std::string& out, std::string& err) {
        if (pos_ >= len_ || src_[pos_] != '"') {
            err = "Expected string starting with '\"'";
            return ManifestStatus::TYPE_MISMATCH;
        }
        pos_++; // Consume opening quote

        out.clear();
        while (pos_ < len_) {
            char c = src_[pos_++];
            if (c == '"') {
                return ManifestStatus::OK;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                err = "Unescaped control character in JSON string";
                return ManifestStatus::INVALID_JSON_SYNTAX;
            }
            if (c == '\\') {
                if (pos_ >= len_) {
                    err = "Unexpected end of input inside escape sequence";
                    return ManifestStatus::INVALID_ESCAPE_SEQUENCE;
                }
                char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > len_) {
                            err = "Incomplete \\u unicode escape sequence";
                            return ManifestStatus::INVALID_ESCAPE_SEQUENCE;
                        }
                        uint32_t cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else {
                                err = "Invalid hex character in \\u unicode escape";
                                return ManifestStatus::INVALID_ESCAPE_SEQUENCE;
                            }
                        }
                        // Encode to UTF-8
                        if (cp <= 0x7F) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp <= 0x7FF) {
                            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        err = std::string("Invalid escape sequence '\\") + esc + "'";
                        return ManifestStatus::INVALID_ESCAPE_SEQUENCE;
                }
            } else {
                out.push_back(c);
            }
        }
        err = "Unclosed string literal in JSON";
        return ManifestStatus::INVALID_JSON_SYNTAX;
    }

    ManifestStatus parse_uint64(uint64_t& out, std::string& err) {
        skip_whitespace();
        if (pos_ >= len_) {
            err = "Expected number, found end of input";
            return ManifestStatus::TYPE_MISMATCH;
        }

        if (src_[pos_] == '-') {
            err = "Negative numbers not allowed for size/count fields";
            return ManifestStatus::TYPE_MISMATCH;
        }

        if (!std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            err = "Expected digit in number";
            return ManifestStatus::TYPE_MISMATCH;
        }

        uint64_t val = 0;
        size_t start_pos = pos_;
        while (pos_ < len_ && std::isdigit(static_cast<unsigned char>(src_[pos_]))) {
            uint64_t digit = static_cast<uint64_t>(src_[pos_] - '0');
            if (val > (UINT64_MAX - digit) / 10) {
                err = "Integer overflow parsing unsigned number";
                return ManifestStatus::INTEGER_OVERFLOW;
            }
            val = val * 10 + digit;
            pos_++;
        }

        if (pos_ - start_pos > 1 && src_[start_pos] == '0') {
            // Note: leading zeroes are generally not standard JSON numbers, but allow '0' alone
            err = "Leading zeros not permitted in JSON numbers";
            return ManifestStatus::INVALID_JSON_SYNTAX;
        }

        if (pos_ < len_ && (src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E')) {
            err = "Floating point numbers not allowed for integer size/count fields";
            return ManifestStatus::TYPE_MISMATCH;
        }

        out = val;
        return ManifestStatus::OK;
    }

    const std::string& src_;
    size_t pos_;
    size_t len_;
};

} // namespace

ManifestStatus parse_manifest_json(const std::string& json_str, PkgManifest& out_manifest, std::string& out_error) {
    if (json_str.size() > MAX_MANIFEST_FILE_SIZE) {
        out_error = "Manifest content exceeds maximum size limit of " + std::to_string(MAX_MANIFEST_FILE_SIZE) + " bytes";
        return ManifestStatus::FILE_TOO_LARGE;
    }
    JsonParser parser(json_str);
    return parser.parse(out_manifest, out_error);
}

ManifestStatus read_manifest_file(const std::string& file_path, PkgManifest& out_manifest, std::string& out_error) {
    struct stat st;
    if (stat(file_path.c_str(), &st) != 0) {
        out_error = "Manifest file not found: " + file_path;
        return ManifestStatus::FILE_NOT_FOUND;
    }

    if (static_cast<uint64_t>(st.st_size) > MAX_MANIFEST_FILE_SIZE) {
        out_error = "Manifest file size (" + std::to_string(st.st_size) + " bytes) exceeds maximum limit of " +
                    std::to_string(MAX_MANIFEST_FILE_SIZE) + " bytes";
        return ManifestStatus::FILE_TOO_LARGE;
    }

    std::ifstream file(file_path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        out_error = "Failed to open manifest file for reading: " + file_path;
        return ManifestStatus::FILE_READ_ERROR;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        out_error = "I/O error reading manifest file: " + file_path;
        return ManifestStatus::FILE_READ_ERROR;
    }
    file.close();

    return parse_manifest_json(content, out_manifest, out_error);
}

bool write_manifest_file_atomic(const std::string& target_path, const PkgManifest& manifest, std::string& out_error) {
    std::string json_data = serialize_manifest_json(manifest);
    std::string temp_path = target_path + ".tmp." + std::to_string(static_cast<unsigned long>(getpid()));

    int fd = open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        out_error = "Failed to create temporary manifest file: " + temp_path;
        return false;
    }

    size_t written = 0;
    const char* buf = json_data.data();
    size_t total = json_data.size();
    while (written < total) {
        ssize_t res = write(fd, buf + written, total - written);
        if (res <= 0) {
            close(fd);
            unlink(temp_path.c_str());
            out_error = "Failed writing to temporary manifest file: " + temp_path;
            return false;
        }
        written += static_cast<size_t>(res);
    }

    if (fsync(fd) != 0) {
        close(fd);
        unlink(temp_path.c_str());
        out_error = "Failed to fsync temporary manifest file: " + temp_path;
        return false;
    }
    close(fd);

    if (rename(temp_path.c_str(), target_path.c_str()) != 0) {
        unlink(temp_path.c_str());
        out_error = "Failed to rename temporary manifest to target: " + target_path;
        return false;
    }

    // Sync parent directory to persist directory entry
    size_t last_slash = target_path.find_last_of("/\\");
    std::string parent_dir = (last_slash == std::string::npos) ? "." : target_path.substr(0, last_slash);
    if (parent_dir.empty()) parent_dir = ".";
    int dir_fd = open(parent_dir.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    return true;
}

ManifestStatus validate_chunk_geometry(
    const PkgManifest& manifest,
    const std::vector<uint64_t>& actual_part_sizes,
    std::string& out_error
) {
    if (manifest.chunk_size_bytes == 0) {
        out_error = "Manifest chunk_size_bytes is 0";
        return ManifestStatus::GEOMETRY_MISMATCH;
    }
    if (manifest.total_size_bytes == 0) {
        out_error = "Manifest total_size_bytes is 0";
        return ManifestStatus::GEOMETRY_MISMATCH;
    }
    if (manifest.chunk_count == 0) {
        out_error = "Manifest chunk_count is 0";
        return ManifestStatus::GEOMETRY_MISMATCH;
    }

    uint64_t expected_chunk_count = (manifest.total_size_bytes + manifest.chunk_size_bytes - 1) / manifest.chunk_size_bytes;
    if (manifest.chunk_count != expected_chunk_count) {
        out_error = "Manifest chunk_count (" + std::to_string(manifest.chunk_count) +
                    ") does not match expected chunk count (" + std::to_string(expected_chunk_count) +
                    ") derived from total_size_bytes and chunk_size_bytes";
        return ManifestStatus::CHUNK_COUNT_MISMATCH;
    }

    if (actual_part_sizes.size() != manifest.chunk_count) {
        out_error = "Actual part count (" + std::to_string(actual_part_sizes.size()) +
                    ") does not match manifest chunk_count (" + std::to_string(manifest.chunk_count) + ")";
        return ManifestStatus::CHUNK_COUNT_MISMATCH;
    }

    uint64_t total_actual_size = 0;
    for (size_t i = 0; i < actual_part_sizes.size(); ++i) {
        uint64_t part_sz = actual_part_sizes[i];
        total_actual_size += part_sz;

        if (i + 1 < actual_part_sizes.size()) {
            // Non-final part
            if (part_sz != manifest.chunk_size_bytes) {
                out_error = "Non-final part " + std::to_string(i + 1) + " size (" + std::to_string(part_sz) +
                            " bytes) does not equal chunk_size_bytes (" + std::to_string(manifest.chunk_size_bytes) + ")";
                return ManifestStatus::GEOMETRY_MISMATCH;
            }
        } else {
            // Final part
            uint64_t expected_final_size = manifest.total_size_bytes - (manifest.chunk_count - 1) * manifest.chunk_size_bytes;
            if (part_sz != expected_final_size) {
                out_error = "Final part size (" + std::to_string(part_sz) +
                            " bytes) does not match expected remainder size (" + std::to_string(expected_final_size) + ")";
                return ManifestStatus::GEOMETRY_MISMATCH;
            }
            if (part_sz == 0 || part_sz > manifest.chunk_size_bytes) {
                out_error = "Final part size (" + std::to_string(part_sz) + ") is invalid (must be > 0 and <= chunk_size_bytes)";
                return ManifestStatus::GEOMETRY_MISMATCH;
            }
        }
    }

    if (total_actual_size != manifest.total_size_bytes) {
        out_error = "Sum of actual part sizes (" + std::to_string(total_actual_size) +
                    " bytes) does not equal manifest total_size_bytes (" + std::to_string(manifest.total_size_bytes) + ")";
        return ManifestStatus::TOTAL_SIZE_MISMATCH;
    }

    return ManifestStatus::OK;
}

} // namespace manifest
