// Minimal SHA-256 (FIPS 180-4), used to recognise the retail default.xbe.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

class Sha256 {
public:
    Sha256();
    void        Update(const void *data, size_t len);
    std::string HexDigest();   // finalises; call once

private:
    void     Block(const uint8_t *p);
    uint32_t h_[8];
    uint8_t  buf_[64];
    size_t   used_ = 0;
    uint64_t total_ = 0;
};
