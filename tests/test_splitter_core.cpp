#include "../splitter/splitter_core.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

static void cleanup_test_dir(const std::string& dir) {
    // Delete any files in dir and remove dir
    std::string p1 = dir + "/TestGame_001.pkgpart";
    std::string p2 = dir + "/TestGame_002.pkgpart";
    std::string p3 = dir + "/TestGame_003.pkgpart";
    std::string p4 = dir + "/TestGame_004.pkgpart";
    std::string src = dir + "/TestGame.pkg";
    std::string empty = dir + "/Empty.pkg";

    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(p3.c_str());
    std::remove(p4.c_str());
    std::remove(src.c_str());
    std::remove(empty.c_str());
    rmdir(dir.c_str());
}

void test_splitter_path_utilities() {
    assert(splitter::extract_base_name("Game.pkg") == "Game");
    assert(splitter::extract_base_name("/path/to/MyGame.v1.0.pkg") == "MyGame.v1.0");
    assert(splitter::extract_base_name("C:\\Games\\FinalFantasy.pkg") == "FinalFantasy");
    assert(splitter::extract_base_name("NoExtension") == "NoExtension");
    assert(splitter::extract_base_name(".hidden") == ".hidden");

    assert(splitter::extract_directory("/path/to/Game.pkg") == "/path/to/");
    assert(splitter::extract_directory("Game.pkg") == "");
    assert(splitter::extract_directory("C:\\Games\\Game.pkg") == "C:\\Games\\");

    assert(splitter::get_chunk_file_name("Game", 1) == "Game_001.pkgpart");
    assert(splitter::get_chunk_file_name("Game", 42) == "Game_042.pkgpart");
    assert(splitter::get_chunk_file_name("Game", 1000) == "Game_1000.pkgpart");

    assert(splitter::get_chunk_file_path("/tmp/out", "Game", 1) == "/tmp/out/Game_001.pkgpart");
    assert(splitter::get_chunk_file_path("/tmp/out/", "Game", 1) == "/tmp/out/Game_001.pkgpart");
    assert(splitter::get_chunk_file_path("", "Game", 1) == "Game_001.pkgpart");

    // Overflow conversion test
    uint64_t bytes = 0;
    assert(splitter::mb_to_bytes(15000, bytes));
    assert(bytes == 15000000000ULL);
    assert(!splitter::mb_to_bytes(UINT64_MAX / 500000, bytes));

    std::cout << "[PASS] test_splitter_path_utilities\n";
}

void test_splitter_exact_boundary() {
    std::string test_dir = "/tmp/pkg_splitter_test_boundary";
    mkdir(test_dir.c_str(), 0777);

    std::string input_path = test_dir + "/TestGame.pkg";
    // Create exactly 200 bytes file
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(200, 'A');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100; // Exact multiple: exactly 2 chunks of 100 bytes each
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 2);
    assert(result.total_bytes_read == 200);
    assert(result.generated_parts.size() == 2);

    // Verify part 1 and part 2 exist and are exactly 100 bytes
    std::string p1 = test_dir + "/TestGame_001.pkgpart";
    std::string p2 = test_dir + "/TestGame_002.pkgpart";
    std::string p3 = test_dir + "/TestGame_003.pkgpart";

    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    assert(!splitter::file_exists(p3)); // FIX #8: Ensure NO trailing empty part 3 is created!

    struct stat st1, st2;
    stat(p1.c_str(), &st1);
    stat(p2.c_str(), &st2);
    assert(st1.st_size == 100);
    assert(st2.st_size == 100);

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_exact_boundary (Fix #8 verified: no trailing empty part)\n";
}

void test_splitter_non_boundary() {
    std::string test_dir = "/tmp/pkg_splitter_test_nonboundary";
    mkdir(test_dir.c_str(), 0777);

    std::string input_path = test_dir + "/TestGame.pkg";
    // Create 250 bytes file (100 + 100 + 50)
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(250, 'B');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 3);
    assert(result.total_bytes_read == 250);

    std::string p1 = test_dir + "/TestGame_001.pkgpart";
    std::string p2 = test_dir + "/TestGame_002.pkgpart";
    std::string p3 = test_dir + "/TestGame_003.pkgpart";
    std::string p4 = test_dir + "/TestGame_004.pkgpart";

    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    assert(splitter::file_exists(p3));
    assert(!splitter::file_exists(p4));

    struct stat st1, st2, st3;
    stat(p1.c_str(), &st1);
    stat(p2.c_str(), &st2);
    stat(p3.c_str(), &st3);
    assert(st1.st_size == 100);
    assert(st2.st_size == 100);
    assert(st3.st_size == 50);

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_non_boundary\n";
}

void test_splitter_zero_byte_and_invalid_args() {
    std::string test_dir = "/tmp/pkg_splitter_test_zero";
    mkdir(test_dir.c_str(), 0777);

    std::string empty_path = test_dir + "/Empty.pkg";
    {
        std::ofstream src(empty_path, std::ios::binary);
        // 0-byte file
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;

    // Zero-byte input
    auto result_empty = splitter::split_file(empty_path, options);
    assert(result_empty.status == splitter::SplitStatus::EMPTY_INPUT);
    assert(result_empty.parts_count == 0);
    assert(!splitter::file_exists(test_dir + "/Empty_001.pkgpart"));

    // Invalid chunk size = 0
    options.chunk_size_bytes = 0;
    auto result_zero_chunk = splitter::split_file(empty_path, options);
    assert(result_zero_chunk.status == splitter::SplitStatus::INVALID_CHUNK_SIZE);

    // Non-existent input file
    options.chunk_size_bytes = 100;
    auto result_no_file = splitter::split_file(test_dir + "/NonExistent.pkg", options);
    assert(result_no_file.status == splitter::SplitStatus::INPUT_OPEN_ERROR);

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_zero_byte_and_invalid_args\n";
}

void test_splitter_overwrite_guard() {
    std::string test_dir = "/tmp/pkg_splitter_test_overwrite";
    mkdir(test_dir.c_str(), 0777);

    std::string input_path = test_dir + "/TestGame.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(150, 'C');
        src.write(dummy.data(), dummy.size());
    }

    // Pre-create an existing part file
    std::string pre_existing = test_dir + "/TestGame_001.pkgpart";
    {
        std::ofstream pre(pre_existing, std::ios::binary);
        pre.write("OLD_DATA", 8);
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = false; // Overwrite guard active (Fix #9)

    auto result_blocked = splitter::split_file(input_path, options);
    assert(result_blocked.status == splitter::SplitStatus::OUTPUT_ALREADY_EXISTS);
    assert(!result_blocked.existing_conflicts.empty());

    // Verify existing file was not modified
    {
        std::ifstream check(pre_existing, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
        assert(content == "OLD_DATA");
    }

    // Now split with force_overwrite = true
    options.force_overwrite = true;
    auto result_forced = splitter::split_file(input_path, options);
    assert(result_forced.status == splitter::SplitStatus::SUCCESS);
    assert(result_forced.parts_count == 2);

    struct stat st1;
    stat(pre_existing.c_str(), &st1);
    assert(st1.st_size == 100); // Successfully overwritten with new chunk

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_overwrite_guard (Fix #9 verified)\n";
}

int main() {
    test_splitter_path_utilities();
    test_splitter_exact_boundary();
    test_splitter_non_boundary();
    test_splitter_zero_byte_and_invalid_args();
    test_splitter_overwrite_guard();
    std::cout << "All Splitter Core tests passed successfully!\n";
    return 0;
}
