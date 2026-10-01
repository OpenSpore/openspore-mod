// ByteStream.h - little-endian byte writer/reader used by the OpenSpore wire protocol.
// Portable: no Windows or Spore dependencies, so it is unit-tested on Linux.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace osmp {

class ByteWriter {
public:
    explicit ByteWriter(size_t reserve = 256) { buf_.reserve(reserve); }

    void u8(uint8_t v) { buf_.push_back(v); }
    void u16(uint16_t v) { u8((uint8_t)(v & 0xFF)); u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v) { u16((uint16_t)(v & 0xFFFF)); u16((uint16_t)(v >> 16)); }
    void i32(int32_t v) { u32((uint32_t)v); }
    void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
    void bytes(const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    // length-prefixed (1 byte) string, truncated to 255 bytes
    void str(const std::string& s) {
        size_t n = s.size() > 255 ? 255 : s.size();
        u8((uint8_t)n);
        bytes(s.data(), n);
    }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t>& data() { return buf_; }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* p, size_t n) : p_(p), n_(n), pos_(0), ok_(true) {}
    explicit ByteReader(const std::vector<uint8_t>& v) : ByteReader(v.data(), v.size()) {}
    // The reader only borrows the buffer, so a temporary vector would dangle.
    explicit ByteReader(std::vector<uint8_t>&&) = delete;

    bool ok() const { return ok_; }
    size_t remaining() const { return n_ - pos_; }
    size_t position() const { return pos_; }

    uint8_t u8() { if (pos_ + 1 > n_) { ok_ = false; return 0; } return p_[pos_++]; }
    uint16_t u16() { uint16_t lo = u8(); uint16_t hi = u8(); return (uint16_t)(lo | (hi << 8)); }
    uint32_t u32() { uint32_t lo = u16(); uint32_t hi = u16(); return lo | (hi << 16); }
    int32_t i32() { return (int32_t)u32(); }
    float f32() { uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    bool bytes(void* dst, size_t n) {
        if (pos_ + n > n_) { ok_ = false; return false; }
        std::memcpy(dst, p_ + pos_, n);
        pos_ += n;
        return true;
    }
    std::string str() {
        uint8_t n = u8();
        if (!ok_) return std::string();
        if (pos_ + n > n_) { ok_ = false; return std::string(); }
        std::string s(reinterpret_cast<const char*>(p_ + pos_), n);
        pos_ += n;
        return s;
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_;
    bool ok_;
};

} // namespace osmp
