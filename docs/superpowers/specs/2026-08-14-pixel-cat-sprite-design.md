# 像素猫精灵（方向 C）—— 设计文档

日期：2026-08-14
状态：已批准

## 1. 目标

把当前「程序化矢量橘猫」替换为「像素风全身猫」，保持现有状态机与行为（左右踱步、追蝴蝶），用**单精灵 + 运行时变换**实现各状态动画，让猫更可爱（用户反馈当前猫「丑」）。

## 2. 范围

- 精灵：96×96 RGB565（18KB），由 🐈 emoji 光栅化 → 降采样 → 重映射到「橘+白」4 色板生成。
- 渲染：`pet_animation.cpp` 的 `draw()` 从「程序化画形状」改为「精灵 blit + 变换」。
- 动画：运行时变换（镜像 / bob / squash / 灰化 / 变暗），**不做整套逐帧 PNG 资产**。
- 叠加符号（`?` / `Zzz` / ⭐ / 💧 / 蝴蝶）保留。

## 3. 明确不做

- 整套逐帧 PNG 资产管线（YAGNI；若单精灵变换效果不足，后续再升级）。
- 不改状态机（`pet_state.h`）、BLE/WiFi/统计、bridge。

## 4. 技术方案

### 4.1 精灵资产生成

新增 `scripts/gen_cat_sprite.py`（Python + Playwright + Pillow）：

1. Playwright 无头 Chromium 渲染 🐈 emoji（1024px、透明底截图）。
2. Pillow LANCZOS 降采样到 96×96。
3. 最近色映射到「橘+白」4 色板（无粉色）：
   - 橘 `#f6c880`、深橘/条纹 `#d69c58`、白 `#fce2b4`、描边/眼 `#402a1c`
4. 输出 `firmware/src/ui/cat_sprite.h`：`kCatSprite[9216]`（RGB565）+ `kCatMask[1152]`（1-bit 透明遮罩）。

### 4.2 渲染层

- `pet_animation.cpp` 新增 `blitSprite(...)`：把 32×32 精灵按变换画到 `GFXcanvas16`。
- 朝向：精灵为侧身全身猫，`facingLeft_` 镜像实现左右。
- 变换复用现有 `update()` 产出的参数：
  | 状态 | 变换 |
  |---|---|
  | IDLE | 上下 bob（呼吸）+ 眨眼（暂时用轻微 squash 或省略，后续加密） |
  | WORKING | 追蝴蝶：左右滑动 + gallop bob + squash |
  | WAITING | 歪头（轻微旋转近似为 squash 偏移）+ `?` |
  | COMPLETED | 上下蹦跳 + ⭐ |
  | ERROR | 左右抖动 + 💧 |
  | SLEEP | 压扁（squash）+ 变暗 |
  | OFFLINE | 灰化（去饱和） |
- 符号（`?`/`Zzz`/⭐/💧）与蝴蝶继续用小精灵/程序化叠加在猫上方。

### 4.3 屏幕缩放

96×96 精灵**按原生 1× 显示为 96px**（与 12px 文字清晰度协调，像素不放大），最终比例真机目视校准。

### 4.4 涉及文件

| 文件 | 改动 |
|---|---|
| `scripts/gen_cat_sprite.py` | 新增：资产生成脚本 |
| `firmware/src/ui/cat_sprite.h` | 新增：生成的 RGB565 精灵 + mask |
| `firmware/src/pet/pet_animation.cpp` | `draw()` 重写为 blit；`update()` 基本不动 |
| `firmware/src/pet/pet_animation.h` | 删掉不再用的 `drawSideHead/drawSideLeg/drawSideTail/drawButterfly` 等形状函数，加 blit 状态 |

## 5. 数据流（不变）

```
Codex/DSH → bridge → BLE → ESP32 → PetState → update(变换参数) → blitSprite → LCD
```

## 6. 错误处理

- RGB565 无法表达 alpha：用独立 1-bit `kCatMask` 标记透明像素；blit 时 mask 为 0 的像素不写。
- 变换参数越界：clamp 到合理范围。
- 精灵缺失：编译期常量，不运行时缺失；若未来改运行时加载，回退程序化绘制。

## 7. 验收标准

- [ ] 精灵在真机上正常显示（橘猫配色、无错位/花屏）。
- [ ] 各状态动画正常（IDLE/WORKING/WAITING/COMPLETED/ERROR/SLEEP/OFFLINE）。
- [ ] 左右朝向镜像正确（追蝴蝶方向一致）。
- [ ] FPS ≥ 20，无内存泄漏。
- [ ] 猫更可爱（用户目视确认）。

## 8. 风险与待验证

1. **4 色重映射丢失细节**：emoji 眼睛高光等可能被吞，真机目视确认，必要时加回少量色。
2. **精灵朝向与镜像**：侧身精灵 + `facingLeft_` 镜像后条纹/脸方向需核对。
3. **尺寸**：96×96 原生 96px 已定；若偏大/偏小可改 64×64 或 128×128。
4. **眨眼等表情**：单精灵难以做眨眼；后续可加「眼睛遮挡层」或第二帧（不在本轮）。
