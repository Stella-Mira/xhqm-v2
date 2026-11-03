# nebula_api —— 家庭音视频硬件抽象层

> 统合家里所有音视频硬件（麦克风、摄像头、音响、屏幕），向上层服务提供统一的输入输出能力。
> 技术栈：**C++（xhqm 库）**。本文档为项目总纲，所有决策以本文档为准。

---

## 1. 项目定位

一个抽象层项目，是家庭感知系统（ASR/TTS/人脸/声纹/唤醒等一切上层能力）的**地基**：

1. **设备统一注册**：麦克风、摄像头、音响、屏幕散在多台机器（4060 本机 / 3090 Win11 / mac / 未来边缘设备），统一发现、命名、状态管理。
2. **输入**：任意设备的采集 → 标准流（如 `mic.3090` → PCM 16k 流）。
3. **输出**：标准流 → 任意设备（TTS 音频 → 任意音响；视频 → 任意屏幕）。
4. **统合**：一路流可 fan-out 一分多（一路摄像头同时喂人脸识别+录制+屏幕）；多路可合成一路（混音、画面合成）。
5. **解耦**：上层服务只跟「流」打交道，不关心设备在哪台机器、什么驱动。

**分层哲学（D13）**：xhqm 库是**抽象底层，不做真实现**——只提供分布式组网骨架与抽象接口；
真实设备 I/O、音视频字节流全部在实现层（本项目的客户端/服务端）落地。

## 2. 决策记录（联合决策，按时间序）

| # | 决策 | 内容 |
|---|---|---|
| D1 | 场景范围 | 四场景全要：家人身份识别 / 语音交互升级 / 创作直播工具链 / 家庭监控告警 |
| D2 | 隐私边界 | **音频视频都不出家门**，本地服务为主路，云端仅兜底 |
| D3 | GPU 分工 | 4060（本机 ubt22）= 低延迟热路径；3090（Win11 服务器，24G/64G/3950X）= 重负载/批量/训练 |
| D4 | 抽象层结构 | 两端：边缘客户端（采集+受控执行）↔ 中枢服务端（抽象+路由+推流） |
| D5 | 计算位置 | **计算分散在客户端**（采集/编码/合成在客户端本地执行），服务端保持轻 |
| D6 | 上层接口 | **订阅式**：上层声明订阅某流，服务端转发 |
| D7 | 合成语义 | **显式声明**：合成器（mix/compose）是路由表里的显式节点，不默认经过 |
| D8 | 低功耗适配 | 客户端适配低功耗设备（树莓派/移动端，ARM）；算力不足时服务端兜底合成 |
| D9 | 技术栈 | **C++（xhqm 库）**，弃 Go 方案；复用 xhqm network/nebula/hardware |
| D10 | 实施顺序 | ① nebula 分布式层完善 → ② hardware 设备能力抽象定义 → ③ test/nebula_api 实现客户端与服务器 |
| D11 | 项目位置 | 客户端与服务器实现在 `xhqm/test/nebula_api/`（本文档所在目录） |
| D12 | 服务端口 | media 中枢服务端独立端口 **11110**（4060 网关旁），并入全局端口规划 |
| D13 | 分层哲学 | **xhqm 是抽象底层，不需要真正的实现**；真实功能 = 实现层继承 zero/data 落地 |
| D14 | 流式实现 | 数据面用现成帧服务器 **MediaMTX**（内嵌自包含，每客户端单二进制）；xhqm 不搬音视频字节 |
| D15 | 流发布模式 | **流 = 地址发布**：设备起流后把 `rtsp://...` 地址作为元数据进通道发布，消费者订阅拿地址自行拉流 |
| D16 | 指令模型 | **指令 = 数据 + 本地消费**：服务端 publish 到指令通道，客户端订阅（执行器消费）+ upload 回执——不造新协议 |
| D17 | 上下线语义 | **上线/下线即组网**：入网成功 = 注册，心跳判死 = 注销；设备目录不需要单独注册协议 |
| D18 | 模板边界 | nebula 模板止步 link 层：find 组合 link_b / post 继承 find / data 继承 post（纯虚 collect）/ zero 空壳继承 data |

## 3. 架构定稿

```
┌─ 边缘客户端（zero 子类，单二进制，低功耗友好）────────────┐
│ 设备抽象：media 设备接口实现（采集/播放）                  │
│ 流实现：内嵌 MediaMTX（~10MB Go 单二进制）起流/收流        │
│ 组网：nebula L1 入网（上线）/ 心跳判死（下线）             │
│ 注册：upload("devices", caps + 流地址)                    │
│ 指令：subscribe("commands/<id>", 执行器) ← 本地消费        │
│ 回执：upload("acks", {ok, result})                        │
│ 合成：mix 混音 / compose 拼画面（客户端内，显式声明）      │
└───────────────┬─────────────────────────────────────────┘
                │ 控制面：nebula 事件/通道（元数据与指令）
                │ 数据面：MediaMTX（rtsp:// 地址，库外拉流）
┌─ 中枢服务端（data 子类，11110，保持轻）───────────────────┐
│ 设备目录：订阅 devices 通道 = 全网设备清单（上下线自动）   │
│ 指令下发：publish("commands/<id>") → 等回执（超时重发）    │
│ 流消费：订阅拿地址 → MediaMTX 拉流 → 喂上层服务            │
│ 兜底：低功耗客户端声明后服务端代合成                       │
└───────────────┬─────────────────────────────────────────┘
                │ 订阅式接口
┌─ 上层消费端 ────────────────────────────────────────────┐
│ 唤醒 / ASR / TTS / 人脸 / sprite 桌宠 / 未来服务          │
└─────────────────────────────────────────────────────────┘
```

### 核心抽象

```
Device（设备）  →  Stream（流=地址）  →  Route（订阅路由）
kind: mic/cam/spk/screen    url: rtsp://...      in-device → out-consumer(s)
host / caps（能力描述）      meta: 格式/时间戳     支持 fan-out（一入多出）
```

- **流 = 地址发布**：xhqm 通道只搬运「谁有流、地址在哪」的元数据；字节流走 MediaMTX，库不经手。
- **指令 = 订阅消费**：指令是发布到通道的数据，订阅节点本地消费执行——与采集数据同构，零新协议。
- **上下线 = 组网**：设备目录无注册协议；流地址随设备生命走（下线判死后元数据随窗口过期）。
- **虚拟设备一等公民**：虚拟声卡桥（不自研驱动：Linux=PipeWire null-sink，Win=VB-CABLE/WASAPI loopback，mac=BlackHole）、混音器 mix、合成画面 compose，都是 Device 的实现，与物理设备同接口。
- **控制面/数据面分离**：控制面走 nebula（元数据/指令/回执）；数据面走 MediaMTX（音视频字节）。

### 设备命名空间

```
mic.<host>.*   cam.<host>.*   spk.<host>.*   screen.<host>.*
v.mix.<name>   v.compose.<name>   v.vloop.<name>    ← 虚拟设备
```

### 通道约定（D14-D17 落地，M2 协议文档细化）

| 通道 | 方向 | 内容 |
|---|---|---|
| `devices` | 客户端 → 全网 | 设备注册：caps + 流地址（上线即发，随窗口过期=下线） |
| `streams` | 客户端 → 全网 | 流发布：`{device, url, format}`（起流/停流事件） |
| `commands/<device_id>` | 服务端 → 设备 | 指令：`{action, params}`（开流/关流/调参/切换） |
| `acks` | 客户端 → 服务端 | 指令回执：`{cmd_id, ok, result}` |

## 4. 实施路线（主线，依赖顺序）

```
✅ M1  nebula 分布式层完善（B 世代迁移收尾 + 组合架构定型，r512-r516，73 断言验证）
⬜ M2  通道与消息格式协议约定（devices/streams/commands/acks）+ 约定验证测试
⬜ M3  hardware 设备能力抽象定义（media 抽象：caps/枚举/开闭流接口，只定义不实现）
⬜ M4  nebula_api 实现：客户端（zero+MediaMTX 内嵌+指令执行）/ 服务端（data 中枢+设备目录）
⬜ M5  合成节点（mix/compose，客户端内）+ 低功耗服务端兜底
⬜ 后续（另立项）ASR/TTS/人脸/声纹/唤醒 挂接
```

## 5. nebula 模块现状（M1 已完成）

`xhqm/network/nebula`（组合架构，模板止步 link 层）：

```
addr.h/cpp  地址多态（addr_b/addr_t<kind>）+ L1（link_b 组合 addr 句柄 / link_t<kind> 传输实现）
  └─（组合）find.h   L2：find 持有 shared_ptr<link_b>，事件钩子挂接 + find/list 发现
       └─（继承）post.h   L3：post 继承 find，upper_event 链 + 三级投递
            └─（继承）data.h/cpp  数据层：纯虚 collect() + 通道/窗口/查询/聚合/SSE/answer_handler
                 └─（继承）zero.h  采集节点命名（空壳，using data::data 构造透传）
legacy/  A 世代废弃文件（作迁移参考，不参与编译）
```

能力：L1 UDP 广播入网/心跳判死/自动补连接、L2 事件驱动发现 + dijkstra 下一跳、
L3 订阅推送（直连/中继/泛洪）、数据层通道窗口/查询聚合/SSE/astral 落库、
请求-应答（answer_handler 注入）、事件钩子（组合层挂接）。
验证：`test/nebula_api/nebula_api_test.cpp` 4 用例 73 断言（L1 三节点 / L2L3 十五节点 / 数据层 / zero 采集）。

M1 期间修复的 4 个库级缺陷：static_assert 依赖断言、submit 模板消歧、listen 广播地址顺序、
**钩子事件流驱动超时扫描（环断死锁）**——详见 nebula/README.md。

## 6. hardware 设备能力抽象（M3，设计要点）

现 `hardware/hardware.h` 仅计算设备选择（no/cpu/gpu/net/auto）。新增**音视频设备抽象**（`hardware/media.h`）：

```
kind         设备类别：mic / cam / spk / screen / mix / compose / vloop
dir          方向：input / output / duplex
media_type   audio / video
audio_format  采样率 / 通道 / 编码
video_format  分辨率 / 帧率 / 编码
caps         设备能力描述：id / host / kind / dir / 格式列表（json 往返）
device       设备接口抽象：capability() / open(config) / close() / good()
              流地址 stream_url()（实现层起流后返回）
device_list  枚举本机设备（实现层提供，xhqm 只定义签名）
```

原则（D13 落地）：**xhqm 只定义数据结构与接口签名，不定义字节搬运**——read/write 与
MediaMTX 桥接全在实现层；平台实现（ALSA/WASAPI/AVFoundation、V4L2/DShow）后续逐平台落地，
低功耗设备可只实现子集。

## 7. nebula_api 项目结构（规划）

```
test/nebula_api/
├── README.md          本文档（总纲）
├── PROTOCOL.md        通道与消息格式约定 v1（M2 已产出）
├── common/protocol.h  通道 uuid 常量 + 消息构造 + latest_message 消费辅助（M2 已产出）
├── client/            边缘客户端（zero 子类 + media 设备实现 + MediaMTX 内嵌 + 指令执行）
│   ├── device_impl.h  设备骨架（审查通过：后端可插拔/注册表/指令注册制）
│   └── device_client.h  客户端（上线注册/软状态刷新/指令订阅执行/优雅下线）
├── server/
│   └── media_server.h  中枢（设备目录累积/流地址缓存/指令下发回执等待）
└── nebula_api_test.cpp  独立验证（M1 组网 + M3 media + M2 协议 + M4 端到端，145 断言）
```

## 8. 端口规划（追加到全局 config）

```
11110  media 中枢服务端（本机 4060 网关旁）
+ 每客户端 MediaMTX 内嵌实例端口（设备自定，随流地址发布）
```

## 9. 里程碑与验收

| 里程碑 | 内容 | 验收 |
|---|---|---|
| M1 | nebula 迁移收尾 + 组合架构 | ✅ 全库编译 + 73 断言 ×3 次稳定（r512-r516） |
| M2 | 协议约定（通道/消息格式）+ 验证测试 | ✅ PROTOCOL.md v1 + common/protocol.h + T6（127 断言 ×2 稳定，r519） |
| M3 | hardware media 抽象定义 | caps json 往返 + 接口编译测试；抽象无平台实现 |
| M4 | nebula_api 客户端/服务端跑通 | ✅ client/device_client.h + server/media_server.h + T7 端到端（145 断言 ×4 稳定） |
| M5 | 合成节点（mix/compose）+ 兜底合成 | TTS+BGM 混音进虚拟声卡；合成画面推流 |

## 10. 待定项（open items）

1. 服务端鉴权方案（先 token，后收紧）。
2. 摄像头来源与位置（上层 P2 阶段前定）。
3. 告警通道选型（Telegram/微信/钉钉）。
4. 11094 本地 LLM 选型（qwen2.5-7B/14B 量化，3090 可跑）。
5. MediaMTX 内嵌方式：进程内 exec 子进程 vs 句柄库调用（M4 实现期定，先子进程）。
6. 项目名 `nebula_api` 为哥哥指定，沿用。

## 附录 A：上层技术选型调研摘要（后续立项用）

| 域 | 首选方案 | 备选 |
|---|---|---|
| 人脸识别 | insightface（SCRFD+ArcFace）+ Silent-Face-Anti-Spoofing 活体 | dlib |
| 声纹识别 | 3D-Speaker cam++ / ECAPA-TDNN + webrtcvad | speechbrain |
| 图像分割/识别 | YOLO11-seg 常驻 + SAM2 按需精修 | MobileSAM |
| 多角色 ASR | FunASR：MOSS-Transcribe-Diarize（离线）/ SenseVoice（SED）/ Fun-ASR-Nano | WhisperX+pyannote |
| 字级时间戳 | Paraformer + Monotonic-Aligner（纯中文官方推荐） | MFA / LLM-ForcedAligner |
| 关键词 | 字级时间戳 ASR 输出文本匹配（工程最实用） | sherpa-onnx KWS / FunASR 热词 biasing |
| 唤醒词 | openWakeWord（CPU，可自训） | sherpa-onnx KWS |
| 语音克隆 | Qwen3-TTS（3 秒，1.7B/0.6B）/ GPT-SoVITS v4（少样本微调） | CosyVoice2 |
| 情绪 TTS | IndexTTS2（12 情感+强度）路由 / CosyVoice2 instruct | Marco-Voice（权重未放，跟进） |
| 流式 TTS | Qwen3-TTS（97ms 首字符，Dual-Track） | Qwen TTS Realtime 云服务兜底 |

## 附录 B：关联记忆（astral）

- `家庭感知系统技术路线`：决策与路线主节点
- `多模态感知技术调研`：五类感知技术 + 4060/3090 硬件评估
- `ASR与TTS选型调研`：ASR/TTS 深调研
- `Marco-Voice项目评估`：情感 TTS 跟进
- `nebula B世代迁移收尾`：M1 与组合架构记录

---

*创建：2026-01 · 更新：架构定稿 v2（D13-D18）· 依据本会话全部讨论与 astral 决策节点整理*
