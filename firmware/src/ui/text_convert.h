#pragma once
#include <cstdint>
#include <cstddef>

namespace utf8 {

struct Codepoint { uint32_t cp = 0; uint8_t len = 0; };

// 解码 s 开头的 UTF-8 码点；无效/截断首字节返回 U+FFFD、len=1。
inline Codepoint decode(const char* s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    Codepoint out;
    const uint8_t b0 = p[0];
    if (b0 < 0x80)                       { out.cp = b0; out.len = 1; }
    else if ((b0 & 0xE0) == 0xC0)        { out.cp = ((b0 & 0x1F) << 6) | (p[1] & 0x3F); out.len = 2; }
    else if ((b0 & 0xF0) == 0xE0)        { out.cp = ((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); out.len = 3; }
    else if ((b0 & 0xF8) == 0xF0)        { out.cp = ((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); out.len = 4; }
    else                                 { out.cp = 0xFFFD; out.len = 1; }
    return out;
}

// 返回截断字节偏移：从 s 开头累计宽度（asciiUnits / wideUnits），
// 使累计 <= maxUnits，且总是落在 UTF-8 码点边界上。s 必须 null 结尾。
inline size_t truncate_by_width(const char* s, int maxUnits, int asciiUnits, int wideUnits) {
    size_t i = 0;
    size_t last = 0;
    int used = 0;
    while (s[i] != '\0') {
        Codepoint c = decode(s + i);
        int w = (c.cp < 0x80) ? asciiUnits : wideUnits;
        if (used + w > maxUnits) break;
        used += w;
        i += c.len;
        last = i;
    }
    return last;
}

} // namespace utf8
