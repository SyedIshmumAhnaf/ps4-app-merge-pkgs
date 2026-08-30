#include "splitter_core.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <unistd.h>

#if defined(_WIN32)
#include <io.h>
#define IS_TTY(fd) _isatty(fd)
#else
#define IS_TTY(fd) isatty(fd)
#endif

static void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [options] <input-file>\n\n"
              << "Options:\n"
              << "  -c, --chunk-size <MB>   Chunk size in megabytes (default: 15000 MB)\n"
              << "  -o, --output-dir <dir>  Output directory for split .pkgpart files\n"
              << "  -f, --force             Overwrite existing .pkgpart files without confirmation\n"
              << "  -h, --help              Show this help message\n";
}

int main(int argc, char *argv[]) {
    splitter::SplitOptions options;
    uint64_t chunkSizeMB = 15000; // Default is 15 GB (15,000 MB)
    std::string inputFilePath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--chunk-size") {
            if (i + 1 < argc) {
                char* endptr = nullptr;
                unsigned long long parsed = std::strtoull(argv[++i], &endptr, 10);
                if (*endptr != '\0' || parsed == 0) {
                    std::cerr << "Error: Invalid chunk size value: " << argv[i] << "\n";
                    return 1;
                }
                chunkSizeMB = parsed;
            } else {
                std::cerr << "Error: Missing value for chunk size\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (arg == "-f" || arg == "--force") {
            options.force_overwrite = true;
        } else if (arg == "-o" || arg == "--output-dir") {
            if (i + 1 < argc) {
                options.output_dir = argv[++i];
            } else {
                std::cerr << "Error: Missing value for output directory\n";
                print_usage(argv[0]);
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        } else {
            inputFilePath = arg;
        }
    }

    if (inputFilePath.empty()) {
        std::cerr << "Error: No input file specified.\n";
        print_usage(argv[0]);
        return 1;
    }

    if (!splitter::mb_to_bytes(chunkSizeMB, options.chunk_size_bytes)) {
        std::cerr << "Error: Chunk size value is too large and caused an arithmetic overflow.\n";
        return 1;
    }

    auto progress_callback = [](uint64_t processed, uint64_t total) {
        if (total > 0) {
            double pct = (static_cast<double>(processed) / static_cast<double>(total)) * 100.0;
            std::cout << "\rSplitting... " << std::fixed << pct << "% ("
                      << (processed / 1000000) << "/" << (total / 1000000) << " MB)" << std::flush;
        }
    };

    splitter::SplitResult result = splitter::split_file(inputFilePath, options, progress_callback);

    // Handle existing output files prompt (Fix #9)
    if (result.status == splitter::SplitStatus::OUTPUT_ALREADY_EXISTS) {
        std::cout << "\nWarning: The following output part files already exist:\n";
        for (const auto& file : result.existing_conflicts) {
            std::cout << "  - " << file << "\n";
        }

        if (IS_TTY(fileno(stdin))) {
            std::cout << "Overwrite existing files? (y/N): " << std::flush;
            std::string response;
            if (std::getline(std::cin, response) && (!response.empty() && (response[0] == 'y' || response[0] == 'Y'))) {
                options.force_overwrite = true;
                result = splitter::split_file(inputFilePath, options, progress_callback);
            } else {
                std::cerr << "Operation aborted by user.\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Output files already exist. Use --force / -f to overwrite in non-interactive mode.\n";
            return 1;
        }
    }

    if (result.status != splitter::SplitStatus::SUCCESS) {
        std::cout << "\n";
        std::cerr << "Error: " << result.error_message << "\n";
        return 1;
    }

    std::cout << "\nFile successfully split into " << result.parts_count << " parts ("
              << result.total_bytes_read << " bytes total).\n";
    for (const auto& part : result.generated_parts) {
        std::cout << "  Created: " << part << "\n";
    }
    if (!result.manifest_path.empty()) {
        std::cout << "  Manifest: " << result.manifest_path << "\n";
        std::cout << "  SHA-256:  " << result.sha256 << "\n";
    }

    return 0;
}
