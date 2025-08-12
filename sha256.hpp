#ifndef SHA256_H
#define SHA256_H

#include <cstring>
#include <string>
#include <vector>
#include <stdint.h>

static const uint32_t k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

class SHA256 {
public:
    SHA256() {
        datalen = 0;
        bitlen = 0;
        finalized = false;
        state[0] = 0x6a09e667;
        state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372;
        state[3] = 0xa54ff53a;
        state[4] = 0x510e527f;
        state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab;
        state[7] = 0x5be0cd19;
    }
    void update(const uint8_t* data_, size_t len) {
        if (finalized) return;
        for (size_t i = 0; i < len; ++i) {
            data[datalen] = data_[i];
            datalen++;
            if (datalen == 64) {
                transform(data);
                bitlen += 512;
                datalen = 0;
            }
        }
    }
    void update(const std::string& data_) {
        update(reinterpret_cast<const uint8_t*>(data_.data()), data_.size());
    }
    std::string digest() {
        finalize();
        char buf[65];
        for (int i = 0; i < 8; ++i) {
            sprintf_s(buf + i * 8, 9, "%08x", state[i]);
        }
        buf[64] = 0;
        return std::string(buf);
    }

private:
    void transform(const uint8_t* chunk) {
        uint32_t m[64];
        for (int i = 0; i < 16; ++i) {
            m[i] = (chunk[i * 4] << 24) | (chunk[i * 4 + 1] << 16) | (chunk[i * 4 + 2] << 8) | (chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = h + ep1(e) + ch(e, f, g) + k[i] + m[i];
            uint32_t t2 = ep0(a) + maj(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
    void finalize() {
        if (finalized) return;
        uint32_t i = datalen;

        // Pad whatever data is left in the buffer.
        if (datalen < 56) {
            data[i++] = 0x80;
            while (i < 56)
                data[i++] = 0x00;
        }
        else {
            data[i++] = 0x80;
            while (i < 64)
                data[i++] = 0x00;
            transform(data);
            memset(data, 0, 56);
        }

        bitlen += datalen * 8;

        data[63] = bitlen;
        data[62] = bitlen >> 8;
        data[61] = bitlen >> 16;
        data[60] = bitlen >> 24;
        data[59] = bitlen >> 32;
        data[58] = bitlen >> 40;
        data[57] = bitlen >> 48;
        data[56] = bitlen >> 56;

        transform(data);
        finalized = true;
    }

    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
    bool finalized;

    static uint32_t rotr(uint32_t x, uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }
    static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    static uint32_t ep0(uint32_t x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }
    static uint32_t ep1(uint32_t x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }
    static uint32_t sig0(uint32_t x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }
    static uint32_t sig1(uint32_t x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }
};

std::string hex_to_bin(const std::string& hex) {
    std::string out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int byte;
        sscanf_s(hex.substr(i, 2).c_str(), "%02x", &byte);
        out.push_back(static_cast<char>(byte));
    }
    return out;
}

std::string bin_to_hex(const std::string& bin) {
    std::ostringstream oss;
    for (unsigned char c : bin) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return oss.str();
}

std::string hmac_sha256(const std::string& key, const std::string& message) {
    const size_t blockSize = 64;
    std::string key_block = key;

    if (key_block.size() > blockSize) {
        SHA256 sha;
        sha.update(key_block);
        key_block = hex_to_bin(sha.digest());  // returns hex, convert to binary
    }

    key_block.resize(blockSize, '\0'); // zero pad if needed

    std::string o_key_pad(blockSize, 0x5c);
    std::string i_key_pad(blockSize, 0x36);

    for (size_t i = 0; i < blockSize; i++) {
        o_key_pad[i] ^= key_block[i];
        i_key_pad[i] ^= key_block[i];
    }

    SHA256 sha_inner;
    sha_inner.update(i_key_pad);
    sha_inner.update(message);
    std::string inner_hash = hex_to_bin(sha_inner.digest());

    SHA256 sha_outer;
    sha_outer.update(o_key_pad);
    sha_outer.update(inner_hash);
    std::string hmac_bin = hex_to_bin(sha_outer.digest());

    return bin_to_hex(hmac_bin);  // final result: lowercase hex string
}
#endif