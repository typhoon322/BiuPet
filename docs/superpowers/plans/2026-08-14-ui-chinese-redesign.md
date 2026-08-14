# CodexPet UI 精简 + 中文任务文本 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 ESP32 屏幕的任务行正确显示中文（12px），并把整屏精简成方案 B 的四段式布局。

**Architecture:** 在 Adafruit_GFX 渲染栈上引入 `U8g2_for_Adafruit_GFX` + `u8g2_font_wqy12_t_gb2312` 字库；纯逻辑（UTF-8 解码、宽度截断、Unicode→GB2312 映射）拆成无 Arduino 依赖的 `text_convert` 模块以便宿主机单测；渲染层 `chinese_text` 只做字模绘制。bridge 端完全不动。

**Tech Stack:** PlatformIO + Arduino-ESP32、Adafruit GFX / ST7789、U8g2_for_Adafruit_GFX、Python 3（表生成器）、g++（宿主机单测）。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `firmware/src/ui/text_convert.h` | 纯逻辑：UTF-8 解码、宽度截断、UTF-8→GB2312 转换（无 Arduino 依赖） |
| `firmware/src/ui/gb2312_table.h` | 生成的 Unicode→GB2312 映射表（只读数据 + 二分查找） |
| `firmware/src/ui/chinese_text.h/.cpp` | 渲染层：`drawChineseText()`，用 U8g2_for_Adafruit_GFX 画到 Adafruit_GFX |
| `scripts/gen_gb2312_table.py` | 生成 `gb2312_table.h`（用 Python 内置 gb2312 编解码） |
| `tests/text_convert_test.cpp` | 宿主机单测：解码/截断/转换（g++ 编译运行） |
| `firmware/src/main.cpp` | 接线：任务行改用 `drawChineseText`；重写 `drawStatusBar`（B 布局） |
| `firmware/platformio.ini` | 增加 `U8g2_for_Adafruit_GFX` 依赖 |

分工原则：`text_convert` + `gb2312_table` 完全无 Arduino/Adafruit 依赖，可在 Mac 上用 g++ 跑单测；`chinese_text` 与 `main.cpp` 依赖硬件，用「编译 + 烧录 + 串口日志 + 目视」验证。

---

## Task 1: 增加 U8g2_for_Adafruit_GFX 依赖

**Files:**
- Modify: `firmware/platformio.ini:20-21`

- [ ] **Step 1: 在 lib_deps 加库**

把 `firmware/platformio.ini` 的 `lib_deps` 段从：

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.3.0
```

改为：

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.3.0
    olikraus/U8g2_for_Adafruit_GFX@^1.8.0
```

- [ ] **Step 2: 编译验证依赖可解析**

Run: `~/.platformio/penv/bin/pio run`
Expected: 构建成功（首次会拉取新库）。若报找不到库，改用 `olikraus/U8g2_for_Adafruit_GFX`（不带版本号）。

- [ ] **Step 3: 提交**

```bash
git add firmware/platformio.ini
git commit -m "build: add U8g2_for_Adafruit_GFX dependency"
```

---

## Task 2: 纯逻辑模块 text_convert（UTF-8 解码 + 宽度截断）

**Files:**
- Create: `firmware/src/ui/text_convert.h`
- Test: `tests/text_convert_test.cpp`

- [ ] **Step 1: 写失败的单测**

创建 `tests/text_convert_test.cpp`：

```cpp
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
    // maxUnits=18 -> "A" (6) + "你"(12) = 18 ok; "好" would exceed -> cut after "你"
    const char* s = "A\xE4\xBD\xA0\xE5\xA5\xBD";
    size_t off = utf8::truncate_by_width(s, 18, 6, 12);
    assert(off == 1 + 3); // "A" + "你"
    // never split a multi-byte char: maxUnits=10 (only 6 fits "A", "你" needs 12 -> cut before 你)
    assert(utf8::truncate_by_width(s, 10, 6, 12) == 1);
    // maxUnits large enough -> whole string
    assert(utf8::truncate_by_width(s, 1000, 6, 12) == 1 + 3 + 3);

    std::puts("ALL text_convert TESTS PASS");
    return 0;
}
```

- [ ] **Step 2: 运行单测确认失败**

Run: `g++ -std=c++17 -Wall tests/text_convert_test.cpp -o /tmp/tc_test && /tmp/tc_test`
Expected: 编译失败（`text_convert.h` 不存在）。

- [ ] **Step 3: 实现 text_convert.h**

创建 `firmware/src/ui/text_convert.h`：

```cpp
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
```

- [ ] **Step 4: 运行单测确认通过**

Run: `g++ -std=c++17 -Wall tests/text_convert_test.cpp -o /tmp/tc_test && /tmp/tc_test`
Expected: `ALL text_convert TESTS PASS`

- [ ] **Step 5: 提交**

```bash
git add firmware/src/ui/text_convert.h tests/text_convert_test.cpp
git commit -m "feat: UTF-8 decode + width truncation (pure, host-tested)"
```

---

## Task 3: GB2312 映射表生成器 + 表

**Files:**
- Create: `scripts/gen_gb2312_table.py`
- Create: `firmware/src/ui/gb2312_table.h`（由脚本生成）
- Test: `tests/text_convert_test.cpp`（追加转换断言）

- [ ] **Step 1: 写生成器**

创建 `scripts/gen_gb2312_table.py`：

```python
#!/usr/bin/env python3
"""Generate unicode -> GB2312 lookup table header using Python's built-in codec."""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "firmware" / "src" / "ui" / "gb2312_table.h"

pairs = []
for hi in range(0xA1, 0x100):
    for lo in range(0xA1, 0x100):
        if lo == 0x7F:
            continue
        try:
            ch = bytes([hi, lo]).decode("gb2312")
        except (UnicodeDecodeError, ValueError):
            continue
        pairs.append((ord(ch), (hi << 8) | lo))

pairs.sort(key=lambda p: p[0])

lines = [
    "// Generated by scripts/gen_gb2312_table.py — do not edit by hand.",
    "#pragma once",
    "#include <cstdint>",
    "",
    f"// {len(pairs)} entries, sorted by unicode codepoint.",
    f"constexpr uint16_t kGbCp[{len(pairs)}] = {{",
]
for i in range(0, len(pairs), 8):
    lines.append("    " + ", ".join(f"0x{p[0]:04X}" for p in pairs[i:i+8]) + ",")
lines += ["};", "", f"constexpr uint16_t kGbGb[{len(pairs)}] = {{"]
for i in range(0, len(pairs), 8):
    lines.append("    " + ", ".join(f"0x{p[1]:04X}" for p in pairs[i:i+8]) + ",")
lines += ["};", "", f"constexpr int kGbCount = {len(pairs)};", ""]

lines += [
    "// 二分查找；未命中返回 0。ASCII(0x20-0x7E) 直接透传，不走此表。",
    "inline uint16_t unicode_to_gb2312(uint16_t cp) {",
    "    int lo = 0, hi = kGbCount - 1;",
    "    while (lo <= hi) {",
    "        int mid = (lo + hi) / 2;",
    "        if (kGbCp[mid] == cp) return kGbGb[mid];",
    "        if (kGbCp[mid] < cp) lo = mid + 1; else hi = mid - 1;",
    "    }",
    "    return 0;",
    "}",
    "",
]

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"wrote {OUT} ({len(pairs)} entries)")
```

- [ ] **Step 2: 生成表头**

Run: `python3 scripts/gen_gb2312_table.py`
Expected: 输出 `wrote .../gb2312_table.h (6763 entries)`（或接近 6763）。

- [ ] **Step 3: 在 text_convert.h 增加转换函数**

在 `firmware/src/ui/text_convert.h` 末尾（`} // namespace utf8` 之后）追加：

```cpp
#include "gb2312_table.h"

// UTF-8 -> GB2312 字节串。ASCII 透传（1 字节）；CJK 映射为 2 字节；
// 未映射码点写 '?'（1 字节）。按宽度截断（asciiUnits/wideUnits），
// 返回写入 dst 的字节数（不含结尾 '\0'）。
inline size_t utf8_to_gb2312(const char* src, char* dst, size_t dstCap,
                             int maxUnits, int asciiUnits, int wideUnits) {
    size_t i = 0, o = 0;
    int used = 0;
    while (src[i] != '\0' && o + 4 < dstCap) {
        utf8::Codepoint c = utf8::decode(src + i);
        int w = (c.cp < 0x80) ? asciiUnits : wideUnits;
        if (used + w > maxUnits) break;
        if (c.cp < 0x80) {
            dst[o++] = static_cast<char>(c.cp);
        } else {
            uint16_t gb = unicode_to_gb2312(static_cast<uint16_t>(c.cp));
            if (gb != 0) {
                dst[o++] = static_cast<char>((gb >> 8) & 0xFF);
                dst[o++] = static_cast<char>(gb & 0xFF);
            } else {
                dst[o++] = '?';
            }
        }
        used += w;
        i += c.len;
    }
    dst[o] = '\0';
    return o;
}
```

- [ ] **Step 4: 追加单测断言**

在 `tests/text_convert_test.cpp` 的 `return 0;` 之前追加：

```cpp
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
```

- [ ] **Step 5: 运行单测确认通过**

Run: `g++ -std=c++17 -Wall tests/text_convert_test.cpp -o /tmp/tc_test && /tmp/tc_test`
Expected: `ALL text_convert TESTS PASS`

- [ ] **Step 6: 提交**

```bash
git add scripts/gen_gb2312_table.py firmware/src/ui/gb2312_table.h firmware/src/ui/text_convert.h tests/text_convert_test.cpp
git commit -m "feat: unicode->GB2312 table generator + conversion"
```

---

## Task 4: chinese_text 渲染层 + 接入任务行

**Files:**
- Create: `firmware/src/ui/chinese_text.h`
- Create: `firmware/src/ui/chinese_text.cpp`
- Modify: `firmware/src/main.cpp`（删除 `sanitizeTaskLine`，任务行改用 `drawChineseText`）

- [ ] **Step 1: 实现 chinese_text**

创建 `firmware/src/ui/chinese_text.h`：

```cpp
#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>

// 用 wqy12 中文字库在 (x, y) 处绘制 UTF-8 文本，按像素宽度截断。
void drawChineseText(Adafruit_GFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx);
```

创建 `firmware/src/ui/chinese_text.cpp`：

```cpp
#include "chinese_text.h"
#include <U8g2_for_Adafruit_GFX.h>
#include "text_convert.h"

namespace {
U8g2_for_Adafruit_GFX u8g2f;
bool initialized = false;
}

void drawChineseText(Adafruit_GFX& g, int16_t x, int16_t y, const char* utf8, uint16_t color, int maxPx) {
    if (!initialized) {
        u8g2f.begin(g);
        u8g2f.setFont(u8g2_font_wqy12_t_gb2312);
        initialized = true;
    }
    // ASCII ~6px，CJK ~12px（真机校准 asciiUnits/wideUnits 即可微调）
    char buf[96];
    utf8_to_gb2312(utf8, buf, sizeof(buf), maxPx, 6, 12);
    u8g2f.setForegroundColor(color);
    u8g2f.setBackgroundColor(0x0000);
    u8g2f.setCursor(x, y + 12);   // y 是基线之上的顶线，wqy12 行高约 12
    u8g2f.print(buf);
}
```

- [ ] **Step 2: main.cpp 接入任务行**

在 `firmware/src/main.cpp`：
1. 顶部 include 区加入 `#include "ui/chinese_text.h"`（放在 `#include "storage/pet_stats.h"` 之后）。
2. 删除整个 `sanitizeTaskLine()` 函数（原 `main.cpp` 里 `static void sanitizeTaskLine(...)` 到其右花括号，约 15 行）。
3. 在 `drawStatusBar()` 的任务行部分，把：

```cpp
    tft.fillRect(0, 198, 320, 14, ST77XX_BLACK);
    char taskBuf[48];
    sanitizeTaskLine(bottomText, taskBuf, sizeof(taskBuf), 296);
    tft.setCursor(16, 200);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print(taskBuf);
```

替换为：

```cpp
    tft.fillRect(0, 198, 320, 14, ST77XX_BLACK);
    drawChineseText(tft, 16, 198, bottomText, ST77XX_CYAN, 296);
```

> 说明：`bottomText` 目前存的是 `"task: xxx"`。本任务先保持 `bottomText` 内容不变（含 `task:` 前缀），在 Task 5 的 B 布局里一并去掉前缀。

- [ ] **Step 3: 编译**

Run: `~/.platformio/penv/bin/pio run`
Expected: 编译成功。若报 `u8g2_font_wqy12_t_gb2312` 未定义或 `U8g2_for_Adafruit_GFX` 找不到头，检查 lib 是否拉取成功；若字体不在默认 `u8g2_fonts.c` 里，改 `#include <U8g2_for_Adafruit_GFX.h>` 后补一行 `#include <u8g2_fonts.h>`（Task 1 已装库，此处只做最终确认）。

- [ ] **Step 4: 烧录 + 串口验证**

Run: `~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbserial-A5069RR4`
Expected: 烧录成功，串口出现 `=== CodexPet phase2: BLE ===` 和 `[PET] ready`，无 crash。目视：任务行中文不再显示 `?`（需 bridge 推送中文任务或先用 `send_state.py --task "测试中文"` 验证，见 Task 6）。

- [ ] **Step 5: 提交**

```bash
git add firmware/src/ui/chinese_text.h firmware/src/ui/chinese_text.cpp firmware/src/main.cpp
git commit -m "feat: render Chinese task text via U8g2 wqy12"
```

---

## Task 5: B 精简布局（重写 drawStatusBar）

**Files:**
- Modify: `firmware/src/main.cpp`（`drawStatusBar` 与任务行 `bottomText` 前缀）

- [ ] **Step 1: 去掉任务前缀**

把 `main.cpp` 里两处 `snprintf(bottomText, sizeof(bottomText), "task: %s", ...)`（wifi 分支与 ble.taskChanged 分支）中的 `"task: %s"` 改为 `"%s"`。

- [ ] **Step 2: 重写 drawStatusBar**

把 `main.cpp` 里整个 `drawStatusBar(PetState state)` 函数替换为四段式：

```cpp
void drawStatusBar(PetState state) {
    // 顶栏：状态名 + 两个彩色圆点
    tft.fillRect(0, 0, 320, 20, ST77XX_BLACK);
    tft.setCursor(8, 4);
    tft.setTextColor(ST77XX_CYAN);
    tft.setTextSize(1);
    tft.print(petStateName(state));

    const bool bleOk = ble.isOnline();
    const bool wifiOk = wifi.isConnected();
    // BLE 圆点
    tft.fillCircle(268, 10, 4, bleOk ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(276, 4);
    tft.setTextColor(bleOk ? ST77XX_GREEN : ST77XX_RED);
    tft.print("BLE");
    // WiFi 圆点
    tft.fillCircle(306, 10, 4, wifiOk ? ST77XX_GREEN : ST77XX_RED);
    tft.setCursor(312, 4);
    tft.setTextColor(wifiOk ? ST77XX_GREEN : ST77XX_RED);
    tft.print("WiFi");

    // 任务行（中文）
    tft.fillRect(0, 198, 320, 16, ST77XX_BLACK);
    if (bottomText[0] != '\0') {
        drawChineseText(tft, 8, 198, bottomText, ST77XX_CYAN, 304);
    }

    // 底栏：Lv / 用量 / 余额 单行
    tft.fillRect(0, 216, 320, 24, ST77XX_BLACK);
    const auto& st = stats.stats();
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u", st.level);
    tft.setCursor(8, 220);
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.print(lv);

    tft.setCursor(52, 220);
    tft.setTextColor(ST77XX_MAGENTA);
    tft.print(usageText);

    char bal6[7];
    strncpy(bal6, ble.balanceText(), 6);
    bal6[6] = '\0';
    char balFixed[7];
    snprintf(balFixed, sizeof(balFixed), "%6s", bal6);
    tft.setCursor(268, 220);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(balFixed);
    drawWhale(tft, 238, 218);
}
```

- [ ] **Step 3: 调整猫区 blit 目标 y**

把 `main.cpp` 的 `pet.draw(tft, 0, 64);` 改为 `pet.draw(tft, 0, 30);`（猫区上移，留白增加；猫画布仍为 320×128，y 30..158）。

> 若发现猫画布 128 高 + y30 会压到任务行 y198，属于正常（30+128=158 < 198）。真机如发现符号（`?`/`Zzz`）被顶栏裁，则把 30 微调到 36。

- [ ] **Step 4: 编译 + 烧录 + 目视校准**

Run: `~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbserial-A5069RR4`
Expected: 顶栏只有状态名 + 两个圆点；猫区留白变大；任务行中文；底栏单行 Lv/用量/余额。真机校准圆点位置与 `pet.draw` 的 y（允许 ±4px）。

- [ ] **Step 5: 提交**

```bash
git add firmware/src/main.cpp
git commit -m "feat: B layout — slim top strip, dots, single footer line"
```

---

## Task 6: 端到端验收

**Files:** 无（仅验证）

- [ ] **Step 1: 用 send_state 推中文任务**

Run: `python3 bridge/send_state.py WORKING --task "测试中文显示" --name CodexPet`
Expected: 串口 `[BLE] task text: 测试中文显示`；设备任务行显示「测试中文显示」（无 `?`、无乱码）。

- [ ] **Step 2: 各状态目视检查**

依次 `send_state.py` 推 `IDLE / WORKING / WAITING / COMPLETED / ERROR / SLEEP / OFFLINE`，确认：
- 顶栏状态名正确变化；
- BLE/WiFi 圆点颜色正确（断开 bridge 或关 WiFi 后变红）；
- 猫动画随状态切换，符号不被顶栏/底栏裁切。

- [ ] **Step 3: 性能与稳定性**

在串口观察 `[PERF] fps=... heap=...`：FPS ≥ 20，heap 无明显持续下降（连续观察 5 分钟）。

- [ ] **Step 4: 提交（如有微调）**

```bash
git add -A && git commit -m "chore: final layout tuning + verify"
```

---

## 自检（对照 spec）

- **中文任务文本**：Task 2/3/4 覆盖（解码、GB2312、渲染）；`sanitizeTaskLine` 删除在 Task 4；截断不切中文字在 Task 2 单测覆盖。✅
- **B 布局**：Task 5 覆盖（顶栏圆点、猫区、中文任务行、底栏单行）。✅
- **猫尺寸不改**：Task 5 Step 3 只改 blit y，不改 `CANVAS_H` 与绘制。✅
- **bridge 不动**：无 bridge 改动任务。✅
- **错误处理**：未映射字符 `?`（Task 3 Step 4）、空任务不画（Task 5 Step 2 `bottomText[0] != '\0'`）。✅
- **待验证项**：字体 API 细节在 Task 4 Step 3/4 用编译+烧录兜底确认。✅
