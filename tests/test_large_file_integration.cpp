#include "../merger-app/merger_core.hpp"
#include "../splitter/splitter_core.hpp"
#include "../common/manifest.hpp"
#include "../common/sha256.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>

// Test with 2 GiB (2 * 1024 * 1024 * 1024 = 2,147,483,648 bytes)
// with 700 MB chunk size -> produces 3 parts (2x 700MB + 1x 648MB)
// Exercises 64-bit integer arithmetic, multi-buffer iterations,
// boundary crossing, chunk geometry validation, streaming SHA-256,
// and atomic manifest publication and verification.
void test_multi_gb_split_merge_roundtrip() {
    std::cout << "--- Starting Multi-GB Integration Test (2.0 GiB) ---\n";
    std::string test_dir = "/tmp/pkg_test_large_file_integration";

    // Clean any prior artifacts
    std::remove((test_dir + "/LargeGame_001.pkgpart").c_str());
    std::remove((test_dir + "/LargeGame_002.pkgpart").c_str());
    std::remove((test_dir + "/LargeGame_003.pkgpart").c_str());
    std::remove((test_dir + "/LargeGame.manifest.json").c_str());
    std::remove((test_dir + "/LargeGame.pkg").c_str());
    std::remove((test_dir + "/LargeGame_Reconstructed.pkg").c_str());
    rmdir(test_dir.c_str());
    mkdir(test_dir.c_str(), 0777);

    std::string original_pkg = test_dir + "/LargeGame.pkg";
    std::string reconstructed_pkg = test_dir + "/LargeGame_Reconstructed.pkg";

    const uint64_t total_size = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2 GiB
    const size_t buffer_size = 4 * 1024 * 1024; // 4 MiB buffer
    const uint64_t chunk_size = 700ULL * 1024ULL * 1024ULL; // 700 MiB chunks

    std::cout << "1. Generating 2 GiB pseudo-random binary dataset with streaming SHA-256...\n";
    auto t0 = std::chrono::steady_clock::now();

    crypto::SHA256 gen_hasher;
    std::vector<uint8_t> buffer(buffer_size);
    for (size_t i = 0; i < buffer_size; ++i) {
        buffer[i] = static_cast<uint8_t>((i * 131 + 59) & 0xFF);
    }

    {
        std::ofstream out(original_pkg, std::ios::binary);
        assert(out.is_open());
        uint64_t remaining = total_size;
        uint32_t round = 0;
        while (remaining > 0) {
            size_t to_write = static_cast<size_t>(std::min(static_cast<uint64_t>(buffer_size), remaining));
            buffer[0] = static_cast<uint8_t>(round & 0xFF);
            buffer[1] = static_cast<uint8_t>((round >> 8) & 0xFF);
            out.write(reinterpret_cast<const char*>(buffer.data()), to_write);
            assert(out.good());
            gen_hasher.update(buffer.data(), to_write);
            remaining -= to_write;
            round++;
        }
        out.close();
    }
    std::string expected_hash = gen_hasher.final_hex();

    auto t1 = std::chrono::steady_clock::now();
    double gen_sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "   Generated 2 GiB in " << gen_sec << "s. Expected SHA-256: " << expected_hash << "\n";

    // Step 2: Split using splitter_core into 700 MiB parts
    std::cout << "2. Splitting into 700 MiB parts with manifest generation...\n";
    splitter::SplitOptions split_opts;
    split_opts.chunk_size_bytes = chunk_size;
    split_opts.output_dir = test_dir;
    split_opts.force_overwrite = true;

    auto split_res = splitter::split_file(original_pkg, split_opts);
    auto t2 = std::chrono::steady_clock::now();
    double split_sec = std::chrono::duration<double>(t2 - t1).count();
    std::cout << "   Split completed in " << split_sec << "s.\n";

    assert(split_res.status == splitter::SplitStatus::SUCCESS);
    assert(split_res.parts_count == 3); // 700MB + 700MB + 648MB = 2048MB
    assert(split_res.sha256 == expected_hash);
    assert(splitter::file_exists(split_res.manifest_path));

    // Step 3: Validate parts before merge
    std::cout << "3. Validating parts via merger_core...\n";
    auto val_res = merger::validate_and_prepare_parts(split_res.generated_parts);
    assert(val_res.status == merger::ValidationStatus::OK);
    assert(val_res.single_base_name == "LargeGame");
    assert(val_res.sorted_files.size() == split_res.parts_count);

    // Step 4: Perform streaming merge and SHA-256 verification
    std::cout << "4. Performing streaming merge with single-pass verification...\n";
    auto merge_res = merger::perform_merge(test_dir, val_res.sorted_files, reconstructed_pkg);
    auto t3 = std::chrono::steady_clock::now();
    double merge_sec = std::chrono::duration<double>(t3 - t2).count();
    std::cout << "   Merge completed in " << merge_sec << "s.\n";

    assert(merge_res.status == merger::MergeStatus::SUCCESS);
    assert(merge_res.bytes_written == total_size);
    assert(merge_res.computed_sha256 == expected_hash);
    assert(merge_res.expected_sha256 == expected_hash);
    assert(merger::file_exists(reconstructed_pkg));

    // Verify reconstructed file size exactly matches
    struct stat st;
    assert(stat(reconstructed_pkg.c_str(), &st) == 0);
    assert(static_cast<uint64_t>(st.st_size) == total_size);

    // Step 5: Clean up all multi-GB test files immediately
    std::cout << "5. Cleaning up temporary test files...\n";
    for (const auto& p : split_res.generated_parts) {
        std::remove(p.c_str());
    }
    std::remove(split_res.manifest_path.c_str());
    std::remove(original_pkg.c_str());
    std::remove(reconstructed_pkg.c_str());
    rmdir(test_dir.c_str());

    double total_sec = std::chrono::duration<double>(t3 - t0).count();
    std::cout << "[PASS] Multi-GB integration test passed! (Total round-trip time: " << total_sec << "s)\n";
}

int main() {
    test_multi_gb_split_merge_roundtrip();
    return 0;
}
