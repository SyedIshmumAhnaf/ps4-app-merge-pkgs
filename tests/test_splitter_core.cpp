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

void test_splitter_obsolete_parts_cleanup_on_overwrite() {
    std::string test_dir = "/tmp/pkg_splitter_test_obsolete";
    mkdir(test_dir.c_str(), 0777);

    // Simulate an older split that generated 4 chunks
    std::string p1 = test_dir + "/TestGame_001.pkgpart";
    std::string p2 = test_dir + "/TestGame_002.pkgpart";
    std::string p3 = test_dir + "/TestGame_003.pkgpart";
    std::string p4 = test_dir + "/TestGame_004.pkgpart";

    {
        std::ofstream(p1, std::ios::binary).write("OLD_PART_1", 10);
        std::ofstream(p2, std::ios::binary).write("OLD_PART_2", 10);
        std::ofstream(p3, std::ios::binary).write("OLD_PART_3", 10);
        std::ofstream(p4, std::ios::binary).write("OLD_PART_4", 10);
    }
    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    assert(splitter::file_exists(p3));
    assert(splitter::file_exists(p4));

    // Create a new smaller file (only 200 bytes, chunk size 100 -> only 2 chunks)
    std::string input_path = test_dir + "/TestGame.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(200, 'Z');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true; // [P1] Force overwrite must remove obsolete parts

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 2);

    // Verify parts 1 and 2 exist with new size
    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    struct stat st1, st2;
    stat(p1.c_str(), &st1);
    stat(p2.c_str(), &st2);
    assert(st1.st_size == 100);
    assert(st2.st_size == 100);

    // Verify obsolete parts 3 and 4 were cleanly REMOVED!
    assert(!splitter::file_exists(p3));
    assert(!splitter::file_exists(p4));

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_obsolete_parts_cleanup_on_overwrite (Fix [P1] verified)\n";
}

void test_splitter_default_output_location() {
    std::string nested_dir = "/tmp/pkg_splitter_nested_source";
    mkdir(nested_dir.c_str(), 0777);

    std::string input_path = nested_dir + "/SourceGame.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(100, 'X');
        src.write(dummy.data(), dummy.size());
    }

    // Split with default options (output_dir == "")
    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = ""; // Empty string should write to current working directory, NOT nested_dir
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 1);

    // Verify it was NOT written into nested_dir
    std::string wrong_location = nested_dir + "/SourceGame_001.pkgpart";
    assert(!splitter::file_exists(wrong_location));

    // Verify it WAS written to current working directory
    std::string expected_cwd_location = "SourceGame_001.pkgpart";
    assert(splitter::file_exists(expected_cwd_location));

    // Clean up
    std::remove(expected_cwd_location.c_str());
    std::remove(input_path.c_str());
    rmdir(nested_dir.c_str());

    std::cout << "[PASS] test_splitter_default_output_location (Fix [P2] verified: cwd preserved)\n";
}

void test_splitter_distinct_package_prefix_preservation() {
    std::string test_dir = "/tmp/pkg_splitter_test_prefix_guard";
    mkdir(test_dir.c_str(), 0777);

    std::string game_p1 = test_dir + "/Game_001.pkgpart";
    std::string game_p2 = test_dir + "/Game_002.pkgpart";
    std::string dlc_p1 = test_dir + "/Game_DLC_001.pkgpart";
    std::string update_p1 = test_dir + "/Game_Update_001.pkgpart";

    {
        std::ofstream(game_p1, std::ios::binary).write("GAME_PART_1", 11);
        std::ofstream(game_p2, std::ios::binary).write("GAME_PART_2", 11);
        std::ofstream(dlc_p1, std::ios::binary).write("DLC_PART_1", 10);
        std::ofstream(update_p1, std::ios::binary).write("UPDATE_PART_1", 13);
    }

    // Verify find_existing_part_files strictly matches only Game_NNN.pkgpart
    auto matched_files = splitter::find_existing_part_files(test_dir, "Game");
    assert(matched_files.size() == 2);
    assert(matched_files[0] == game_p1);
    assert(matched_files[1] == game_p2);

    // Split Game.pkg producing only 1 chunk with force overwrite enabled
    std::string input_path = test_dir + "/Game.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(100, 'G');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 1);

    // Verify Game_001 was overwritten and obsolete Game_002 was removed
    assert(splitter::file_exists(game_p1));
    assert(!splitter::file_exists(game_p2));

    // Verify Game_DLC_001.pkgpart and Game_Update_001.pkgpart were PRESERVED untouched!
    assert(splitter::file_exists(dlc_p1));
    assert(splitter::file_exists(update_p1));

    std::remove(game_p1.c_str());
    std::remove(dlc_p1.c_str());
    std::remove(update_p1.c_str());
    std::remove(input_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_splitter_distinct_package_prefix_preservation (Fix [P1] verified: DLC/Update not deleted)\n";
}

void test_splitter_obsolete_cleanup_error_propagation() {
    std::string test_dir = "/tmp/pkg_splitter_test_unlink_fail";
    mkdir(test_dir.c_str(), 0777);

    std::string p1 = test_dir + "/TestGame_001.pkgpart";
    std::string p2 = test_dir + "/TestGame_002.pkgpart";
    std::string input_path = "/tmp/TestGame.pkg";

    {
        std::ofstream(p1, std::ios::binary).write("CHUNK_1", 7);
        std::ofstream(p2, std::ios::binary).write("CHUNK_2", 7);
        std::ofstream(input_path, std::ios::binary).write("NEW_CHUNK_1", 11);
    }

    // Remove write permission on directory so unlink of p2 fails with EACCES
    chmod(test_dir.c_str(), 0555);

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    // Because p2 could not be unlinked, function must report WRITE_ERROR rather than fake SUCCESS
    assert(result.status == splitter::SplitStatus::WRITE_ERROR);
    assert(!result.error_message.empty());

    // Restore directory permissions and clean up
    chmod(test_dir.c_str(), 0777);
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(input_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_splitter_obsolete_cleanup_error_propagation (Fix [P2] verified: deletion error propagated)\n";
}

int main() {
    test_splitter_path_utilities();
    test_splitter_exact_boundary();
    test_splitter_non_boundary();
    test_splitter_zero_byte_and_invalid_args();
    test_splitter_overwrite_guard();
    test_splitter_obsolete_parts_cleanup_on_overwrite();
    test_splitter_default_output_location();
    test_splitter_distinct_package_prefix_preservation();
    test_splitter_obsolete_cleanup_error_propagation();
    std::cout << "All Splitter Core tests passed successfully!\n";
    return 0;
}
