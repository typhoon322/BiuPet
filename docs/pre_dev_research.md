# CodexPet 开发前调研报告

> 基于 [`DevGuide.md`](file:///Users/yanx/ESP32/CodexPet/DevGuide.md) 与外部公开项目，为第一阶段 MVP（Codex 状态 → Mac Bridge → BLE → ESP32-S3 → SPI LCD 动画）做的技术调研。  
> 撰写时间：2026-08-13

---

## 1. 项目目标与第一阶段范围

CodexPet 是一个**实体桌面宠物**，不是把 Codex 运行在 ESP32 上，而是：

```text
Mac (Codex CLI / Codex App)
        ↓
codex-pet-bridge (Python)
        ↓
    BLE / Wi-Fi
        ↓
ESP32-S3 + SPI LCD
        ↓
   小猫动画
```

第一阶段只跑通**状态联动**，不做养成系统。必须实现的状态：

```text
OFFLINE   IDLE   WORKING   WAITING   COMPLETED   ERROR   SLEEP
```

最终效果：Codex 开始干活 → 小猫醒来/打字；Codex 等待输入 → 小猫疑惑；Codex 完成 → 小猫庆祝；Codex 出错 → 小猫沮丧；长时间空闲 → 小猫睡觉。

---

## 2. 参考项目调研

DevGuide 明确提到：网上已有两类实现思路可作参考，但不要直接复制。调研结果如下。

### 2.1 Vibe Pet（多 Agent 桌面/硬件宠物）

- **仓库**：[Seeed-Solution/vibe-pet](https://github.com/Seeed-Solution/vibe-pet)
- **定位**：Electron 桌面应用 + 硬件同步，支持 Codex / Cursor / Windsurf / Claude 等 Agent。
- **Mac 端**：本地 HTTP bridge（`127.0.0.1:17384`），Agent 通过 hook 或 JSONL 上报状态。
- **Codex 集成方式**：
  - Hook：`~/.codex/hooks.json` + `[features] hooks = true`，事件包括 `SessionStart`、`UserPromptSubmit`、`PreToolUse`、`PermissionRequest`、`PostToolUse`、`Stop`。
  - JSONL 监听：`~/.codex/sessions/**/rollout-*.jsonl`，作为 hook 未覆盖时的 fallback。
- **硬件协议**：BLE GATT + JSON payload（非二进制），也支持 Wi-Fi 轮询。
- **可取之处**：Codex hook 配置、JSONL 事件映射、状态归一化思路、BLE 设备名前缀扫描策略。
- **本项目差异**：DevGuide 明确要求第一版用**二进制 BLE 协议**、状态模型更精简（无 `thinking`/`juggling`）、不依赖 Electron。

### 2.2 Codex RLCD Pet Display（ESP32-S3 + BLE 黑白屏）

- **仓库**：[ADC-xm/codex-rlcd-pet-display](https://github.com/ADC-xm/codex-rlcd-pet-display)
- **定位**：Waveshare ESP32-S3-RLCD-4.2 全反射屏，显示 Codex 额度 + 线条宠物。
- **数据获取**：`codex app-server --listen stdio://` → `account/rateLimits/read`。
- **同步**：BLE 串口式分包传输 JSON。
- **可取之处**：BLE 桥接脚本守护、开机自启动、低分辨率 UI 设计思路。
- **本项目差异**：不显示额度，而是显示任务状态；屏幕为彩色 SPI LCD；协议为二进制。

### 2.3 Codex Pet MCU Desk Companion（ESP32-P4/C6 + USB Serial）

- **仓库**：[Coke1120/codex-pet-dev-board](https://github.com/Coke1120/codex-pet-dev-board)
- **定位**：GUITION JC4880P443C-I-W（480×800 触摸屏），ESP32-P4 驱动显示，C6 负责 Wi-Fi/BLE。
- **数据获取**：Codex lifecycle hooks + CodexBar quota。
- **Mac 端**：`codex_pet_daemon.py` 通过 USB Serial 与设备通信，维护状态机。
- **可取之处**：生命周期 hook 映射器、状态机解耦、硬件抽象层（BSP）思路。
- **本项目差异**：使用 BLE 而非 USB Serial；MCU 为 ESP32-S3；屏幕分辨率更低。

---

## 3. Codex 状态获取方案（关键调研结论）

DevGuide 强调：**必须先确认当前版本 Codex 实际可用的接口/事件来源，再决定 `codex_monitor.py` 怎么写**。经过本地验证与参考项目，结论如下。

### 3.1 当前 Codex CLI 版本

本地安装：

```text
codex-cli 0.147.0-alpha.6.6
```

`hooks` feature 状态：

```text
hooks  stable  true
```

说明 **hooks 已稳定可用**，不需要破解内部协议。

### 3.2 方案 A：官方 Hook（主来源）

Codex 会在 `~/.codex/hooks.json` 中配置的命令，在特定事件触发时调用。参考 Vibe Pet 的实践经验，可配置的事件与状态映射如下：

| Codex Hook 事件 | 本项目状态 | 说明 |
|---|---|---|
| `SessionStart` | `IDLE` | 会话开始 |
| `UserPromptSubmit` | `WORKING` | 用户已提交，Codex 开始处理 |
| `PreToolUse` | `WORKING` | 正在调工具 |
| `PermissionRequest` | `WAITING` | 等待用户授权/批准 |
| `PostToolUse` | `WORKING` | 工具调用后继续处理 |
| `Stop` | `COMPLETED` → `IDLE` | 本轮结束，短暂庆祝后回到待机 |

Hook 脚本建议做得极简：读取 stdin 的 JSON payload，用 `curl` 把事件 POST 到 bridge 的本地端口（例如 `127.0.0.1:17384/api/hook`），然后立即退出。桥接到时再聚合/防抖。

启用方式：

```toml
# ~/.codex/config.toml
[features]
hooks = true
```

```json
// ~/.codex/hooks.json（示例，见附录 B）
{
  "hooks": {
    "SessionStart": [{"hooks": [{"type": "command", "command": "/Users/.../bridge/hook.sh SessionStart"}]}],
    ...
  }
}
```

**风险**：首次安装或更新 hook 脚本后，Codex 可能要求用户在交互界面执行 `/hooks` 并批准新命令。

### 3.3 方案 B：JSONL Session Log（Fallback）

Codex 会把会话事件写入：

```text
~/.codex/sessions/YYYY/MM/DD/rollout-YYYY-MM-DDTHH-MM-SS-<uuid>.jsonl
```

本地日志样本已验证包含以下事件：

| JSONL 类型 / 子类型 | 本项目状态 |
|---|---|
| `session_meta` | `IDLE` |
| `event_msg:task_started` | `WORKING` |
| `response_item:reasoning` | `WORKING` |
| `response_item:function_call` | `WORKING` |
| `response_item:custom_tool_call` | `WORKING` |
| `event_msg:task_complete` | `COMPLETED` → `IDLE` |
| `event_msg:turn_aborted` | `IDLE` |
| 权限相关字段 | `WAITING` |

实现思路：bridge 启动一个后台线程，按日期目录轮询最近 1–2 天的 rollout 文件，使用 `tail`/`seek` 方式增量读取，避免全量加载。

### 3.4 方案 C：进程/守护状态（辅助）

- 检测 `codex` 进程是否存在，仅用于判断“是否处于可工作状态”。
- 如果 Codex 未运行，bridge 可向 ESP32 发送 `OFFLINE` 或 `IDLE`。
- 不能替代 hook/log，因为无法区分 WORKING/WAITING/COMPLETED。

### 3.5 推荐策略

采用 **“Hook 主来源 + JSONL Fallback + 心跳兜底”** 三层模型：

```text
Codex hooks ──► Bridge StateHub ──► 聚合/防抖 ──► BLE
                    ▲
JSONL monitor ──────┘
                    ▲
heartbeat/idle ─────┘
```

- hook 覆盖即时状态转换；
- JSONL 在 hook 失效、延迟、或被用户拒绝时补状态；
- bridge 周期性发送心跳，若 Codex 长时间无活动则切回 `IDLE`，再超时就进入 `SLEEP`。

---

## 4. 硬件方案

### 4.1 主控

| 项目 | 要求 |
|---|---|
| MCU | ESP32-S3 |
| Flash | ≥ 8 MB |
| PSRAM | ≥ 2 MB（推荐 8 MB OPI） |
| 框架 | Arduino（PlatformIO） |

ESP32-S3 的 Wi-Fi/BLE 共存能力为后续扩展预留空间，第一版只用 BLE。

### 4.2 屏幕

| 参数 | 建议 |
|---|---|
| 尺寸 | 2.0"–3.5" |
| 分辨率 | 默认 320×240 横屏 |
| 接口 | SPI |
| 常见驱动 | ST7789、ILI9341、GC9A01 |

**必须做 Display HAL**：不要写死某个驱动。可用 LovyanGFX 的 `Bus_SPI` + `Panel_*` 抽象，通过 `display_config.h` + build flag 切换。

### 4.3 待确认信息

目前还不知道实际使用的开发板和屏幕型号/pinout。Phase 1 开始编码前需要确认：

1. 开发板型号（ESP32-S3-DevKitC-1 / LilyGo T-Display S3 / 其他）
2. LCD 控制器型号与分辨率
3. SPI 引脚（SCK/MOSI/CS/DC/RST/BL）
4. 是否有按键、蜂鸣器、震动马达等外设

---

## 5. 软件栈与关键库

### 5.1 ESP32 固件

| 层级 | 选择 | 说明 |
|---|---|---|
| 构建系统 | PlatformIO | 推荐，Arduino 框架 |
| 显示 | LovyanGFX | 高性能、多驱动、Sprite 支持 |
| BLE | NimBLE-Arduino | 比 Bluedroid 更省内存、更稳定 |
| JSON（debug/配置） | ArduinoJson | 仅在需要时引入 |
| 存储 | 优先 Flash + PSRAM | 动画帧预加载 |

推荐 `platformio.ini` 片段：

```ini
[env:esp32-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_build.arduino.memory_type = qio_opi
lib_deps =
    lovyan0330/LovyanGFX @ ^1.2.0
    h2zero/NimBLE-Arduino @ ^1.4.0
```

> 实际 board 要根据硬件替换，如 `lilygo-t-display-s3`。

### 5.2 Mac Bridge

| 依赖 | 用途 |
|---|---|
| Python 3.11+ | 桥接主程序 |
| `bleak` | BLE Central（扫描、连接、写 characteristic） |
| `PyYAML` | 读取 `config.yaml` |
| `watchdog` | 监听 Codex 会话目录变化 |
| `tomllib`（内置） | 读取 `~/.codex/config.toml` |
| `requests` / `urllib` | hook 上报接收（可选本地 HTTP） |

当前环境检查：

- `bleak`：未安装
- `yaml`：未安装
- `pyserial`：已安装 3.5
- `tomllib`：Python 3.11+ 内置可用

### 5.3 开发工具

- 当前未安装 PlatformIO CLI。后续需要在本地安装或改用 VS Code 插件。

---

## 6. 通信协议设计

### 6.1 BLE GATT

ESP32 作为 **Peripheral**，Mac 作为 **Central**。

已为本项目生成合法 UUID（可用 `uuidgen` 重新生成）：

| 项目 | UUID |
|---|---|
| Service | `CDD4DFFB-2FD0-4F2D-9A87-C7D2535B59E0` |
| State（写） | `DA3CDABA-E192-460B-ACF3-B2C59C6A3EE0` |
| Status（notify） | `7497D0A0-FE42-4E2D-B28E-93D083A7CD68` |
| Command（写） | `7C9DA1DE-8FE4-4B95-A366-51EA94E3010C` |

设备广播名建议：`CodexPet`。

### 6.2 二进制状态包（第一版）

DevGuide 明确要求用紧凑二进制而非 JSON：

| 字节 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | `version` | uint8 | 协议版本，固定 `0x01` |
| 1 | `state` | uint8 | 见状态枚举 |
| 2 | `progress` | uint8 | 0–100，255 表示无效 |
| 3 | `mood` | uint8 | 预留，第一版可填 0 |
| 4 | `animation` | uint8 | 预留，第一版可填 0 |
| 5 | `flags` | uint8 | bit0=心跳，bit1=强制刷新等 |
| 6–9 | `timestamp` | uint32 LE | Unix 时间戳 |

状态枚举：

| 值 | 状态 |
|---|---|
| 0 | `OFFLINE` |
| 1 | `IDLE` |
| 2 | `WORKING` |
| 3 | `WAITING` |
| 4 | `COMPLETED` |
| 5 | `ERROR` |
| 6 | `SLEEP` |

示例：`01 02 48 00 00 00 64 0A 2F 3A` → v1, WORKING, 72%, 时间戳 2026-08-13 11:45:11。

### 6.3 Task Text

任务名称单独一个 characteristic（`TASK_TEXT`），UTF-8 字符串，建议最大 64 字节。ESP32 端做截断/滚动显示。

### 6.4 心跳与离线检测

| 方向 | 周期 | 行为 |
|---|---|---|
| Bridge → ESP32 | 2 秒 | 发送心跳状态包（`flags` bit0=1） |
| ESP32 超时判断 | 15 秒 | 未收到任何包则进入 `OFFLINE` |
| ESP32 本地守护 | 5 秒 | 检查心跳，刷新屏幕时间等 |

### 6.5 状态 → 动画映射

| 状态 | 默认动画 | 备注 |
|---|---|---|
| `OFFLINE` | offline | 低功耗，屏幕常亮显示 |
| `IDLE` | idle | 小猫呼吸/眨眼 |
| `WORKING` | working | 疯狂打字/忙碌 |
| `WAITING` | waiting | 疑惑/等待 |
| `COMPLETED` | completed | 短暂庆祝后回到 idle |
| `ERROR` | error | 沮丧/摇头 |
| `SLEEP` | sleep | 长时间 IDLE 后进入 |

状态切换时建议插入过渡动画：

```text
IDLE → WORKING: wake_up → working
WORKING → COMPLETED: working → celebrate → idle
WORKING → ERROR: working → shock → error
```

---

## 7. 动画与 UI 方案

### 7.1 动画格式

- 单帧尺寸：128×128（推荐）
- 像素格式：RGB565（2 字节/像素）
- 单帧大小：约 32 KB
- 10 帧动画：约 320 KB，ESP32-S3 完全可承受
- 存储：优先预加载到 PSRAM；PSRAM 不足时从 Flash 按需 push

### 7.2 渲染流程

```text
LovyanGFX Sprite (128x128 RGB565)
        ↓
画到屏幕中心
        ↓
叠加状态文字、进度条、任务名
        ↓
pushSprite / flush
```

### 7.3 UI 布局（320×240 横屏）

```text
┌────────────────────────────────┐
│ CODEX PET             ● ONLINE │  ← 标题 + 连接状态
│                                │
│         /\_/\                  │
│        ( o.o )                 │  ← 128x128 宠物动画
│         > ^ <                  │
│                                │
│       WORKING...               │  ← 当前状态文字
│                                │
│ ███████████████░░░░  72%       │  ← 进度条（可选）
│                                │
│ task: fixing display driver    │  ← 任务名滚动显示
└────────────────────────────────┘
```

---

## 8. 推荐目录结构

与 DevGuide 最终目录保持一致：

```text
codex-pet/
├── firmware/
│   ├── platformio.ini
│   └── src/
│       ├── main.cpp
│       ├── config/
│       ├── display/
│       ├── pet/
│       ├── communication/
│       ├── storage/
│       └── ui/
├── bridge/
│   ├── main.py
│   ├── codex_monitor.py
│   ├── ble_server.py
│   ├── protocol.py
│   ├── config.py
│   ├── hook.sh
│   └── requirements.txt
├── assets/
│   ├── idle/
│   ├── working/
│   ├── waiting/
│   ├── completed/
│   ├── error/
│   └── sleep/
├── docs/
│   ├── pre_dev_research.md
│   ├── protocol.md
│   └── hardware.md
└── README.md
```

---

## 9. 开发顺序（严格遵守）

按 DevGuide Phase 执行：

| 阶段 | 任务 | 目标 |
|---|---|---|
| Phase 1 | ESP32 LCD + 小猫动画 | 屏幕稳定 ≥20 FPS |
| Phase 2 | BLE + 手工发状态 | Mac 用脚本发送状态，验证小猫变化 |
| Phase 3 | Codex Monitor | 识别 IDLE/WORKING/COMPLETED/ERROR |
| Phase 4 | 合并 | Codex → Bridge → BLE → ESP32 MVP |
| Phase 5 | WAITING / SLEEP / Task Text | 完善状态 |
| Phase 6 | Usage | 后续扩展 |
| Phase 7 | 养成 / 音效 / 震动 / Wi-Fi / OTA | 最后再做 |

---

## 10. 风险与待确认事项

| # | 风险/问题 | 影响 | 建议处理 |
|---|---|---|---|
| 1 | 未知具体开发板与屏幕引脚 | Phase 1 阻塞 | 用户确认硬件型号或提供 pinout |
| 2 | Codex hook 安装后需用户批准 | 首次集成失败 | 在 bridge 安装脚本中提示执行 `/hooks` |
| 3 | hook 超时/脚本崩溃 | 状态丢失 | 用 JSONL fallback 兜底 |
| 4 | BLE 连接在 Mac 睡眠后断开 | 需要重连 | bridge 断线自动重连，ESP32 15s 超时切 OFFLINE |
| 5 | 动画帧过多导致 PSRAM 不足 | 内存泄漏/卡顿 | 预加载前检查 free heap/psram，按需分页 |
| 6 | 屏幕控制器型号多样 | 代码硬编码 | Display HAL + build flag |
| 7 | 当前环境缺少 PlatformIO、bleak、PyYAML | 开发环境不齐 | 后续安装 |

---

## 11. 第一阶段验收标准（摘自 DevGuide）

### A. BLE
- [ ] ESP32 能被 Mac 发现
- [ ] Bridge 能自动连接
- [ ] 断开后能自动重连
- [ ] ESP32 能检测离线

### B. 状态
- [ ] IDLE、WORKING、WAITING、COMPLETED、ERROR、OFFLINE 全部正确显示

### C. 动画
- [ ] idle、working、waiting、completed、error、offline 动画可用

### D. 性能
- [ ] FPS ≥ 20
- [ ] 无明显卡顿
- [ ] 无持续内存泄漏
- [ ] 连续运行 24h 不死机

### E. Bridge
- [ ] Codex 活动能被检测
- [ ] 状态能转换、能发送
- [ ] Bridge 崩溃后可重新启动
- [ ] ESP32 不依赖 Bridge 也能运行

---

## 附录

### A. 生成好的 BLE UUID

```text
Service:        CDD4DFFB-2FD0-4F2D-9A87-C7D2535B59E0
State (write):  DA3CDABA-E192-460B-ACF3-B2C59C6A3EE0
Status (notify):7497D0A0-FE42-4E2D-B28E-93D083A7CD68
Command (write):7C9DA1DE-8FE4-4B95-A366-51EA94E3010C
```

### B. 示例 `~/.codex/hooks.json`（供 bridge 安装使用）

```json
{
  "hooks": {
    "SessionStart": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh SessionStart", "timeout": 30 }] }
    ],
    "UserPromptSubmit": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh UserPromptSubmit", "timeout": 30 }] }
    ],
    "PreToolUse": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh PreToolUse", "timeout": 30 }] }
    ],
    "PermissionRequest": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh PermissionRequest", "timeout": 600 }] }
    ],
    "PostToolUse": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh PostToolUse", "timeout": 30 }] }
    ],
    "Stop": [
      { "hooks": [{ "type": "command", "command": "/Users/yanx/ESP32/CodexPet/bridge/hook.sh Stop", "timeout": 30 }] }
    ]
  }
}
```

> 实际 hook 脚本会接收 stdin 的 JSON payload，并转发给 bridge。

### C. 示例二进制状态包

```text
v1 + IDLE + 0% + 0 mood + 0 anim + 心跳标志
01 01 00 00 00 01 64 0A 2F 3A
```

```text
v1 + WORKING + 72% + 0 mood + 0 anim + 无标志
01 02 48 00 00 00 64 0A 2F 3A
```

### D. 建议的 ESP32 库版本

| 库 | 版本 |
|---|---|
| LovyanGFX | `^1.2.0` |
| NimBLE-Arduino | `^1.4.0` |
| ArduinoJson | `^7.0.0`（仅在需要时） |

