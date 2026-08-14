#include <cassert>
#include <cstdio>
#include "../firmware/src/ui/text_convert.h"

int main() {
    // ASCII
    auto a = utf8::decode("A");
    assert(a.cp == 0x41 && a.len == 1);
    // 2-byte: é (U+00E9)
    auto e = utf8::decode("\xC3\xA9");
    assert(e.cp == 0xE9 && e.len == 2);
    // 3-byte: 你 (U+4F60)
    auto n = utf8::decode("\xE4\xBD\xA0");
    assert(n.cp == 0x4F60 && n.len == 3);
    // 4-byte: 😀 (U+1F600)
    auto g = utf8::decode("\xF0\x9F\x98\x80");
    assert(g.cp == 0x1F600 && g.len == 4);
    // invalid lead byte -> U+FFFD, len 1
    auto bad = utf8::decode("\xFF");
    assert(bad.cp == 0xFFFD && bad.len == 1);

    // width-based truncation: "A你好" -> ASCII=6, CJK=12 each
    const char* s = "A\xE4\xBD\xA0\xE5\xA5\xBD";
    size_t off = utf8::truncate_by_width(s, 18, 6, 12);
    assert(off == 1 + 3); // "A" + "你"
    assert(utf8::truncate_by_width(s, 10, 6, 12) == 1);
    assert(utf8::truncate_by_width(s, 1000, 6, 12) == 1 + 3 + 3);

    // 你 = U+4F60 = GB2312 0xC4E3 ; 好 = U+597D = GB2312 0xBAC3
    {
        char out[32];
        size_t n = utf8_to_gb2312("\xE4\xBD\xA0\xE5\xA5\xBD", out, sizeof(out), 1000, 6, 12);
        assert(n == 4);
        assert((unsigned char)out[0] == 0xC4 && (unsigned char)out[1] == 0xE3);
        assert((unsigned char)out[2] == 0xBA && (unsigned char)out[3] == 0xC3);
    }
    // ASCII 透传
    {
        char out[32];
        size_t n = utf8_to_gb2312("task A", out, sizeof(out), 1000, 6, 12);
        assert(n == 6 && out[0] == 't' && out[5] == 'A');
    }
    // 未映射字符 -> '?'
    {
        char out[32];
        size_t n = utf8_to_gb2312("\xF0\x9F\x98\x80", out, sizeof(out), 1000, 6, 12); // 😀 不在 GB2312
        assert(n == 1 && out[0] == '?');
    }

    std::puts("ALL text_convert TESTS PASS");
    return 0;
}
