# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Moves both builds onto the LeviLamina 26.40 line (MC 26.40.x).

### Changed

- 开发基线从 LeviLamina `26.10.14` 升到 `26.40.6`（`xmake.lua` / `tooth.json` 两个 variant /
  README 中英双版 / building.md / getting-started.md / THIRD_PARTY_NOTICES.md 同步）。服务端与
  客户端两个 target 均在 26.40.6 下编译链接通过，零警告。
- 删除 `xmake.lua` 里对 `levilamina.rapidjson` 的 `add_requireconfs` 覆盖。26.40.6 的 SDK 自己
  就是 `add_requires("rapidjson 2025.02.05")`，被覆盖的 `v1.1.0` 钉版不再出现。删掉覆盖并
  `xmake f -c` 重新解析后仍编译通过，编译命令行里的 rapidjson 头路径不变。
- `src/client/entry/ClientEventIds.h` 扩为 `src/shared/event/EventIdBindings.h`，服务端与客户端的
  事件 ID 绑定收敛到同一份清单，新增事件监听只需登记一处。

### Fixed

- **事件 ID 绑定去掉 inline 段（双端全部事件监听）**。26.40.6 把 `ll::event` 下所有子命名空间
  （`client` / `command` / `entity` / `input` / `io` / `player` / `render` / `server` / `world`）
  都定义成 inline namespace。SDK 由 MSVC 编译、`__FUNCSIG__` 不打印 inline namespace，本模组由
  clang-cl 编译、`__PRETTY_FUNCTION__` 打印，于是同一个事件在两侧算出不同 ID，`emplaceListener`
  静默返回空监听器且不产生任何日志。原绑定带 inline 段（`ll::event::client::ClientJoinLevelEvent`、
  `ll::event::player::PlayerJoinEvent` 等），在 26.40.6 上是错的；现按 SDK 的 `LeviLamina.dll`
  实测结果统一绑定为无 inline 段的规范 ID，8 个事件逐条核对通过。
- 事件前置声明补上 `inline`。非 inline 的前置声明会被 clang 判为
  `-Winline-namespace-reopened-noninline`，且与后续头文件的定义不匹配。
- 删除 `GamePacketTransport.cpp` 里为 `ll::network::Packet::getRuntimeId` 手写的兜底实现。
  26.40.6 起该函数在 SDK 头里带 `LLNDAPI` 并随 DLL 导出（导入库中可见
  `?getRuntimeId@Packet@network@ll@@UEBA_KXZ`），26.10.14 的导入库则没有这个符号。留着这份
  重复定义会遮蔽 SDK 实现（语义与 SDK 的 `doHash(getName())` 一致），并触发
  `-Winconsistent-dllimport`。
- HUD 文本绘制适配 `RectangleArea` 变更：26.40.6 移除了
  `RectangleArea(x0, y0, x1, y1, checkForValidity)` 构造（26.10.14 里也只在 `LL_PLAT_C` 下声明），
  现在只剩 public 成员 `_x0/_x1/_y0/_y1`，改由 `HudRenderer.cpp` 的 `makeRect()` 按成员赋值，
  语义等价于旧的 `checkForValidity=false`。
- 状态图标贴图链路适配 SDK 收窄的 API：`AppPlatform::loadTexture` / `loadTextureFromStream`
  已移除，改用仍在的 `loadImage`；`TexturePtr::getClientTexture()` 已移除，改为取
  `mClientTexture->mClientTexture.get()`，并补 `BedrockTextureData.h` / `ClientTexture.h` 头。
  `drawImage` / `flushImages` 签名未变，绘制行为不变。

## [0.1.2] - 2026-10-01

Lets server operators pick the speech-to-text model instead of shipping one fixed model, and fixes
the downloader shipped alongside it.

### Added

- **STT 模型下载器** — `Install-SttModel.cmd` 交互式选型，下载并安装 sherpa-onnx 运行时与
  所选模型到 bschat 模组目录，并写入 `config/bschat.json` 的 `sttModel` 段。部署目录只分发
  这一个文件。五档模型：纯中文极速/标准/高配，中英双语，中英粤三语。全部为 sherpa-onnx 在线
  识别器的真流式模型，非 VAD 模拟流式。支持 `Install-SttModel.cmd <tier>` 静默安装。
- **`sttModel.modelType`** — 服务端不再假定 transducer。新增 `modelType` 与 `modelPath` 配置
  字段，支持 sherpa-onnx 在线识别器的全部模型族（`transducer`、`paraformer`、`zipformer2_ctc`、
  `nemo_ctc`、`t_one_ctc`）。缺省 `transducer`，旧配置无需改动；无法识别的取值回退
  `transducer` 而非静默产生空识别器。

### Changed

- 服务端加载失败时的日志会提示检查 `modelType` 与模型文件是否匹配。
- 模型许可清单移入 `THIRD_PARTY_NOTICES.md` 的可选模型小节，五档模型的许可均已向上游
  模型注册页核实为 Apache-2.0。

### Fixed

- `Install-SttModel.cmd` 部署目录此前会带出三个 `.ps1`，其中两个是开发工具。现在只分发单个
  `.cmd`，实现以内嵌 PowerShell 载荷的形式随文件携带，运行时抽到临时文件执行后删除。

## [0.1.1] - 2026-09-26

Pins the v1 wire protocol as a stable, append-only contract, and makes capability negotiation and
handshake admission explicit on both ends. Also adds diagnostics that attribute dropped uplink frames.

### Added

- **Protocol compatibility contract** — the v1 envelope and existing payloads are now pinned by
  byte-for-byte fixtures for `Hello` and `Welcome`. Evolution is append-only: new message types, or
  optional payload tails that also raise the declared payload length. Envelope-level trailing bytes are
  rejected.
- **Capability negotiation** — the client declares capabilities in `Hello`; the server intersects them
  with what it supports (PTT is the base capability, subtitles follow STT availability) and returns the
  result in `Welcome.serverCapabilities`. Only negotiated options are enabled, and no new wire field was
  needed because the byte already existed in 0.1.0.
- **Handshake admission** — business messages are dropped on both ends before the handshake completes,
  and a stale or duplicate `Welcome` no longer flips the client state. Unregistered senders are rejected
  by the server.
- **Per-receiver downlink filtering** — subtitles are only delivered to sessions that negotiated the
  subtitle capability.

### Changed

- **Uplink diagnostics** — the server logs a speech summary per push-to-talk burst: duration, received
  and accepted frames, and drops split into rate-limited, late, duplicate, buffer-full and evicted.
  Repeating rejection logs are throttled instead of printed per frame.
- **Jitter buffer reporting** — pushing a frame now reports whether it was accepted, accepted with
  eviction, late, duplicate or rejected because the buffer was full, instead of a silent boolean, so
  every dropped frame is attributable.

### Known limitations

- The limitations listed for 0.1.0 still apply.
- The old/new interoperability combinations are covered by fixtures and unit tests only; they have not
  been exercised on real game binaries across two mod versions yet.

## [0.1.0] - 2026-09-26

First development release: the core voice engine, the wire protocol and both adaptation layers (the
server plugin and the client mod) are implemented, covered by a host unit test target for the core.

### Added

- **Core engine** (`bschat-core`) — Opus codec wrapper, protocol envelopes, audio frame types, jitter
  buffer, WAV reading, per-receiver mixing core, spatial policy and the JSON configuration model.
- **Server voice** — a dedicated mixing thread on a 120 ms tick that mixes per receiver and keeps the
  sender's own voice out of their mix; no downlink traffic while nobody speaks; bounded pending queue.
- **Client audio** — WASAPI shared-mode capture and playback, automatic gain control, Opus uplink and
  jitter-buffered playback of the mixed downlink.
- **Push-to-talk** — hold **V** (configurable) to talk; **J** (configurable) opens the voice settings
  panel.
- **HUD** — status icons plus text and on-screen subtitles drawn natively; the status PNGs are decoded
  at runtime and uploaded to the engine texture group, so no resource pack is needed.
- **Native form panels** — the client settings panel is relayed by the server as a native form, and the
  operator admin panel is built and sent by the server.
- **Server commands** — `/bsc test`, `/bsc play <file>`, `/bsc stop`, `/bsc admin`.
- **Speech to text** — optional server-side sherpa-onnx streaming Zipformer transcription delivered as
  subtitles; a missing model disables subtitles alone and leaves voice untouched.
- **In-game self-check** — `/bsc test` runs a handshake / uplink / downlink check on the requesting
  client and writes the result to the client log.
- **Internationalization** — both the server and the client ship `en_US` and `zh_CN`.
- **Build and release** — one repository produces the server plugin and the client mod through
  `--target_type`; CI builds both flavors and the release workflow publishes two Windows x64 archives.
- **Unit tests** — the host target `bschat-tests` covers the codec, protocol, mixing, spatial policy,
  sessions, speech-to-text queueing, WAV parsing, configuration and panels.

### Known limitations

- Test release: configuration and protocol may still change between versions.
- Subtitles show the text only, without the speaker's name, and the HUD layout is not yet calibrated for
  every GUI scale.
- No proximity/distance attenuation, no environmental reverb and no multi-room channels yet; VAD is
  wired but off by default.
- The admin panel exposes the server voice switch only.
