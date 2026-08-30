#include "../common/sha256.hpp"
#include "../common/manifest.hpp"
#include "../splitter/splitter_core.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

void test_sha256_standard_vectors() {
    // NIST standard test vectors
    // 1. Empty string
    assert(crypto::SHA256::hash_string("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. "abc"
    assert(crypto::SHA256::hash_string("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 3. Multi-block string
    std::string multi_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    assert(crypto::SHA256::hash_string(multi_block) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // 4. Million 'a' characters
    {
        crypto::SHA256 ctx;
        std::string chunk(1000, 'a');
        for (int i = 0; i < 1000; ++i) {
            ctx.update(chunk);
        }
        assert(ctx.final_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    // 5. Incremental streaming vs single-shot equality
    {
        std::string payload = "The quick brown fox jumps over the lazy dog";
        crypto::SHA256 ctx;
        for (char c : payload) {
            ctx.update(&c, 1);
        }
        std::string stream_hash = ctx.final_hex();
        std::string single_hash = crypto::SHA256::hash_string(payload);
        assert(stream_hash == single_hash);
        assert(stream_hash == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    }

    std::cout << "[PASS] test_sha256_standard_vectors\n";
}

void test_manifest_serialization_and_parsing_roundtrip() {
    manifest::PkgManifest original;
    original.schema_version = 1;
    original.original_filename = "Game \"Special\" \\ Edition\n.pkg";
    original.package_base_name = "Game_Special_Edition";
    original.total_size_bytes = 47244640256ULL;
    original.chunk_size_bytes = 4294967296ULL;
    original.chunk_count = 11;
    original.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    std::string json_str = manifest::serialize_manifest_json(original);
    assert(!json_str.empty());

    manifest::PkgManifest parsed;
    std::string err;
    manifest::ManifestStatus status = manifest::parse_manifest_json(json_str, parsed, err);
    assert(status == manifest::ManifestStatus::OK);
    assert(err.empty());

    assert(parsed.schema_version == original.schema_version);
    assert(parsed.original_filename == original.original_filename);
    assert(parsed.package_base_name == original.package_base_name);
    assert(parsed.total_size_bytes == original.total_size_bytes);
    assert(parsed.chunk_size_bytes == original.chunk_size_bytes);
    assert(parsed.chunk_count == original.chunk_count);
    assert(parsed.sha256 == original.sha256);

    std::cout << "[PASS] test_manifest_serialization_and_parsing_roundtrip\n";
}

void test_manifest_parser_hardening() {
    manifest::PkgManifest m;
    std::string err;

    // 1. Duplicate key rejection
    std::string dup_key_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 100,
        "total_size_bytes": 200,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    })json";
    assert(manifest::parse_manifest_json(dup_key_json, m, err) == manifest::ManifestStatus::DUPLICATE_KEY);

    // 2. Unknown key rejection in Version 1
    std::string unknown_key_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 100,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "unrecognized_field": 123
    })json";
    assert(manifest::parse_manifest_json(unknown_key_json, m, err) == manifest::ManifestStatus::UNKNOWN_KEY);

    // 3. Integer overflow rejection
    std::string overflow_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 18446744073709551616,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    })json";
    assert(manifest::parse_manifest_json(overflow_json, m, err) == manifest::ManifestStatus::INTEGER_OVERFLOW);

    // 4. Negative numbers rejection
    std::string negative_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": -100,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    })json";
    assert(manifest::parse_manifest_json(negative_json, m, err) == manifest::ManifestStatus::TYPE_MISMATCH);

    // 5. Trailing non-whitespace rejection
    std::string trailing_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 100,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    })json" " extra_garbage";
    assert(manifest::parse_manifest_json(trailing_json, m, err) == manifest::ManifestStatus::INVALID_JSON_SYNTAX);

    // 6. Invalid escape sequence
    std::string invalid_esc_json = "{\n"
        "  \"schema_version\": 1,\n"
        "  \"original_filename\": \"Game\\k.pkg\",\n"
        "  \"package_base_name\": \"Game\",\n"
        "  \"total_size_bytes\": 100,\n"
        "  \"chunk_size_bytes\": 50,\n"
        "  \"chunk_count\": 2,\n"
        "  \"sha256\": \"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"\n"
        "}";
    assert(manifest::parse_manifest_json(invalid_esc_json, m, err) == manifest::ManifestStatus::INVALID_ESCAPE_SEQUENCE);

    // 7. Invalid SHA-256 (uppercase or wrong length)
    std::string upper_hash_json = R"json({
        "schema_version": 1,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 100,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"
    })json";
    assert(manifest::parse_manifest_json(upper_hash_json, m, err) == manifest::ManifestStatus::INVALID_HASH_FORMAT);

    // 8. Size cap (> 64 KiB) rejection
    std::string large_manifest(70 * 1024, ' ');
    assert(manifest::parse_manifest_json(large_manifest, m, err) == manifest::ManifestStatus::FILE_TOO_LARGE);

    // 9. Unsupported schema version
    std::string v2_json = R"json({
        "schema_version": 2,
        "original_filename": "Game.pkg",
        "package_base_name": "Game",
        "total_size_bytes": 100,
        "chunk_size_bytes": 50,
        "chunk_count": 2,
        "sha256": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    })json";
    assert(manifest::parse_manifest_json(v2_json, m, err) == manifest::ManifestStatus::UNSUPPORTED_SCHEMA_VERSION);

    std::cout << "[PASS] test_manifest_parser_hardening\n";
}

void test_manifest_chunk_geometry_validation() {
    manifest::PkgManifest m;
    m.schema_version = 1;
    m.original_filename = "Game.pkg";
    m.package_base_name = "Game";
    m.total_size_bytes = 250;
    m.chunk_size_bytes = 100;
    m.chunk_count = 3; // ceil(250 / 100) = 3
    m.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    std::string err;

    // 1. Valid geometry: [100, 100, 50]
    std::vector<uint64_t> valid_sizes = {100, 100, 50};
    assert(manifest::validate_chunk_geometry(m, valid_sizes, err) == manifest::ManifestStatus::OK);

    // 2. Non-final part size wrong: [100, 90, 60]
    std::vector<uint64_t> non_final_wrong = {100, 90, 60};
    assert(manifest::validate_chunk_geometry(m, non_final_wrong, err) == manifest::ManifestStatus::GEOMETRY_MISMATCH);

    // 3. Final part size wrong: [100, 100, 40] -> total 240 != 250
    std::vector<uint64_t> final_wrong = {100, 100, 40};
    assert(manifest::validate_chunk_geometry(m, final_wrong, err) == manifest::ManifestStatus::GEOMETRY_MISMATCH);

    // 4. Wrong part count: [100, 100]
    std::vector<uint64_t> count_wrong = {100, 100};
    assert(manifest::validate_chunk_geometry(m, count_wrong, err) == manifest::ManifestStatus::CHUNK_COUNT_MISMATCH);

    // 5. Inconsistent manifest geometry (chunk_count claim does not match total / chunk_size)
    manifest::PkgManifest inconsistent_m = m;
    inconsistent_m.chunk_count = 2; // claimed 2, but ceil(250 / 100) = 3
    assert(manifest::validate_chunk_geometry(inconsistent_m, valid_sizes, err) == manifest::ManifestStatus::CHUNK_COUNT_MISMATCH);

    std::cout << "[PASS] test_manifest_chunk_geometry_validation\n";
}

void test_splitter_atomic_manifest_lifecycle() {
    std::string test_dir = "/tmp/pkg_splitter_test_manifest_lifecycle";
    mkdir(test_dir.c_str(), 0777);

    std::string src_file = test_dir + "/LifeTest.pkg";
    {
        std::ofstream out(src_file, std::ios::binary);
        std::string data = "Hello, PS4 PKG Manifest Verification World! Chunk 1. Chunk 2.";
        out.write(data.data(), data.size());
    }

    splitter::SplitOptions options;
    options.chunk_size_bytes = 20;
    options.output_dir = test_dir;
    options.force_overwrite = true;

    auto result = splitter::split_file(src_file, options);
    assert(result.status == splitter::SplitStatus::SUCCESS);
    assert(!result.manifest_path.empty());
    assert(splitter::file_exists(result.manifest_path));

    // Verify manifest contents
    manifest::PkgManifest read_m;
    std::string err;
    assert(manifest::read_manifest_file(result.manifest_path, read_m, err) == manifest::ManifestStatus::OK);
    assert(read_m.package_base_name == "LifeTest");
    assert(read_m.chunk_size_bytes == 20);
    assert(read_m.chunk_count == result.parts_count);
    assert(read_m.total_size_bytes == result.total_bytes_read);
    assert(read_m.sha256 == result.sha256);

    // Verify hash matches full source file hash
    std::ifstream src_in(src_file, std::ios::binary);
    std::string src_content((std::istreambuf_iterator<char>(src_in)), std::istreambuf_iterator<char>());
    assert(read_m.sha256 == crypto::SHA256::hash_string(src_content));

    // Overwrite test with --force: old manifest invalidated before split
    options.chunk_size_bytes = 30; // Fewer parts
    auto result2 = splitter::split_file(src_file, options);
    assert(result2.status == splitter::SplitStatus::SUCCESS);
    assert(result2.parts_count < result.parts_count);

    manifest::PkgManifest read_m2;
    assert(manifest::read_manifest_file(result2.manifest_path, read_m2, err) == manifest::ManifestStatus::OK);
    assert(read_m2.chunk_size_bytes == 30);
    assert(read_m2.chunk_count == result2.parts_count);

    // Cleanup
    for (const auto& p : result.generated_parts) std::remove(p.c_str());
    for (const auto& p : result2.generated_parts) std::remove(p.c_str());
    std::remove(result.manifest_path.c_str());
    std::remove(src_file.c_str());
    rmdir(test_dir.c_str());

    std::cout << "[PASS] test_splitter_atomic_manifest_lifecycle\n";
}
