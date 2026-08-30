#include "../merger-app/merger_core.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>


void test_filename_parser() {
    merger::PkgPartInfo info;

    // Valid cases
    assert(merger::parse_pkgpart_filename("Game_001.pkgpart", info));
    assert(info.base_name == "Game");
    assert(info.part_index == 1);

    assert(merger::parse_pkgpart_filename("Title_Update_v1.02_042.pkgpart", info));
    assert(info.base_name == "Title_Update_v1.02");
    assert(info.part_index == 42);

    // Invalid cases (defensive tests against crash bugs)
    assert(!merger::parse_pkgpart_filename("short.pkg", info));
    assert(!merger::parse_pkgpart_filename(".pkgpart", info));
    assert(!merger::parse_pkgpart_filename("abc.pkgpart", info));
    assert(!merger::parse_pkgpart_filename("_001.pkgpart", info));
    assert(!merger::parse_pkgpart_filename("Game_000.pkgpart", info)); // 0-indexed part is invalid
    assert(!merger::parse_pkgpart_filename("Game_abc.pkgpart", info));
    assert(!merger::parse_pkgpart_filename("Game_01.pkgpart", info)); // < 3 digits
    std::cout << "[PASS] test_filename_parser\n";
}

void test_filtering_and_sorting() {
    std::vector<std::string> raw_files = {
        "readme.txt",
        "Game_003.pkgpart",
        ".DS_Store",
        "Game_001.pkgpart",
        "Game_002.pkgpart",
        "other_file.bin"
    };

    auto filtered = merger::filter_pkgpart_files(raw_files);
    assert(filtered.size() == 3);

    auto result = merger::validate_and_prepare_parts(raw_files);
    assert(result.status == merger::ValidationStatus::OK);
    assert(result.single_base_name == "Game");
    assert(result.sorted_files.size() == 3);
    assert(result.sorted_files[0] == "Game_001.pkgpart");
    assert(result.sorted_files[1] == "Game_002.pkgpart");
    assert(result.sorted_files[2] == "Game_003.pkgpart");
    std::cout << "[PASS] test_filtering_and_sorting\n";
}

void test_multi_game_rejection() {
    std::vector<std::string> mixed_files = {
        "GameA_001.pkgpart",
        "GameA_002.pkgpart",
        "GameB_001.pkgpart"
    };

    auto result = merger::validate_and_prepare_parts(mixed_files);
    assert(result.status == merger::ValidationStatus::MULTIPLE_BASE_NAMES);
    assert(result.detected_base_names.size() == 2);
    assert(!result.error_message.empty());
    std::cout << "[PASS] test_multi_game_rejection\n";
}

void test_missing_and_duplicate_parts() {
    // Missing part 2
    std::vector<std::string> missing_parts = {
        "Game_001.pkgpart",
        "Game_003.pkgpart"
    };
    auto result_missing = merger::validate_and_prepare_parts(missing_parts);
    assert(result_missing.status == merger::ValidationStatus::NON_CONTIGUOUS_PARTS);

    // Duplicate part 1
    std::vector<std::string> duplicate_parts = {
        "Game_001.pkgpart",
        "Game_001.pkgpart",
        "Game_002.pkgpart"
    };
    auto result_dup = merger::validate_and_prepare_parts(duplicate_parts);
    assert(result_dup.status == merger::ValidationStatus::DUPLICATE_PARTS);

    // Starts at 2 instead of 1
    std::vector<std::string> not_starting_at_one = {
        "Game_002.pkgpart",
        "Game_003.pkgpart"
    };
    auto result_not_one = merger::validate_and_prepare_parts(not_starting_at_one);
    assert(result_not_one.status == merger::ValidationStatus::NON_CONTIGUOUS_PARTS);

    std::cout << "[PASS] test_missing_and_duplicate_parts\n";
}

void test_phase2_output_and_merge() {
    // Create temporary directory for testing
    std::string test_dir = "/tmp/pkg_merger_test";
    mkdir(test_dir.c_str(), 0777);

    std::string part1_path = test_dir + "/TestGame_001.pkgpart";
    std::string part2_path = test_dir + "/TestGame_002.pkgpart";
    std::string part3_path = test_dir + "/TestGame_003.pkgpart";
    std::string output_path = test_dir + "/TestGame.pkg";

    // Clean up potential leftovers
    std::remove(part1_path.c_str());
    std::remove(part2_path.c_str());
    std::remove(part3_path.c_str());
    std::remove(output_path.c_str());

    // Write mock data to part files
    {
        std::ofstream p1(part1_path, std::ios::binary);
        p1.write("CHUNK1_DATA_", 12);
        std::ofstream p2(part2_path, std::ios::binary);
        p2.write("CHUNK2_DATA_", 12);
        std::ofstream p3(part3_path, std::ios::binary);
        p3.write("CHUNK3_DATA!", 12);
    }

    std::vector<std::string> part_files = {
        "TestGame_001.pkgpart",
        "TestGame_002.pkgpart",
        "TestGame_003.pkgpart"
    };

    // Test file_exists
    assert(merger::file_exists(part1_path));
    assert(!merger::file_exists(output_path));

    // Test calculate_total_parts_size
    uint64_t total_size = merger::calculate_total_parts_size(test_dir, part_files);
    assert(total_size == 36);

    // Test compute_required_space and overflow handling (Fix #7 / P2)
    uint64_t req_space = 0;
    assert(merger::compute_required_space(total_size, merger::FREE_SPACE_MULTIPLIER, req_space));
    assert(req_space == 72);

    uint64_t overflow_out = 0;
    assert(!merger::compute_required_space(UINT64_MAX / 2 + 1, 2, overflow_out));

    // Test clean_stale_temp_file: simulate stale temp file left after an interrupted run
    std::string stale_temp_file = merger::get_temporary_merge_path(output_path);
    {
        std::ofstream st(stale_temp_file, std::ios::binary);
        st.write("STALE_DATA_FROM_CRASH", 21);
    }
    assert(merger::file_exists(stale_temp_file));
    assert(merger::clean_stale_temp_file(output_path));
    assert(!merger::file_exists(stale_temp_file)); // Stale temp cleaned up prior to space check

    // Test get_available_space
    uint64_t free_bytes = 0;
    assert(merger::get_available_space(test_dir, free_bytes));
    assert(free_bytes > 0);

    // Test perform_merge happy path (atomically written, fsync'd, and renamed from .tmp.merging)
    uint64_t progress_last_processed = 0;
    auto progress_cb = [](uint64_t processed, uint64_t total) {
        assert(total == 36);
        assert(processed <= total);
    };

    auto res = merger::perform_merge(test_dir, part_files, output_path, progress_cb);
    (void)progress_last_processed;
    assert(res.status == merger::MergeStatus::SUCCESS);
    assert(res.bytes_written == 36);
    assert(merger::file_exists(output_path));
    assert(!merger::file_exists(merger::get_temporary_merge_path(output_path))); // temp file cleanly removed / renamed

    // Verify content
    {
        std::ifstream merged(output_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(merged)), std::istreambuf_iterator<char>());
        assert(content == "CHUNK1_DATA_CHUNK2_DATA_CHUNK3_DATA!");
    }

    // Test overwrite preservation on failure (Fix #5 & Fix #6 / P1):
    // When output_path already has existing valid content and a merge fails mid-way,
    // the temporary file is deleted and the existing file at output_path remains untouched!
    {
        // Existing file has valid content "PREVIOUS_EXISTING_PKG"
        std::ofstream existing(output_path, std::ios::binary);
        existing.write("PREVIOUS_EXISTING_PKG", 21);
    }

    std::vector<std::string> broken_parts = {
        "TestGame_001.pkgpart",
        "TestGame_NonExistent_002.pkgpart"
    };
    auto fail_res = merger::perform_merge(test_dir, broken_parts, output_path);
    assert(fail_res.status == merger::MergeStatus::INPUT_OPEN_ERROR);
    assert(!merger::file_exists(merger::get_temporary_merge_path(output_path))); // temp file cleaned up

    // Verify existing file at output_path was NOT truncated or destroyed
    {
        std::ifstream untouched(output_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(untouched)), std::istreambuf_iterator<char>());
        assert(content == "PREVIOUS_EXISTING_PKG");
    }

    // Test perform_merge with relative output path (Fix #6 / P2: relative output parent dir sync)
    std::string rel_output = "RelGame.pkg";
    std::remove(rel_output.c_str());
    auto rel_res = merger::perform_merge(test_dir, part_files, rel_output);
    assert(rel_res.status == merger::MergeStatus::SUCCESS);
    assert(merger::file_exists(rel_output));
    assert(!merger::file_exists(merger::get_temporary_merge_path(rel_output)));
    std::remove(rel_output.c_str());

    // Cleanup
    std::remove(part1_path.c_str());
    std::remove(part2_path.c_str());
    std::remove(part3_path.c_str());
    std::remove(output_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_phase2_output_and_merge\n";
}




int main() {
    test_filename_parser();
    test_filtering_and_sorting();
    test_multi_game_rejection();
    test_missing_and_duplicate_parts();
    test_phase2_output_and_merge();
    std::cout << "All Phase 1 & Phase 2 unit tests passed successfully!\n";
    return 0;
}

