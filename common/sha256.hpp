#pragma once

#include <string>
#include <cstdint>
#include <cstddef>
#include <array>

namespace crypto {

class SHA256 {
public:
    static constexpr size_t DIGEST_SIZE = 32;
    static constexpr size_t BLOCK_SIZE = 64;

    SHA256();
    void init();
    void update(const void* data, size_t len);
    void update(const std::string& data);

    // Finalizes and returns the 32 raw bytes of digest
    std::array<uint8_t, DIGEST_SIZE> final_raw();

    // Finalizes and returns the lowercase 64-character hex string
    std::string final_hex();

    // Convenience one-shot hashing helpers
    static std::string hash_string(const std::string& input);
    static std::string hash_bytes(const void* data, size_t len);

private:
    void transform(const uint8_t block[64]);

    uint32_t state_[8];
    uint64_t bit_count_;
    uint8_t buffer_[64];
};

} // namespace crypto
