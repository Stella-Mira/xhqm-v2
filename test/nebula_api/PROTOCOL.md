# PROTOCOL.md —— nebula_api 通道与消息协议（v1）

> M2 产出。控制面（元数据/指令/回执）走 nebula 通道；数据面（音视频字节）走 MediaMTX（rtsp 地址），
> 库不经手字节（D13-D15）。两端实现以本文档为准。

---

## 1. 总则

| 项 | 约定 |
|---|---|
| 消息载体 | json |
| 时间戳 | 纳秒（`ts_ns`，与 astral 对齐） |
| 协议版本 | 所有消息带 `"v":1`；语义破坏变更 = v 递增 |
| 兼容规则 | 收方**忽略未知字段**；未知 `op` 忽略（幂等安全） |
| 通道机制 | nebula 数据层通道：窗口数据点（devices/streams/acks）+ 订阅推送（commands） |
| 上下线语义 | 入网成功 = 注册（发 devices register）；心跳判死 = 注销（窗口过期兜底） |

## 2. 通道划分

| 通道 | 方向 | 数据形态 | 语义 |
|---|---|---|---|
| `devices` | 客户端 → 全网 | 窗口数据点 | 设备注册/注销（上线即发；被动下线靠窗口过期） |
| `streams` | 客户端 → 全网 | 窗口数据点 | 流发布/停止（起流即发地址） |
| `commands/<device_id>` | 服务端 → 设备 | 订阅推送 | 指令（设备订阅本地消费，执行后回执） |
| `acks` | 客户端 → 服务端 | 窗口数据点 | 指令回执 |

- 全局通道（devices/streams/acks）：**确定性 UUID 常量**（见 §6，编译进 `common/protocol.h`）。
- 设备指令通道：`commands/<device_id>` 的 uuid 由设备 id 确定性派生（§6）。
- 多传感器共享通道语义：持有通道 uuid 即可上传/订阅（nebula 数据层原生能力）。

## 3. 消息格式

### 3.0 订阅推送的数据形态（重要）

- **窗口通道**（devices/streams/acks）：订阅者收到的推送是**窗口快照**（数组
  `[{"time":..., "json":{消息}}, ...]`）。
  - **多发布者通道（devices/streams/acks）**：各发布者的窗口独立，订阅者必须
    **逐条处理快照全部条目**（register/unregister 累积），只取最后一条会把不同
    发布者互相覆盖（M4 实测：目录永远只剩一个设备）。
  - 单发布者「最新状态」场景可用 `common/protocol.h::latest_message(pushed)` 取最后一条。
- **指令通道**（commands/<device_id>）：`publish` 直发单条消息（非快照），
  订阅者收到的就是消息本身（`latest_message` 对非数组原样返回，两端可统一调用）。

### 3.1 devices（设备注册/注销）

```json
// 上线注册（客户端 listen 后即发）
{ "v": 1, "op": "register", "caps": { ...media::caps.to_json()... } }

// 主动注销（优雅下线前发；崩溃/断电靠窗口过期兜底）
{ "v": 1, "op": "unregister", "device": "cam.3090.0" }
```

- `caps` 结构 = `xhqm::hardware::media::caps::to_json()`：
  `{ id, host, kind, dir, medias:[], audios:[], videos:[] }`。
- 设备 id 全局唯一（命名空间 §2.6 总纲：`mic.<host>.*` / `cam.<host>.*` / `v.mix.<name>` 等）。

### 3.2 streams（流发布/停止）

```json
// 起流（设备 open 后、MediaMTX 地址就绪时发）
{ "v": 1, "op": "up", "device": "cam.3090.0",
  "url": "rtsp://192.168.1.20:8554/cam.3090.0",
  "format": { ...audio_format/video_format 协商结果... }, "ts_ns": 1789000000000000000 }

// 停流（设备 close 时发）
{ "v": 1, "op": "down", "device": "cam.3090.0", "ts_ns": 1789000000000000000 }
```

- 消费者：订阅 streams → 收到 `up` 即持有可拉流地址（MediaMTX 拉流）；`down` 或设备判死 → 放弃地址。
- 同一设备重复 `up`（改格式重启流）= 新地址覆盖旧地址（消费者按最新 `up` 为准）。

### 3.3 commands/<device_id>（指令）

```json
{ "v": 1, "id": "0f3c...（uuid 文本）", "action": "open",
  "params": { "format": { "rate": 16000, "channels": 1 } }, "ts_ns": 1789000000000000000 }
```

- `id`：指令 id（uuid），回执 `cmd_id` 与之对应。
- 设备侧：订阅 `commands/<自己的 device_id>` 通道 → `command_executor::execute(action, params)` → 回执。
- 投递可靠性：L3 三级投递（直连→中继→泛洪）；服务端发指令后等回执，**超时 3s 重发，重试上限 3 次**，仍无回执 = 设备判离线（§5）。

### 3.4 acks（指令回执）

```json
{ "v": 1, "cmd_id": "0f3c...", "device": "cam.3090.0", "ok": true,
  "result": { ...action 相关结果... }, "ts_ns": 1789000000000000000 }
```

- `ok:false` 时 `result.error` 为错误描述（对应 command_executor 的错误语义）。
- 服务端：订阅 acks → 按 `cmd_id` 匹配待决指令，清等待表。

## 4. 指令词汇（最小集，开放扩展）

| action | params | result（ok 时） |
|---|---|---|
| `open` | `{ "format": {...} }`（可选，缺省=设备默认格式） | `{ "url": "rtsp://...", "caps": {...} }` |
| `close` | `{}` | `{}` |
| `list` | `{}` | `{ "devices": [ caps... ] }`（本设备全部） |
| `get_caps` | `{}` | `{ "caps": {...} }` |

- 扩展规则：新 action = 设备端 `command_executor::bind` 注册 + 本文档登记；
  **未知 action 回 `{ok:false, error:"unknown_action"}`**，不视为协议错误。
- `params`/`result` 均为开放 json——协议只固定外壳（v/op/action/id），载荷随业务演进。

## 5. 可靠性语义

| 场景 | 行为 |
|---|---|
| 指令投递 | publish 走 L3（直连→中继→泛洪）；软状态订阅 10s 重发刷新路径 |
| 注册/流软状态 | **devices register 与 streams up 为软状态：客户端周期重发（10s）**——订阅扩散完成前的发布会丢（推送表未建），重发保证最终可见；窗口 60s 过期兜底 |
| 回执超时 | 服务端 3s 未收到 `acks` 回执 → 重发同 id 指令；3 次后标设备离线 |
| 设备离线 | 判死（心跳 fail 上限）→ 邻居清理；devices/streams 元数据随窗口过期（默认窗口 60s）；服务端可设更短 |
| 指令幂等 | 同 `id` 指令重复到达，设备端执行后仅回一次回执（去重链保障） |
| 优雅下线 | close 全部设备 + 发 streams down + 发 devices unregister + leave |

## 6. 全局通道 UUID 约定

| 通道 | uuid 来源 |
|---|---|
| `devices` / `streams` / `acks` | **固定常量**（编译进 `common/protocol.h`，写死 + 注释说明） |
| `commands/<device_id>` | **确定性派生**：SHA1(namespace_uuid, device_id) 截断成 uuid v5 风格——两端同算同得，无需协商 |

## 7. 版本与演进

- 所有消息带 `"v":1`；字段新增 = 旧端忽略；字段语义变更/删除 = v 递增。
- caps 扩展字段（新能力描述）只加不改，协议版本不动。
- 本文档是两端实现的对接口径——M4 客户端/服务端各自按此实现，再互测。

---

*M2 产出 · 2026-01*
