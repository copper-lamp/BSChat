# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Moves both builds onto the LeviLamina 26.20 line.

### Changed

- 开发基线从 LeviLamina `26.10.14` 升到 `26.20.7`（`xmake.lua` / `tooth.json` 两个 variant /
  README / building.md / getting-started.md / THIRD_PARTY_NOTICES.md 同步）。服务端与客户端两个
  target 均在 26.20.7 下编译链接通过，无缺失符号。

### Fixed

- `ClientEventIds.h` 的事件前置声明补上 `inline`。26.20.7 的 SDK 头把 `ll::event::client` /
  `world` / `input` / `render` 改成了 inline namespace，非 inline 的前置声明会被 clang 判为
  `-Winline-namespace-reopened-noninline`，且与后续头文件的定义不匹配。

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
