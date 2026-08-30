#include "../merger-app/merger_core.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

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

int main() {
    test_filename_parser();
    test_filtering_and_sorting();
    test_multi_game_rejection();
    test_missing_and_duplicate_parts();
    std::cout << "All Phase 1 unit tests passed successfully!\n";
    return 0;
}
