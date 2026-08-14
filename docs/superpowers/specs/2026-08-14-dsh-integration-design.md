# DSH（DeepSeek Harness）接入 —— 设计文档

日期：2026-08-14
状态：已批准

## 1. 目标

让 DeepSeek Harness（DSH）也能驱动宠物，与 Codex **同时**工作。DSH 的会话活动（开始/工作/调工具/等权限/完成）映射到宠物状态，经 BLE 推给 ESP32。

## 2. 现状（已实测）

- DSH 把每个会话写成 JSONL：`~/.dsh/sessions/<工作区slug>/<会话id>/session.jsonl.zstd`（zstd 压缩）。
- 事件类型（实测样本）：
  | 事件 | 状态 |
  |---|---|
  | `turn/start`、`tool/call`、`assistant/chunk`、`reasoning-chunks`、`step/start`、`user/message` | `WORKING` |
  | `approval/asked` | `WAITING` |
  | `turn/end`（reason.kind=completed） | `COMPLETED` → `IDLE` |
  | `user/message` 的 `data.content[].text` | 任务文本（中文） |

## 3. 方案

新增 `bridge/dsh_monitor.py`，**增量 tail DSH 的 session.jsonl.zstd**，复用现有 Codex JSONL monitor 的套路：

- 每 ~1s 扫描 `~/.dsh/sessions/**/<会话>/session.jsonl.zstd`（当前工作区 + 最近修改的会话）。
- 文件变大时才重新解压（zstd 流式），缓存 `(size, offset)`。
- 解析新增的 JSONL 行，按上表映射状态 + 提取任务文本。
- 状态变化回调给 StateHub。

## 4. 多源合并（StateHub）

`bridge/main.py` 加一个 StateHub，聚合 Codex monitor 与 DSH monitor 的状态：

- 每个 monitor 上报 `{state, task, ts}`。
- **最近活动优先**：谁的时间戳更新谁说了算；防止 Codex 的空闲→SLEEP 定时器覆盖 DSH 正在 WORKING。
- 超时兜底：两个 monitor 都长时间无活动 → `IDLE` → `SLEEP`。

## 5. 依赖

- `requirements.txt` 加 `zstandard`（Python zstd 流式解压）。

## 6. 涉及文件

| 文件 | 改动 |
|---|---|
| `bridge/dsh_monitor.py` | 新增：DSH JSONL tail + 事件映射 |
| `bridge/main.py` | 加 StateHub，聚合 Codex + DSH |
| `bridge/requirements.txt` | 加 `zstandard` |

固件**不动**；`codex_monitor.py` 不动（只加一个平级 monitor）。

## 7. 错误处理

- zstd 文件损坏/解压失败：跳过该文件，记 warning，不崩溃。
- 会话文件被轮换/删除：重置 offset 重新开始。
- 事件类型未知：忽略。
- 无 DSH 会话目录：monitor 空转，不影响 Codex。

## 8. 验收

- [ ] DSH 会话活动能被检测（用一个真实 DSH 会话验证）。
- [ ] 状态正确映射（WORKING / WAITING / COMPLETED）。
- [ ] 任务文本（中文）正确推送到设备任务行。
- [ ] Codex + DSH 同时运行时，状态不互相打架（最近活动优先）。
- [ ] bridge 崩溃可重启，不影响固件。
