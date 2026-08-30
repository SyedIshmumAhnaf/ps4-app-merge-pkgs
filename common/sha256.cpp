#include "sha256.hpp"
#include <cstring>
#include <iomanip>
#include <sstream>

namespace crypto {

namespace {

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // namespace

SHA256::SHA256() {
    init();
}

void SHA256::init() {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    bit_count_ = 0;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void SHA256::transform(const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void SHA256::update(const void* data, size_t len) {
    if (!data || len == 0) return;

    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t buffer_index = static_cast<size_t>((bit_count_ >> 3) & 0x3F);
    bit_count_ += static_cast<uint64_t>(len) * 8;

    if (buffer_index > 0) {
        size_t space = BLOCK_SIZE - buffer_index;
        if (len < space) {
            std::memcpy(&buffer_[buffer_index], ptr, len);
            return;
        }
        std::memcpy(&buffer_[buffer_index], ptr, space);
        transform(buffer_);
        ptr += space;
        len -= space;
    }

    while (len >= BLOCK_SIZE) {
        transform(ptr);
        ptr += BLOCK_SIZE;
        len -= BLOCK_SIZE;
    }

    if (len > 0) {
        std::memcpy(buffer_, ptr, len);
    }
}

void SHA256::update(const std::string& data) {
    update(data.data(), data.size());
}

std::array<uint8_t, SHA256::DIGEST_SIZE> SHA256::final_raw() {
    uint8_t final_block[64];
    std::memcpy(final_block, buffer_, 64);

    size_t buffer_index = static_cast<size_t>((bit_count_ >> 3) & 0x3F);
    final_block[buffer_index] = 0x80;
    ++buffer_index;

    if (buffer_index > 56) {
        std::memset(&final_block[buffer_index], 0, 64 - buffer_index);
        transform(final_block);
        std::memset(final_block, 0, 56);
    } else {
        std::memset(&final_block[buffer_index], 0, 56 - buffer_index);
    }

    // Append 64-bit bit_count in big-endian order
    for (int i = 0; i < 8; ++i) {
        final_block[56 + i] = static_cast<uint8_t>((bit_count_ >> ((7 - i) * 8)) & 0xFF);
    }
    transform(final_block);

    std::array<uint8_t, DIGEST_SIZE> digest;
    for (int i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = static_cast<uint8_t>((state_[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((state_[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((state_[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(state_[i] & 0xFF);
    }

    // Reset state for safety
    init();

    return digest;
}

std::string SHA256::final_hex() {
    std::array<uint8_t, DIGEST_SIZE> digest = final_raw();
    static const char hex_digits[] = "0123456789abcdef";
    std::string hex_str;
    hex_str.reserve(64);
    for (uint8_t byte : digest) {
        hex_str.push_back(hex_digits[(byte >> 4) & 0x0F]);
        hex_str.push_back(hex_digits[byte & 0x0F]);
    }
    return hex_str;
}

std::string SHA256::hash_string(const std::string& input) {
    SHA256 ctx;
    ctx.update(input);
    return ctx.final_hex();
}

std::string SHA256::hash_bytes(const void* data, size_t len) {
    SHA256 ctx;
    ctx.update(data, len);
    return ctx.final_hex();
}

} // namespace crypto
