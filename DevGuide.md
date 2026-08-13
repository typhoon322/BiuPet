下面这份我按**真正可以开工的工程规格**写，核心目标是：**Mac 上监听 Codex → Bridge → BLE/Wi-Fi → ESP32-S3 → SPI 屏幕 → 实体 Codex 宠物**。第一版先把状态联动跑通，后面再扩展养成系统。

# Codex Pet —— ESP32-S3 实体桌面宠物开发文档

## 1. 项目目标

开发一个基于 **ESP32-S3 + SPI LCD** 的实体桌面宠物。

它不是把 Codex 运行在 ESP32 上，而是：

```text
┌─────────────────────┐
│       Mac           │
│                     │
│ Codex CLI / Codex   │
│         ↓           │
│  codex-pet-bridge   │
└──────────┬──────────┘
           │ BLE
           │
           ▼
┌─────────────────────┐
│      ESP32-S3       │
│                     │
│   Pet State Engine  │
│          ↓          │
│    Animation Engine │
│          ↓          │
│       SPI LCD       │
└─────────────────────┘
```

最终效果：

- Codex 开始工作 → 宠物醒来
- Codex 正在执行 → 宠物工作/打字
- Codex 等待输入 → 宠物疑惑/等待
- Codex 完成 → 宠物庆祝
- Codex 出错 → 宠物沮丧
- Codex 长时间空闲 → 宠物睡觉
- Mac 端可以向 ESP32 推送任务信息
- ESP32 即使断开 Mac，也能继续显示本地状态
- 后续可增加宠物养成、音效、震动、按键互动等

---

# 2. 第一阶段目标

**第一阶段不要做养成系统。**

只完成：

```text
Codex
  ↓
Mac Bridge
  ↓
BLE
  ↓
ESP32-S3
  ↓
SPI LCD
  ↓
动画
```

必须实现以下状态：

```text
OFFLINE
IDLE
WORKING
WAITING
COMPLETED
ERROR
```

并且状态变化必须能够驱动动画。

---

# 3. 硬件要求

## 推荐硬件

### 主控

ESP32-S3。

推荐：

```text
ESP32-S3
Flash ≥ 8MB
PSRAM ≥ 2MB
```

如果已有 ESP32-S3 开发板，直接使用，不要求重新购买。

---

## LCD

SPI LCD。

推荐：

```text
240×320
320×240
```

常见控制器：

```text
ST7789
ILI9341
GC9A01
```

开发时不要把屏幕控制器写死。

必须设计：

```text
Display HAL
```

例如：

```text
Display
 ├── ST7789
 ├── ILI9341
 └── GC9A01
```

---

# 4. 软件架构

ESP32：

```text
src/
├── main.cpp
│
├── config/
│   └── config.h
│
├── display/
│   ├── display.h
│   ├── display.cpp
│   └── display_config.h
│
├── pet/
│   ├── pet.h
│   ├── pet.cpp
│   ├── pet_state.h
│   └── pet_animation.h
│
├── communication/
│   ├── ble_manager.h
│   ├── ble_manager.cpp
│   ├── protocol.h
│   └── protocol.cpp
│
├── storage/
│   ├── storage.h
│   └── storage.cpp
│
└── ui/
    ├── ui.h
    └── ui.cpp
```

Mac：

```text
bridge/
├── main.py
├── codex_monitor.py
├── ble_server.py
├── protocol.py
├── config.py
└── requirements.txt
```

---

# 5. ESP32 软件框架

推荐：

```text
PlatformIO
+
Arduino Framework
+
LovyanGFX
```

如果使用 LVGL 有必要也可以使用，但**第一版不强制 LVGL**。

第一版推荐：

```text
ESP32-S3
+
LovyanGFX
+
自定义 Sprite 动画
```

原因：

- 宠物动画需求简单
- 不需要复杂控件
- 内存占用更低
- 性能更好控制
- 后续可以自己管理动画帧

---

# 6. 屏幕设计

默认 320×240 横屏。

界面：

```text
┌────────────────────────────────┐
│ CODEX PET             ● ONLINE │
│                                │
│                                │
│           /\_/\\               │
│          ( o.o )               │
│           > ^ <                │
│                                │
│         WORKING...             │
│                                │
│ ███████████████░░░░  72%       │
│                                │
│ task: fixing display driver    │
└────────────────────────────────┘
```

如果没有任务名称：

```text
task: -
```

---

# 7. 宠物状态机

核心状态：

```cpp
enum PetState {
    OFFLINE,
    IDLE,
    WORKING,
    WAITING,
    COMPLETED,
    ERROR,
    SLEEP
};
```

状态转换：

```text
                ┌──────────┐
                │ OFFLINE  │
                └────┬─────┘
                     │ connected
                     ▼
                ┌──────────┐
         ┌─────►│   IDLE   │◄─────┐
         │      └────┬─────┘      │
         │           │             │
         │           ▼             │
         │      ┌──────────┐       │
         │      │ WORKING  │       │
         │      └────┬─────┘       │
         │           │             │
         │     ┌─────┴─────┐       │
         │     ▼           ▼       │
         │ WAITING      COMPLETED  │
         │     │           │       │
         │     └─────┬─────┘       │
         │           ▼             │
         │          IDLE ──────────┘
         │
         └──────── ERROR
```

长时间 IDLE：

```text
IDLE
 ↓
超过 5 分钟
 ↓
SLEEP
```

收到新的 Codex 活动：

```text
SLEEP
 ↓
WORKING
```

---

# 8. 动画系统

不要把动画逻辑写死在状态机里面。

使用：

```cpp
Animation {
    name
    frameCount
    fps
    loop
}
```

例如：

```text
idle
 ├── frame01
 ├── frame02
 ├── frame03
 └── frame04

working
 ├── frame01
 ├── frame02
 ├── frame03
 ├── frame04
 ├── frame05
 └── frame06

waiting
 ├── frame01
 ├── frame02
 └── frame03

completed
 ├── frame01
 ├── frame02
 ├── frame03
 ├── frame04
 └── frame05

error
 ├── frame01
 ├── frame02
 └── frame03

sleep
 ├── frame01
 ├── frame02
 └── frame03
```

第一版可以先使用简单 PNG/RGB565 图片。

---

# 9. 动画要求

动画不要每次重新从 Flash 解码大图。

推荐：

```text
Sprite
+
预加载
+
RGB565
```

例如：

```text
PET_SIZE = 128×128
```

单帧：

```text
128 × 128 × 2
≈ 32KB
```

10 帧：

```text
≈ 320KB
```

ESP32-S3 完全可以接受。

如果 PSRAM 可用，可以把动画缓存放进 PSRAM。

---

# 10. Mac Bridge

这是整个项目最重要的部分。

Mac 上运行：

```bash
codex-pet-bridge
```

负责：

```text
监听 Codex
↓
解析当前状态
↓
转换成统一协议
↓
通过 BLE 发送给 ESP32
```

---

# 11. Bridge 不允许直接依赖 ESP32

必须设计协议层：

```text
Codex
 ↓
CodexMonitor
 ↓
PetProtocol
 ↓
BLE
```

这样以后可以：

```text
BLE
Wi-Fi
WebSocket
USB Serial
```

任意替换。

---

# 12. Codex Monitor

不要假设 Codex 内部 API 永远存在。

Bridge 应该使用**公开、稳定、可维护的方式**获取状态。

优先级：

```text
1. Codex CLI 可观察状态
2. Codex 官方提供的接口
3. 本地进程/事件信息
4. 日志/状态文件
5. 必要时再做兼容性解析
```

**禁止第一版依赖破解/逆向 Codex 内部协议。**

如果某种状态无法可靠获取：

```text
UNKNOWN
```

而不是伪造状态。

---

# 13. Bridge 状态模型

定义：

```json
{
  "state": "working",
  "progress": 72,
  "task": "implement SPI display driver",
  "timestamp": 1786600000
}
```

字段：

| 字段      | 类型   | 说明        |
| --------- | ------ | ----------- |
| state     | string | 当前状态    |
| progress  | int    | 0-100       |
| task      | string | 当前任务    |
| timestamp | int    | Unix 时间戳 |

---

# 14. ESP32 通信协议

第一版建议 BLE。

ESP32 作为：

```text
BLE Peripheral
```

Mac：

```text
BLE Central
```

---

# 15. BLE Service

定义自有 UUID。

例如：

```text
Service UUID

xxxxxxxx-xxxx-xxxx-xxxx-codexpet0001
```

Characteristic：

```text
STATE
WRITE

STATUS
NOTIFY

COMMAND
WRITE
```

注意：

**实际 UUID 不要真的使用上面的字符串。**

由开发者生成合法 UUID。

---

# 16. 数据包

第一版不要直接传 JSON。

使用紧凑二进制：

```text
Byte 0
Protocol Version

Byte 1
State

Byte 2
Progress

Byte 3
Mood

Byte 4
Animation

Byte 5
Flags

Byte 6-9
Timestamp
```

例如：

```text
01 02 48 03 07 00 xx xx xx xx
```

解释：

```text
01 = protocol v1
02 = WORKING
48 = 72%
03 = focused
07 = typing animation
```

---

# 17. 为什么不用 JSON

因为：

```text
ESP32
+
BLE
```

根本不需要传：

```json
{
  "state": "working",
  "progress": 72,
  "mood": "focused",
  "animation": "typing"
}
```

二进制：

```text
01 02 48 03 07
```

更适合嵌入式。

但是：

**Mac Bridge 内部可以继续使用 JSON 对象。**

协议转换：

```text
Codex
 ↓
Python dict
 ↓
Binary packet
 ↓
BLE
```

---

# 18. Task 文本

任务名称不建议塞进状态数据包。

单独 Characteristic：

```text
TASK_TEXT
WRITE
```

例如：

```text
fix SPI display driver
```

ESP32 收到后：

```text
taskText = ...
```

显示时：

```text
task: fix SPI display driver
```

超过屏幕宽度：

```text
滚动显示
```

---

# 19. 心跳机制

必须有。

ESP32 每隔：

```text
5 秒
```

判断 Bridge 是否还在线。

Bridge 每隔：

```text
2 秒
```

发送 heartbeat。

超过：

```text
15 秒
```

没有收到数据：

```text
OFFLINE
```

显示：

```text
● OFFLINE
```

---

# 20. 离线行为

如果 Mac 关机：

```text
Bridge 消失
 ↓
ESP32
 ↓
15 秒
 ↓
OFFLINE
```

但宠物不能黑屏。

继续显示：

```text
Codex Pet

       /\_/\
      ( -.- )

     OFFLINE
```

并进入低功耗动画。

---

# 21. 状态 → 动画映射

默认：

```text
OFFLINE
→ offline

IDLE
→ idle

WORKING
→ working

WAITING
→ waiting

COMPLETED
→ celebration

ERROR
→ error

SLEEP
→ sleep
```

---

# 22. Mood 系统

第一版只实现：

```text
NORMAL
FOCUSED
HAPPY
CONFUSED
SAD
SLEEPY
```

映射：

```text
WORKING
→ FOCUSED

COMPLETED
→ HAPPY

WAITING
→ CONFUSED

ERROR
→ SAD

SLEEP
→ SLEEPY
```

---

# 23. 状态变化动画

非常重要：

**不要状态一变就瞬间切图。**

例如：

```text
IDLE
 ↓
WORKING
```

播放：

```text
wake_up
 ↓
working
```

完成：

```text
working
 ↓
celebrate
 ↓
idle
```

错误：

```text
working
 ↓
shock
 ↓
error
```

这样会明显更像宠物，而不是仪表盘。

---

# 24. 按键

如果 ESP32 开发板有按键，支持：

### 短按

切换页面：

```text
PET
→ STATUS
→ USAGE
→ CLOCK
→ PET
```

### 长按

进入：

```text
SETTINGS
```

第一版没有按键也没关系。

---

# 25. 后续 Usage 页面

第二阶段增加：

```text
CODEX USAGE
```

例如：

```text
┌────────────────────────────┐
│ CODEX USAGE                │
│                            │
│  5 HOURS                   │
│  ███████████░░░  73%       │
│                            │
│  7 DAYS                    │
│  █████████░░░░░  61%       │
│                            │
│  Today                     │
│  02:31:42                  │
└────────────────────────────┘
```

**Usage 数据获取必须和状态获取解耦。**

---

# 26. 后续养成系统

第二阶段再做。

数据：

```text
level
exp
happiness
energy
friendship
tasksCompleted
workingTime
```

例如：

```text
完成一个 Codex 任务
→ +10 EXP

连续工作
→ +friendship

长时间无人使用
→ energy 下降

错误
→ mood -5

完成大型任务
→ mood +20
```

---

# 27. 宠物成长

例如：

```text
Lv.1
小猫

Lv.5
开始戴眼镜

Lv.10
出现小键盘

Lv.20
获得程序员帽子
```

这些都属于后续版本。

**第一版不要实现。**

---

# 28. Wi-Fi

BLE 跑通之后，再增加 Wi-Fi。

Wi-Fi 模式：

```text
Mac
 ↓
HTTP / WebSocket
 ↓
ESP32
```

优点：

- 数据更多
- 可以传图片
- 可以 OTA
- 可以访问 WebUI
- 可以远程控制

但：

**BLE 是第一版首选。**

---

# 29. OTA

第二阶段增加：

```text
ESP32 OTA
```

避免每次修改动画都插 USB。

最终：

```text
Mac
 ↓
codex-pet-bridge
 ↓
Wi-Fi
 ↓
ESP32
```

同时：

```text
ESP32 WebUI
```

可以查看：

```text
Firmware version
BLE status
Wi-Fi status
Pet state
FPS
Heap
PSRAM
```

---

# 30. 性能要求

目标：

```text
LCD FPS ≥ 20
```

动画：

```text
10~20 FPS
```

通信：

```text
BLE heartbeat ≤ 5s
```

主循环不能因为：

```text
BLE
图片
动画
```

阻塞。

禁止：

```cpp
delay(1000);
```

大量使用。

应该采用：

```cpp
millis()
```

或者 FreeRTOS task。

---

# 31. 推荐任务划分

ESP32：

```text
Task 1
Display Task

Task 2
BLE Task

Task 3
Pet State Task

Task 4
Animation Task
```

不要所有事情都塞到：

```cpp
loop()
```

---

# 32. 内存要求

启动后记录：

```text
Free Heap
Free PSRAM
```

每次加载动画后检查：

```text
heap
psram
```

禁止出现：

```text
memory leak
```

长时间运行测试：

```text
24h
```

---

# 33. 容错

ESP32 必须能处理：

```text
BLE 断开
BLE 重连
Mac 重启
Bridge 崩溃
异常数据包
错误 CRC
任务文本过长
未知 state
```

未知 state：

```text
→ IDLE
```

非法数据：

```text
→ 丢弃
```

绝不能：

```text
crash
reboot loop
```

---

# 34. 日志

开发版开启 Serial：

```text
115200
```

输出：

```text
[BLE] connected
[BLE] packet received
[PET] IDLE -> WORKING
[PET] animation = typing
[DISPLAY] FPS = 19.8
```

错误：

```text
[ERROR] invalid packet
[ERROR] animation not found
```

Release 版本降低日志量。

---

# 35. Bridge 日志

Mac：

```text
[Codex] detected
[Codex] state = working
[BLE] connected
[BLE] send state=working
[Codex] state = completed
[BLE] send state=completed
```

提供：

```bash
codex-pet-bridge --debug
```

查看完整事件。

---

# 36. 配置文件

Bridge：

```yaml
device:
  name: CodexPet

connection:
  mode: ble

monitor:
  interval: 1

display:
  send_task: true

debug:
  enabled: false
```

不要把配置写死在 Python。

---

# 37. 开机流程

ESP32：

```text
Power On
 ↓
初始化硬件
 ↓
初始化 LCD
 ↓
显示 Logo
 ↓
初始化 BLE
 ↓
等待连接
 ↓
显示 OFFLINE
 ↓
Mac Bridge 连接
 ↓
收到状态
 ↓
进入对应状态
```

---

# 38. Mac Bridge 开机流程

```text
启动
 ↓
检测 Codex
 ↓
启动 monitor
 ↓
扫描 ESP32
 ↓
连接 CodexPet
 ↓
发送 ONLINE
 ↓
发送当前状态
 ↓
持续监听
```

---

# 39. 第一版验收标准

必须全部通过。

### A. BLE

- [x] ESP32 能被 Mac 发现
- [x] Bridge 能自动连接
- [x] 断开后能自动重连
- [x] ESP32 能检测离线

### B. 状态

- [x] IDLE
- [x] WORKING
- [x] WAITING
- [x] COMPLETED
- [x] ERROR
- [x] OFFLINE

全部正确显示。

### C. 动画

- [x] idle
- [x] working
- [x] waiting
- [x] completed
- [x] error
- [x] offline

### D. 性能

- [x] FPS ≥ 20
- [x] 无明显卡顿
- [x] 无持续内存泄漏
- [ ] 连续运行 24h 不死机

### E. Bridge

- [x] Codex 活动能被检测
- [x] 状态能转换
- [x] 状态能发送
- [x] Bridge 崩溃后可重新启动
- [x] ESP32 不依赖 Bridge 才能运行

---

# 40. 开发顺序

**严格按照这个顺序，不要一开始就做一堆功能。**

### Phase 1 ✅ 已完成

ESP32：

```text
SPI LCD
+
一个小猫
+
动画
```

先确认屏幕稳定 20FPS。

---

### Phase 2 ✅ 已完成

ESP32：

```text
BLE
+
手工发送状态
```

Mac 先用 Python：

```python
send("working")
```

验证：

```text
Mac
 ↓
BLE
 ↓
ESP32
 ↓
小猫变化
```

---

### Phase 3 ✅ 已完成

做：

```text
Codex Monitor
```

先只识别：

```text
IDLE
WORKING
COMPLETED
ERROR
```

---

### Phase 4 ✅ 已完成

合并：

```text
Codex
 ↓
Bridge
 ↓
BLE
 ↓
ESP32
```

完成 MVP。

---

### Phase 5 ✅ 已完成

增加：

```text
WAITING
SLEEP
TASK TEXT
```

---

### Phase 6 ✅ 已完成

再做：

```text
Usage
```

---

### Phase 7 🚧 进行中（养成基础、Wi-Fi/OTA、DeepSeek 余额已完成；皮肤动画已删除；音效/震动/WebUI 待做）

最后才做：

```text
养成
等级
经验
心情
音效
震动
Wi-Fi
OTA
```

#### Phase 7 已落地

**Wi-Fi 控制**（`firmware/src/communication/wifi_server.cpp`）：

- AP 常开 + STA 后台连接（EnvMonitor 模型）：SoftAP `CodexPet-AP` / `codexpet123`（192.168.4.1）永远在线，STA 用退避重试连家里网络，两者互不干扰
- 凭据优先读 NVS（`codepet`/`wifi_ssid`+`wifi_pass`），没有则回退 `firmware/src/config/secrets.h`（gitignore）；**网页/接口可随时改**：
  - `GET /api/wifi` 查看状态；`POST /api/wifi` JSON `{"ssid":"...","pass":"..."}` 保存并重连
  - 手机连 `CodexPet-AP` 后打开 `http://192.168.4.1` 即可设置
- HTTP API：
  - `POST /api/state` JSON `{"state":"WORKING","task":"..."}`，state 也接受数字 0..6
- 皮肤上传接口与 `assets/`、`convert_gif.py`、`skin_upload.py` 已删除（按需求回到内置程序化橘猫）
- ⚠️ 本机同时跑 BLE（NimBLE）时**不要调用 `WiFi.setSleep(false)`**——ESP-IDF 会直接 abort（"Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled"）；EnvMonitor 是纯 WiFi 的 C3 才不受影响

**OTA**（`ArduinoOTA`，hostname `codex-pet`）：

```bash
# 电脑与 ESP32 同网时，平台上的 PIO Remote / Arduino IDE 直接 OTA 刷固件
# 或用 esptool 上传 firmware.bin 到 OTA 分区
```

分区表 `default_16MB.csv` 已带 `app0`/`app1` OTA 分区。

**DeepSeek 余额**（`firmware/src/net/deepseek_balance.cpp`）：

- 参考 EnvMonitor 的 on-device 实现：ESP32 后台任务每 30 秒调 `https://api.deepseek.com/user/balance`（Bearer key，WiFiClientSecure `setInsecure`），只在家里 Wi-Fi 连上时请求
- Key 放 `firmware/src/config/secrets.h`（gitignore，`DEEPSEEK_API_KEY`），首次上电写入 NVS（`codepet`/`ds_key`），后续 OTA 不丢
- 右下角显示小鲸鱼 logo（DeepSeek 蓝）+ 余额数字；请求失败保留上次值
- 已验证接口：`is_available=true`，余额从 DeepSeek 平台实时返回

**用量统计（Codex 直连 DeepSeek）**：仍有效。DeepSeek 走 `wire_api = "responses"` 时 Codex session JSONL 照常写 `event_msg` / `token_count`，`total_token_usage.total_tokens` 由 DeepSeek 回传，`bridge/usage_tracker.py` 解析逻辑不变（实测当日 2.7M tokens）。

⚠️ `usage_tracker.py` 修复：之前缓存把「文件字节数」当 token 返回（session 文件 ~1.3MB 时显示 today 1.30M tok），已改为缓存 (size, tokens)。

**界面**：

- 右上角 WiFi 指示灯：`WiFi` 绿 / `No WiFi` 红；中间显示状态名，左上是 ONLINE/OFFLINE（BLE）
- 左下角不再显示 demo 文字；底部只显示 task（ASCII 化截断）和右下角鲸鱼+余额
- 宠物动画改为**侧视行走橘猫**（方案 A）：IDLE 左右踱步，WORKING 站立打字，COMPLETED 蹦跳，SLEEP 蜷缩，ERROR 抖动
- WebUI（`http://<esp-ip>` 或连 `CodexPet-AP` 后 `192.168.4.1`）：深色仪表盘 + `GET /api/status`（状态/BLE/WiFi/余额/用量/内存）+ WiFi 设置表单（`POST /api/wifi`）

---

# 41. 明确禁止的事情

Kimi 开发过程中必须遵守：

### 禁止 1

不要一上来做 LVGL 大型 UI。

### 禁止 2

不要把 Codex API 写死进 ESP32。

### 禁止 3

不要让 ESP32 直接访问 Codex。

### 禁止 4

不要把状态机和显示代码耦合。

### 禁止 5

不要第一版加入数据库。

### 禁止 6

不要第一版加入云服务。

### 禁止 7

不要为了“架构优雅”过度设计。

### 禁止 8

不要没有实际测试就声称完成。

---

# 42. 最终目录

最终项目：

```text
codex-pet/
│
├── firmware/
│   ├── platformio.ini
│   └── src/
│
├── bridge/
│   ├── main.py
│   ├── codex_monitor.py
│   ├── ble_server.py
│   ├── protocol.py
│   └── requirements.txt
│
├── docs/
│   ├── protocol.md
│   ├── hardware.md
│   └── development.md
│
└── README.md
```

---

# 43. 最终产品效果

最终桌面上放一个小盒子：

```text
             ┌───────────────────┐
             │  CODEX PET        │
             │                   │
             │       /\_/\       │
             │      ( •ω• )      │
             │       > ^ <       │
             │                   │
             │   ● WORKING       │
             │                   │
             │ ██████████░░ 82%  │
             │                   │
             │ fixing SPI bug... │
             └───────────────────┘
```

你在 Mac 上让 Codex 干活：

```text
Codex 开始
    ↓
小猫睁眼
    ↓
开始疯狂打字
    ↓
Codex 调工具
    ↓
小猫忙碌
    ↓
Codex 完成
    ↓
小猫庆祝
    ↓
回到待机
    ↓
长时间没人用
    ↓
睡觉
```

**这就是第一版真正应该做出来的东西。**

另外有一点我建议 Kimi **必须先做技术调研再写代码**：目前网上已经存在至少两类 Codex 实体宠物实现思路——包括 **ESP32-S3 + BLE + 动画 + usage/status**，以及 Tamagotchi 风格的状态/养成设计。可以把它们当作参考，但不要直接复制实现；尤其是 **Codex 状态获取方式**，要先确认当前版本的实际可用接口/事件来源，再决定 `codex_monitor.py` 怎么写。
