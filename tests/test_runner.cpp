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

// -------------------------------------------------------------
// Merger Unit Tests (Phase 1 & 2)
// -------------------------------------------------------------

void test_merger_filename_parser() {
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
    std::cout << "[PASS] test_merger_filename_parser\n";
}

void test_merger_filtering_and_sorting() {
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
    std::cout << "[PASS] test_merger_filtering_and_sorting\n";
}

void test_merger_multi_game_rejection() {
    std::vector<std::string> mixed_files = {
        "GameA_001.pkgpart",
        "GameA_002.pkgpart",
        "GameB_001.pkgpart"
    };

    auto result = merger::validate_and_prepare_parts(mixed_files);
    assert(result.status == merger::ValidationStatus::MULTIPLE_BASE_NAMES);
    assert(result.detected_base_names.size() == 2);
    assert(!result.error_message.empty());
    std::cout << "[PASS] test_merger_multi_game_rejection\n";
}

void test_merger_missing_and_duplicate_parts() {
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

    std::cout << "[PASS] test_merger_missing_and_duplicate_parts\n";
}

void test_merger_phase2_output_and_merge() {
    std::string test_dir = "/tmp/pkg_merger_test";
    mkdir(test_dir.c_str(), 0777);

    std::string part1_path = test_dir + "/TestGame_001.pkgpart";
    std::string part2_path = test_dir + "/TestGame_002.pkgpart";
    std::string part3_path = test_dir + "/TestGame_003.pkgpart";
    std::string output_path = test_dir + "/TestGame.pkg";

    std::string manifest_path = test_dir + "/TestGame.manifest.json";

    std::remove(part1_path.c_str());
    std::remove(part2_path.c_str());
    std::remove(part3_path.c_str());
    std::remove(output_path.c_str());
    std::remove(manifest_path.c_str());

    {
        std::ofstream p1(part1_path, std::ios::binary);
        p1.write("CHUNK1_DATA_", 12);
        std::ofstream p2(part2_path, std::ios::binary);
        p2.write("CHUNK2_DATA_", 12);
        std::ofstream p3(part3_path, std::ios::binary);
        p3.write("CHUNK3_DATA!", 12);

        std::string full_mock = "CHUNK1_DATA_CHUNK2_DATA_CHUNK3_DATA!";
        std::string mock_hash = crypto::SHA256::hash_string(full_mock);

        manifest::PkgManifest m;
        m.schema_version = 1;
        m.original_filename = "TestGame.pkg";
        m.package_base_name = "TestGame";
        m.total_size_bytes = 36;
        m.chunk_size_bytes = 12;
        m.chunk_count = 3;
        m.sha256 = mock_hash;
        std::string m_err;
        assert(manifest::write_manifest_file_atomic(manifest_path, m, m_err));
    }

    std::vector<std::string> part_files = {
        "TestGame_001.pkgpart",
        "TestGame_002.pkgpart",
        "TestGame_003.pkgpart"
    };

    assert(merger::file_exists(part1_path));
    assert(!merger::file_exists(output_path));

    uint64_t total_size = merger::calculate_total_parts_size(test_dir, part_files);
    assert(total_size == 36);

    uint64_t req_space = 0;
    assert(merger::compute_required_space(total_size, merger::FREE_SPACE_MULTIPLIER, req_space));
    assert(req_space == 72);

    uint64_t overflow_out = 0;
    assert(!merger::compute_required_space(UINT64_MAX / 2 + 1, 2, overflow_out));

    std::string stale_temp_file = merger::get_temporary_merge_path(output_path);
    {
        std::ofstream st(stale_temp_file, std::ios::binary);
        st.write("STALE_DATA_FROM_CRASH", 21);
    }
    assert(merger::file_exists(stale_temp_file));
    assert(merger::clean_stale_temp_file(output_path));
    assert(!merger::file_exists(stale_temp_file));
    assert(merger::clean_stale_temp_file(output_path));

    uint64_t free_bytes = 0;
    assert(merger::get_available_space(test_dir, free_bytes));
    assert(free_bytes > 0);

    auto progress_cb = [](uint64_t processed, uint64_t total) {
        assert(total == 36);
        assert(processed <= total);
    };

    auto res = merger::perform_merge(test_dir, part_files, output_path, progress_cb);
    assert(res.status == merger::MergeStatus::SUCCESS);
    assert(res.bytes_written == 36);
    assert(merger::file_exists(output_path));
    assert(!merger::file_exists(merger::get_temporary_merge_path(output_path)));

    {
        std::ifstream merged(output_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(merged)), std::istreambuf_iterator<char>());
        assert(content == "CHUNK1_DATA_CHUNK2_DATA_CHUNK3_DATA!");
    }

    {
        std::ofstream existing(output_path, std::ios::binary);
        existing.write("PREVIOUS_EXISTING_PKG", 21);
    }

    std::vector<std::string> broken_parts = {
        "TestGame_001.pkgpart",
        "TestGame_NonExistent_002.pkgpart"
    };
    auto fail_res = merger::perform_merge(test_dir, broken_parts, output_path);
    assert(fail_res.status == merger::MergeStatus::INPUT_OPEN_ERROR);
    assert(!merger::file_exists(merger::get_temporary_merge_path(output_path)));

    {
        std::ifstream untouched(output_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(untouched)), std::istreambuf_iterator<char>());
        assert(content == "PREVIOUS_EXISTING_PKG");
    }

    std::string rel_output = "RelGame.pkg";
    std::remove(rel_output.c_str());
    auto rel_res = merger::perform_merge(test_dir, part_files, rel_output);
    assert(rel_res.status == merger::MergeStatus::SUCCESS);
    assert(merger::file_exists(rel_output));
    assert(!merger::file_exists(merger::get_temporary_merge_path(rel_output)));
    std::remove(rel_output.c_str());

    std::remove(part1_path.c_str());
    std::remove(part2_path.c_str());
    std::remove(part3_path.c_str());
    std::remove(output_path.c_str());
    std::remove(manifest_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_merger_phase2_output_and_merge\n";
}

// -------------------------------------------------------------
// Splitter Unit Tests (Phase 3)
// -------------------------------------------------------------

static void cleanup_test_dir(const std::string& dir) {
    std::string p1 = dir + "/TestGame_001.pkgpart";
    std::string p2 = dir + "/TestGame_002.pkgpart";
    std::string p3 = dir + "/TestGame_003.pkgpart";
    std::string p4 = dir + "/TestGame_004.pkgpart";
    std::string src = dir + "/TestGame.pkg";
    std::string empty = dir + "/Empty.pkg";
    std::string m1 = dir + "/TestGame.manifest.json";
    std::string m2 = dir + "/Empty.manifest.json";

    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(p3.c_str());
    std::remove(p4.c_str());
    std::remove(src.c_str());
    std::remove(empty.c_str());
    std::remove(m1.c_str());
    std::remove(m2.c_str());
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
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(200, 'A');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 2);
    assert(result.total_bytes_read == 200);
    assert(result.generated_parts.size() == 2);

    std::string p1 = test_dir + "/TestGame_001.pkgpart";
    std::string p2 = test_dir + "/TestGame_002.pkgpart";
    std::string p3 = test_dir + "/TestGame_003.pkgpart";

    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    assert(!splitter::file_exists(p3)); // FIX #8 verified

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
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;

    auto result_empty = splitter::split_file(empty_path, options);
    assert(result_empty.status == splitter::SplitStatus::EMPTY_INPUT);
    assert(result_empty.parts_count == 0);
    assert(!splitter::file_exists(test_dir + "/Empty_001.pkgpart"));

    options.chunk_size_bytes = 0;
    auto result_zero_chunk = splitter::split_file(empty_path, options);
    assert(result_zero_chunk.status == splitter::SplitStatus::INVALID_CHUNK_SIZE);

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

    std::string pre_existing = test_dir + "/TestGame_001.pkgpart";
    {
        std::ofstream pre(pre_existing, std::ios::binary);
        pre.write("OLD_DATA", 8);
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = false;

    auto result_blocked = splitter::split_file(input_path, options);
    assert(result_blocked.status == splitter::SplitStatus::OUTPUT_ALREADY_EXISTS);
    assert(!result_blocked.existing_conflicts.empty());

    {
        std::ifstream check(pre_existing, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
        assert(content == "OLD_DATA");
    }

    options.force_overwrite = true;
    auto result_forced = splitter::split_file(input_path, options);
    assert(result_forced.status == splitter::SplitStatus::SUCCESS);
    assert(result_forced.parts_count == 2);

    struct stat st1;
    stat(pre_existing.c_str(), &st1);
    assert(st1.st_size == 100);

    cleanup_test_dir(test_dir);
    std::cout << "[PASS] test_splitter_overwrite_guard (Fix #9 verified)\n";
}

void test_splitter_obsolete_parts_cleanup_on_overwrite() {
    std::string test_dir = "/tmp/pkg_splitter_test_obsolete";
    mkdir(test_dir.c_str(), 0777);

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

    std::string input_path = test_dir + "/TestGame.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(200, 'Z');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 2);

    assert(splitter::file_exists(p1));
    assert(splitter::file_exists(p2));
    struct stat st1, st2;
    stat(p1.c_str(), &st1);
    stat(p2.c_str(), &st2);
    assert(st1.st_size == 100);
    assert(st2.st_size == 100);

    // Obsolete parts 3 and 4 removed
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

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = "";
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(result.parts_count == 1);

    std::string wrong_location = nested_dir + "/SourceGame_001.pkgpart";
    std::string expected_cwd_location = "SourceGame_001.pkgpart";
    std::string expected_cwd_manifest = "SourceGame.manifest.json";
    assert(splitter::file_exists(expected_cwd_location));
    assert(splitter::file_exists(expected_cwd_manifest));

    std::remove(expected_cwd_location.c_str());
    std::remove(expected_cwd_manifest.c_str());
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

    auto matched_files = splitter::find_existing_part_files(test_dir, "Game");
    assert(matched_files.size() == 2);
    assert(matched_files[0] == game_p1);
    assert(matched_files[1] == game_p2);

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

    assert(splitter::file_exists(game_p1));
    assert(!splitter::file_exists(game_p2));

    assert(splitter::file_exists(dlc_p1));
    assert(splitter::file_exists(update_p1));

    std::remove(game_p1.c_str());
    std::remove(dlc_p1.c_str());
    std::remove(update_p1.c_str());
    std::remove(input_path.c_str());
    std::remove((test_dir + "/Game.manifest.json").c_str());
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

    chmod(test_dir.c_str(), 0555);

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(input_path, options);
    assert(result.status == splitter::SplitStatus::WRITE_ERROR);
    assert(!result.error_message.empty());

    chmod(test_dir.c_str(), 0777);
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    std::remove(input_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_splitter_obsolete_cleanup_error_propagation (Fix [P2] verified: deletion error propagated)\n";
}

void test_splitter_minimum_three_digits_suffix_filter() {
    std::string test_dir = "/tmp/pkg_splitter_test_three_digits";
    std::remove((test_dir + "/Game_1.pkgpart").c_str());
    std::remove((test_dir + "/Game_01.pkgpart").c_str());
    std::remove((test_dir + "/Game_001.pkgpart").c_str());
    std::remove((test_dir + "/Game_002.pkgpart").c_str());
    std::remove((test_dir + "/Game.pkg").c_str());
    std::remove((test_dir + "/Game.manifest.json").c_str());
    rmdir(test_dir.c_str());
    mkdir(test_dir.c_str(), 0777);

    std::string short_1 = test_dir + "/Game_1.pkgpart";
    std::string short_2 = test_dir + "/Game_01.pkgpart";
    std::string valid_1 = test_dir + "/Game_001.pkgpart";
    std::string valid_2 = test_dir + "/Game_002.pkgpart";

    {
        std::ofstream(short_1, std::ios::binary).write("SHORT_1", 7);
        std::ofstream(short_2, std::ios::binary).write("SHORT_2", 7);
        std::ofstream(valid_1, std::ios::binary).write("VALID_1", 7);
        std::ofstream(valid_2, std::ios::binary).write("VALID_2", 7);
    }

    auto matched = splitter::find_existing_part_files(test_dir, "Game");
    assert(matched.size() == 2);
    assert(matched[0] == valid_1);
    assert(matched[1] == valid_2);

    std::remove(valid_1.c_str());
    std::remove(valid_2.c_str());

    std::string input_path = test_dir + "/Game.pkg";
    {
        std::ofstream src(input_path, std::ios::binary);
        std::string dummy(100, 'M');
        src.write(dummy.data(), dummy.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 100;
    options.output_dir = test_dir;
    options.force_overwrite = false;

    auto res_normal = splitter::split_file(input_path, options);
    assert(res_normal.status == splitter::SplitStatus::SUCCESS);

    options.force_overwrite = true;
    auto res_force = splitter::split_file(input_path, options);
    assert(res_force.status == splitter::SplitStatus::SUCCESS);
    assert(splitter::file_exists(short_1));
    assert(splitter::file_exists(short_2));

    std::remove(short_1.c_str());
    std::remove(short_2.c_str());
    std::remove(valid_1.c_str());
    std::remove(input_path.c_str());
    std::remove((test_dir + "/Game.manifest.json").c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_splitter_minimum_three_digits_suffix_filter (Fix [P2] verified: <3 digits not matched)\n";
}

// -------------------------------------------------------------
// End-to-End Split-and-Merge Integration Test
// -------------------------------------------------------------

void test_split_and_merge_roundtrip() {
    std::string test_dir = "/tmp/pkg_test_roundtrip";
    mkdir(test_dir.c_str(), 0777);

    std::string original_pkg = test_dir + "/OriginalGame.pkg";
    std::string reconstructed_pkg = test_dir + "/ReconstructedGame.pkg";

    // 550 bytes data with varied byte patterns
    std::string original_data;
    original_data.reserve(550);
    for (int i = 0; i < 550; ++i) {
        original_data.push_back(static_cast<char>(i % 256));
    }

    {
        std::ofstream out(original_pkg, std::ios::binary);
        out.write(original_data.data(), original_data.size());
    }

    // Step 1: Split into 100-byte chunks (5 full chunks of 100 + 1 partial chunk of 50 = 6 chunks)
    splitter::SplitOptions split_opts;
    split_opts.chunk_size_bytes = 100;
    split_opts.output_dir = test_dir;
    split_opts.force_overwrite = true;

    auto split_res = splitter::split_file(original_pkg, split_opts);
    assert(split_res.status == splitter::SplitStatus::SUCCESS);
    assert(split_res.parts_count == 6);
    assert(split_res.total_bytes_read == 550);

    // Step 2: Validate parts through merger_core and merge back
    std::vector<std::string> part_files = {
        "OriginalGame_001.pkgpart",
        "OriginalGame_006.pkgpart",
        "OriginalGame_002.pkgpart",
        "OriginalGame_003.pkgpart",
        "OriginalGame_004.pkgpart",
        "OriginalGame_005.pkgpart"
    };

    auto merge_val = merger::validate_and_prepare_parts(part_files);
    assert(merge_val.status == merger::ValidationStatus::OK);
    assert(merge_val.sorted_files.size() == 6);

    auto merge_res = merger::perform_merge(test_dir, merge_val.sorted_files, reconstructed_pkg);
    assert(merge_res.status == merger::MergeStatus::SUCCESS);
    assert(merge_res.bytes_written == 550);

    // Verify reconstructed file matches original byte-for-byte
    {
        std::ifstream recon(reconstructed_pkg, std::ios::binary);
        std::string reconstructed_data((std::istreambuf_iterator<char>(recon)), std::istreambuf_iterator<char>());
        assert(reconstructed_data == original_data);
    }

    // Clean up
    for (const auto& part : split_res.generated_parts) {
        std::remove(part.c_str());
    }
    std::remove(original_pkg.c_str());
    std::remove(reconstructed_pkg.c_str());
    std::remove(split_res.manifest_path.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_split_and_merge_roundtrip (Desktop Split -> Merge verified byte-for-byte)\n";
}

void test_sha256_standard_vectors();
void test_manifest_serialization_and_parsing_roundtrip();
void test_manifest_parser_hardening();
void test_manifest_chunk_geometry_validation();
void test_splitter_atomic_manifest_lifecycle();
void test_merger_manifest_validation_and_errors();
void test_merger_checksum_mismatch_and_collision_safe_retention();
void test_split_manifest_merge_e2e_verification();

int main() {
    std::cout << "=== Running Merger Core Tests (Phase 1 & 2) ===\n";
    test_merger_filename_parser();
    test_merger_filtering_and_sorting();
    test_merger_multi_game_rejection();
    test_merger_missing_and_duplicate_parts();
    test_merger_phase2_output_and_merge();

    std::cout << "\n=== Running Splitter Core Tests (Phase 3) ===\n";
    test_splitter_path_utilities();
    test_splitter_exact_boundary();
    test_splitter_non_boundary();
    test_splitter_zero_byte_and_invalid_args();
    test_splitter_overwrite_guard();
    test_splitter_obsolete_parts_cleanup_on_overwrite();
    test_splitter_default_output_location();
    test_splitter_distinct_package_prefix_preservation();
    test_splitter_obsolete_cleanup_error_propagation();
    test_splitter_minimum_three_digits_suffix_filter();

    std::cout << "\n=== Running SHA-256 & Manifest Tests (Phase 4 - Fix #11) ===\n";
    test_sha256_standard_vectors();
    test_manifest_serialization_and_parsing_roundtrip();
    test_manifest_parser_hardening();
    test_manifest_chunk_geometry_validation();
    test_splitter_atomic_manifest_lifecycle();

    std::cout << "\n=== Running Integrity Verification Tests (Phase 4 - Fix #12) ===\n";
    test_merger_manifest_validation_and_errors();
    test_merger_checksum_mismatch_and_collision_safe_retention();
    test_split_manifest_merge_e2e_verification();

    std::cout << "\n=== Running Split & Merge Integration Tests ===\n";
    test_split_and_merge_roundtrip();

    std::cout << "\n>>> ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
