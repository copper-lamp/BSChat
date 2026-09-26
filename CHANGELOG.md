# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
