# 语音聊天模组 v1 设计文档

> 状态：已确认设计，待实现
> 日期：2026-09-13
> 适用范围：LeviLamina 双端模组（服务端 BDS / 客户端 Windows 基岩版）

## 一、需求

### 1.1 概述

本模组为 LeviLamina 生态提供"游戏内实时语音聊天"。同一仓库、同一份协议，编译时按 `target_type` 产出服务端（BDS 插件）与客户端（客户端模组）两个版本。玩家在游戏内按住按键说话，语音经压缩后通过游戏内自定义数据包上行到服务端；服务端把多方语音混音（v1 为全局混音），可选进行本地语音转文字，再将混合音频流与转写文字打包回传给客户端；客户端边收边播并在 HUD 上显示转写字幕。

### 1.2 目标规模

- 中型社区服：同时在线 30–100 人，同一时刻活跃说话者 4–10 人。
- 下行仅在活跃发言时推流，安静时带宽趋近于 0。

### 1.3 功能需求

| 编号 | 需求 | 说明 |
|---|---|---|
| FR-1 | 客户端麦克风采集 | 客户端模组内直接调用 WASAPI 采集，48kHz mono |
| FR-2 | 按键说话（PTT） | 客户端绑定按键（默认 V），按住说话松开结束；可改键 |
| FR-3 | 语音上行 | Opus 低码率高压缩，经游戏内自定义数据包上行 |
| FR-4 | 服务端全局混音 | 解码所有活跃说话者，归一化混音为单路混合流 |
| FR-5 | 语音下行 | 混合流经游戏包回推全部在线客户端，静音不推流 |
| FR-6 | 可选语音转文字 | 服务端本地 Whisper 离线转写，可配置开关 |
| FR-7 | HUD 字幕 | 客户端渲染转写文字（说话者 + 文字 + 淡出） |
| FR-8 | 会话管理 | 玩家加入/离开自动创建/清理语音会话 |
| FR-9 | 配置 | JSON 配置：语音总开关、STT 开关、模型路径、码率档位、按键 |

### 1.4 非功能需求

| 编号 | 需求 | 说明 |
|---|---|---|
| NFR-1 | 延迟 | 端到端语音延迟目标 ≤ 300ms（含游戏网络层抖动缓冲） |
| NFR-2 | 带宽 | 上行 20kbps/活跃说话者；下行活跃时约 20kbps/人，安静时≈0 |
| NFR-3 | 健壮性 | 音频线程崩溃自动重启；STT 故障不影响语音主链路；语音故障不影响游戏本体 |
| NFR-4 | 可扩展性 | 协议、混音、传输、VAD、STT 全部接口化，后续扩展不改既有链路 |
| NFR-5 | 可测试性 | 核心引擎零 LeviLamina 依赖，可纯 host 单测 |
| NFR-6 | 国际化 | 界面/提示文案接入 LeviLamina i18n 模块 |

### 1.5 范围界定（v1 明确不做）

- 不做位置/距离衰减混音、环境混响混音（接口预留，后续版本实现）。
- 不做自动语音检测 VAD（接口预留，默认关闭）。
- 不做 UDP 独立通道（传输接口预留实现）。
- 不做移动端/伴侣应用采集。
- 不做多人房间/频道体系（v1 全体玩家同一房间）。

## 二、架构

### 2.1 分层与目录

沿用 LeviLamina 官方 `src` / `src-server` / `src-client` 约定：

```
Betterlanguagechat/
├── xmake.lua                  # voicechat-core(静态库) + voicechat(模组，按 target_type 切)
├── tooth.json / manifest.json
├── src/
│   ├── core/                  # 核心引擎：纯 C++，零 LeviLamina 依赖
│   │   ├── codec/             #   Opus 编解码封装（编码参数：48k/mono/60ms/低码率/DTX/complexity10）
│   │   ├── protocol/          #   信封 + 消息定义 + 序列化 + MessageRegistry
│   │   ├── audio/             #   音频帧、采样率/声道规整、JitterBuffer
│   │   ├── pipeline/          #   ITransport / IMixer / IVad / IStt 接口
│   │   └── config/            #   配置模型（JSON 序列化）
│   ├── shared/                # 双端共享 LeviLamina 胶水：日志、自定义包注册 helper、i18n 键
│   ├── server/                # 服务端适配层（server 目标编译）
│   │   ├── session/           #   SessionManager / PlayerSession
│   │   ├── mixer/             #   GlobalMixer 实现 IMixer
│   │   ├── stt/               #   WhisperStt 实现 IStt（whisper.cpp）
│   │   └── entry/             #   模组生命周期装配
│   └── client/                # 客户端适配层（client 目标编译）
│       ├── audio/             #   WASAPI CaptureEngine / PlaybackEngine
│       ├── input/             #   PTT 按键绑定
│       ├── hud/               #   SubtitleOverlay（渲染事件绘制字幕）
│       └── entry/             #   模组生命周期装配
├── tests/                     # core 单元测试（纯 host 运行）
└── docs/                      # 模块文档（需求/架构/备注）
```

### 2.2 构建目标

- `voicechat-core`：静态库，`src/core/**.cpp`，依赖 Opus（`add_requires` 静态包）。
- `voicechat`：模组 dll。`src/shared` + 按 `target_type` 选择 `src/server` 或 `src/client`。
- 服务端目标链接 whisper.cpp（模型文件随模组分发，不塞源码进仓库，具体形态见 3.2 风险点）。
- 发布：`target_type=server` → `plugins/voicechat/`；`target_type=client` → `mods/voicechat/`。

### 2.3 外部依赖（v1 锁定）

| 依赖 | 用途 | 形态 |
|---|---|---|
| Opus | 语音编解码 | 静态链接（xmake 包） |
| whisper.cpp | 服务端本地转写 | 服务端目标链接，模型文件随模组分发 |
| nlohmann-json | 配置序列化（header-only，xmake 包） | 静态链接 |

### 2.4 核心引擎（core）关键设计

#### 2.4.1 消息信封（protocol）

```
[magic:2B][ver:1B][type:1B][seq:4B][timestamp:8B][payload_len:2B][payload]
```

- 手写紧凑二进制序列化，固定 little-endian，零第三方依赖。
- `ver` 协议版本；`type` 注册于 `MessageRegistry`，**未知类型一律忽略**（前后兼容机制）。
- 版本不匹配在握手阶段降级或拒绝（可配置）。

#### 2.4.2 消息集（v1）

| 方向 | 类型 | 内容 |
|---|---|---|
| C→S | `Hello` | 玩家 UUID、协议版本、采集参数（48k/mono/60ms）、能力声明（PTT/VAD/字幕） |
| C→S | `AudioData` | Opus 帧 + seq + 说话标志（开始/持续/结束） |
| C→S | `Control` | PTT 按下/松开、本端静音、VAD 开关 |
| S→C | `Welcome` | 握手确认、协商后帧长/采样率、STT 是否启用、服务器能力 |
| S→C | `MixStream` | 混合后 Opus 帧（含 seq，供客户端抖动缓冲） |
| S→C | `SttText` | 说话者 + 文字 + 部分/最终结果标志 |
| S→C | `Control` | 强制静音、状态同步 |

#### 2.4.3 传输抽象（pipeline）

```
interface ITransport {
    void send(peerId, message);
    void onMessage(callback);   // 包 → MessageRegistry → 分发
}
```

- v1 实现 `GamePacketTransport`：`src/shared` 注册 LeviLamina 自定义包（双端注册同一组包 ID），包体即信封字节。
- 预留 `UdpTransport`（后续版本）。上层业务感知不到传输替换。

#### 2.4.4 编码参数（落实"压缩力度大，不塞满数据包"）

- Opus 48kHz mono，60ms/帧，VBR，目标码率默认 **20kbps**（可配 12/16/20/24），complexity 10，DTX 常开。
- 单帧约 100–200 字节；数据包稀疏、不拼包、不填充。
- 下行 `MixStream` 每包聚合 2 帧（120ms），仅活跃发言时推送。

### 2.5 服务端适配层

#### 2.5.1 会话管理（session）

- `SessionManager` 挂接玩家加入/离开事件，玩家 UUID 为键，创建/销毁 `PlayerSession`。
- `PlayerSession`：每发送者一个 Opus 解码器 + 抖动缓冲；按 PTT 开始/结束切分话语 PCM 段供 STT；会话配置快照。

#### 2.5.2 混音器（mixer）

- `GlobalMixer` 实现 core `IMixer`：
  - 独立音频线程，120ms tick，不阻塞 BDS 主线程。
  - 每 tick：取各会话抖动缓冲帧 → 解码 → 归一化/AGC 混音 → 编码低码率 Opus 混合帧 → transport 推送 `MixStream`。
  - 无活跃发言 → 整 tick 不推流。
- 线程安全：音频线程写"待发队列"；游戏主线程只做会话增删与包接收（无锁队列入各会话抖动缓冲）。

#### 2.5.3 STT（stt）

- `WhisperStt` 实现 core `IStt`（whisper.cpp 本地推理）。
- 管线：话语切分完成 → PCM 段入 STT 队列 → 独立 worker 线程推理 → 结果经主线程广播 `SttText`（部分/最终结果）。
- 并发：单 worker + 优先级队列；仅转写启用了字幕的会话。
- 降级：模型缺失/加载失败/推理失败 → 记日志并停用该会话 STT，语音主链路不受影响。

#### 2.5.4 装配（entry）

- 生命周期：读配置 → 初始化 transport（注册包）→ 装配 SessionManager/Mixer/Stt → 启动音频线程。
- 配置（JSON）：语音总开关、STT 开关、Whisper 模型路径、码率档位（12/16/20/24kbps）、帧长。

### 2.6 客户端适配层

#### 2.6.1 采集引擎（audio/CaptureEngine）

- WASAPI 共享模式采集默认麦克风，48kHz mono float，实时优先级采集线程。
- 60ms 切帧 → core 编码器压 Opus（低码率 + DTX + complexity 10）。
- 状态机 `Idle → Speaking(PTT) → Idle`；松开发尾帧（结束标志）。
- 采集失败 → 静默降级 + HUD 提示一次。

#### 2.6.2 PTT 按键（input）

- LeviLamina 客户端 Input 模块绑定按键（默认 V），可配置改键。
- `IVad` 接口就位但默认关闭；未来开 VAD 只换触发源，采集/编码零改动。

#### 2.6.3 播放引擎（audio/PlaybackEngine）

- WASAPI 共享模式渲染；解码 `MixStream` 混合帧播放。
- 客户端抖动缓冲（排序/去抖，深度 60–120ms）。
- 静音/无流 tick 不推帧 → 播放引擎静默，不消耗 CPU。

#### 2.6.4 HUD 字幕（hud/SubtitleOverlay）

- 监听 `SttText` → 维护最近若干条（说话者 + 文字 + 淡出计时），渲染事件绘制屏幕文字，文案走 i18n。
- 展示接口 `pushSubtitle()` 与渲染实现解耦，实现可替换。

#### 2.6.5 装配与状态机（entry）

- 生命周期：读配置 → 注册按键 → 初始化 transport → 挂采集/播放/渲染事件。
- 状态机：`Disconnected → Handshaking → Ready`；世界退出回 `Disconnected`；握手失败 5s 重试。
- 时序约束：进入世界、本地玩家可用后发 `Hello`。

### 2.7 端到端数据流

```
A/B 进世界 → 各发 Hello（UUID/参数/能力）
服务端 → Welcome（协商完成，进入 Ready）

A 按住 V → 采集线程出帧 → 编码 → AudioData(开始标志) 上行
服务端包线程 → push 进 A 会话抖动缓冲
音频线程每 120ms：解码 A（及其它活跃者）帧 → 全局混音 → 编码混合帧
            → MixStream 推给 A、B
B 客户端：抖动缓冲 → 解码 → 播放

A 松开 V → 尾帧(结束标志)
服务端：把 A 这段话语的 PCM 段提交 STT 队列
Whisper worker → 推理 → 结果回主线程 → SttText(A + 文字) 广播
A/B HUD 显示字幕

A 离开 → 服务端清理会话 + 取消其 STT 队列任务
```

### 2.8 错误处理

| 场景（服务端） | 行为 |
|---|---|
| 玩家离开/断开 | 清理会话、抖动缓冲、取消 STT 任务 |
| 协议版本不匹配 | 拒绝该会话，日志提示升级 |
| STT 模型加载失败 | 全局停用 STT，语音主链路不受影响 |
| 恶意灌包/超量语音包 | 会话级速率限制，超限丢弃 + 警告 |
| 音频线程崩溃 | 有界重试自动重启，服务进程不崩 |
| 无活跃发言 | 不推流（带宽≈0） |

| 场景（客户端） | 行为 |
|---|---|
| 无麦克风/设备占用 | 静默 + HUD 提示一次 |
| 握手失败/超时 | 5s 重试，日志记录 |
| 世界退出/重进 | 会话清理，重进后重新 Hello |
| 传输不可用 | 语音自动停，不阻塞游戏 |

### 2.9 扩展点（后续版本）

| 扩展 | 落点 | 方式 |
|---|---|---|
| 环境混音（回声/混响/环境音） | `IMixer` | 新增 `EnvironmentMixer` 实现 |
| 位置/距离混音（近聊/耳语） | `IMixer` + 位置消息 | 新消息类型 + 新 Mixer 实现 |
| UDP 独立通道 | `ITransport` | 新增 `UdpTransport` 实现 |
| 自动语音检测 VAD | `IVad` | 新增 VAD 实现，采集触发源替换 |
| 移动端/伴侣应用 | `ITransport` + 采集接口 | 伴侣应用走 UdpTransport，游戏模组本地转发 |
| 多人房间/频道 | 协议消息 | 新消息类型 + SessionManager 房间模型 |

## 三、备注

### 3.1 开发约定

- 文档按 AGENTS.md：每个模块一份，含 需求/架构/备注 三章节；开发完成后回填实际实现、风险点、TODO。
- i18n：所有面向玩家的文案走 i18n 键（LeviLamina i18n 模块，Common 范围）。
- 提交：头英文、内容中文；阶段测试通过后提交。
- 禁止 emoji，UI 图标用图标包。

### 3.2 风险点

1. **客户端 HUD 文字渲染 API 待验证**：LeviLamina 客户端渲染屏幕文字有两类候选（渲染事件 + 原生 2D 绘制 / OreUI 或 DDUI 屏幕会话）。实现阶段先做可行性 spike，选最简可靠路径；`SubtitleOverlay` 接口已解耦，替换实现不影响架构。
2. **游戏内自定义包承载实时音频的实测效果**：BDS 网络层延迟与吞吐特性需在测试 BDS 上实测。若延迟超标，退路是 `UdpTransport`（架构已预留）。
3. **whisper.cpp 集成形态**：模型体积（base 约 140MB）与服务端 CPU 占用需实测；超限时可降级到只转写"最近发言者"或全局混音段。
4. **WASAPI 设备兼容性**：共享模式下设备被占用/采样率协商失败等边界，需要真机覆盖。

### 3.3 测试策略

1. core 单元测试（纯 host，无 LeviLamina 依赖）：
   - codec：Opus 编解码 roundtrip（静音/正弦波/语音样本）、码率档位验证。
   - protocol：信封序列化/反序列化、未知类型忽略、版本不匹配行为。
   - audio：抖动缓冲排序、丢包容忍。
   - mixer：GlobalMixer 混音正确性（归一化、静音检测、多路叠加）。
2. 集成冒烟（本地测试 BDS + 测试客户端）：握手、PTT 帧上行、混合流下行、字幕链路，脚本化断言。
3. 手工验收：双真机互聊——音量、延迟、字幕显示、退出/重进、STT 开关切换。

### 3.4 待办清单（实现阶段分解）

- [ ] 初始化 git 仓库与构建骨架（双 target）
- [ ] core：codec / protocol / audio / pipeline / config
- [ ] core 单元测试
- [ ] server 适配层（session / mixer / stt / entry）
- [ ] client 适配层（capture / playback / input / hud / entry）
- [ ] HUD 渲染可行性 spike
- [ ] 集成冒烟 + 手工验收
