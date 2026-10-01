
<div align="center">
  <h1>Better Speech Chat</h1>
  <p><strong>Join the world and Start talking.</strong></p>
  <p>An open-source in-game real-time voice chat mod for Minecraft Bedrock on LeviLamina — server-side mixing, client-side capture and playback, with optional live speech-to-text subtitles.</p>
  <p>
    <img src="https://img.shields.io/badge/release-v0.1.3--mc26.20-4c8bf5?style=flat-square" alt="BSChat v0.1.3-mc26.20">
    <img src="https://img.shields.io/badge/Minecraft%20Bedrock-Windows%20x64-62b47a?style=flat-square" alt="Windows x64 Minecraft Bedrock">
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue?style=flat-square" alt="AGPL-3.0 License"></a>
  </p>
  <p>
    <img src="https://img.shields.io/badge/LeviLamina-26.20.7-7b68ee?style=flat-square" alt="LeviLamina 26.20.7">
  </p>
  <p>
    <a href="https://github.com/copper-lamp/BSChat/releases">Releases</a>
    ·
    <a href="CHANGELOG.md">Changelog</a>
    ·
    <a href="https://github.com/copper-lamp/BSChat/issues">Issues</a>
    ·
    <a href="README_ZH.md">简体中文</a>
  </p>
  <p>
    <a href="https://qm.qq.com/q/861900673"><img src="https://img.shields.io/badge/QQ-861900673-EA0000?style=for-the-badge&amp;logo=qq&amp;logoColor=white" alt="Join the BSChat QQ group"></a>
  </p>
</div>

> [!WARNING]
> BSChat is still in early development, and all current builds are test
> builds. Please back up your important worlds and client/server data. The voice protocol is pinned to
> **v1** and only ever extended in a backwards-compatible way, so different mod patch versions can still
> talk to each other. A Minecraft or LeviLamina version change is not guaranteed to stay compatible —
> that depends on the game's network layer and mod loading, which are outside this project's control.

Better Speech Chat brings your voice into the game 

hold a key to talk, and your audio
travels over the game's built-in data channel to everyone on the server in real time; the
server mixes all active speakers and streams the result back. You can also enable live
speech-to-text so subtitles show on screen who said what. One project, two builds: a
server plugin and a client mod, delivering a consistent experience from server to player.

## Quick Start

> [!IMPORTANT]
> BSChat ships as two independent builds — server and client. Both must be
> installed and running on the same LeviLamina baseline, or the handshake will fail.

1. Install [LeviLamina](https://lamina.levimc.org/) on your server (baseline 26.20.7).
2. Install LeviLamina client on each player's Bedrock client.
3. Install the matching build: server → `plugins/bschat/`, client → `mods/bschat/`.
4. Restart, join the world, and **hold V** to talk; release to go quiet. Press **J** to open the voice
   settings panel.

Full installation and configuration steps are in the [Getting Started guide](../docs/getting-started.md).

## Features

- **Real-time voice** — talk the moment you join, near-zero latency, even with many
  speakers at once.
- **Push-to-talk (PTT)** — hold **V** to speak, release to mute; full control over your
  microphone.
- **Bandwidth efficient** — heavily compressed audio, nearly zero traffic when nobody is
  talking.
- **No extra ports** — voice reuses the game's built-in connection, no player-side setup.
- **Live subtitles** — server-side offline transcription shown as on-screen subtitles, so
  nothing is missed.
- **Server and client builds** — one project, two builds, ready to deploy.
- **Graceful degradation** — microphone, model, or network issues isolate themselves and
  never take down the game.
- **Internationalization** — player-facing text goes through the LeviLamina i18n system.

## This Release

`v0.1.3` moves both builds onto the **LeviLamina 26.20 line** (SDK `26.20.7`). Voice behaviour,
the v1 wire protocol and the configuration format are unchanged — this release only moves the host
SDK. `v0.1.2` hands speech-to-text model selection to the server operator. The server package now ships
`Install-SttModel.cmd`; run it, pick a tier, and it downloads the sherpa-onnx runtime plus the chosen
model and writes the resulting configuration. Five tiers are offered — Chinese at three size/accuracy
points, plus Chinese-English and Chinese-English-Cantonese — all true streaming models from
sherpa-onnx's online recognizer rather than VAD-simulated streaming. The server no longer hardcodes
the transducer family: a new `sttModel.modelType` field selects any online model family, and existing
configurations keep working unchanged. `v0.1.1` pinned the voice protocol to **v1** as a stable,
append-only contract and completed capability negotiation on both ends: the client declares what it
supports, the server answers with the common subset, and only negotiated features are enabled — so
newer and older builds keep talking to each other, with base voice preserved whenever an optional
feature is unavailable. Full history is in the [Changelog](CHANGELOG.md).

> [!IMPORTANT]
> This is still a test release and configuration or new features may still change, but the voice
> protocol is pinned to v1 and only grows in a backwards-compatible way. Please keep an eye on the
> changelog after use.

## Compatibility

| End | Build | Install path |
| ------------------ | ---------------- | ------------------------ |
| Server (BDS) | Server build | `plugins/bschat/` |
| Client (Bedrock) | Client build | `mods/bschat/` |

Both are built for **Windows x64** and run on **LeviLamina 26.20.7**.

> [!IMPORTANT]
> Releases are cut per MC version line, and the tag suffix states which line a build targets
> (`-mc26.20` here). A build for another line will not load — pick the build whose suffix matches
> the MC version your players run, and give server and client the same line.

> [!TIP]
> No separate port or UDP channel is required — voice reuses the game's built-in
> connection.

## Build From Source

Build on Windows x64 with xmake; use `--target_type` to produce the server or client
build. Commands, output layout, and dependencies are in the [Building guide](../docs/building.md).

## Commands

| Command | Description |
| -------------------------------- | ---------------------------------- |
| `/bsc test` | Run the end-to-end self-check on your client; the result goes to its log. |
| `/bsc play <file>` | Play a WAV file through the voice downlink (files under `<server config>/audio/`, or an absolute path). |
| `/bsc stop` | Stop the running WAV playback. |
| `/bsc admin` | Open the admin panel; operators only. |

The following commands are planned and will ship as development proceeds:

| Command | Description |
| -------------------------------- | ---------------------------------- |
| `/bsc help` | Show voice chat command help. |
| `/bsc mute <player>` | Force-mute a player. |
| `/bsc unmute <player>` | Unmute a player. |
| `/bsc toggle` | Toggle your own microphone. |

## Languages

- Server: English, 简体中文
- Client: English, 简体中文

Player-facing text is wired through the LeviLamina i18n system and ships with both builds.

## Frequently Asked Questions

### What is BSChat?
It is an in-game real-time voice chat mod for Minecraft Bedrock on Windows x64 LeviLamina.
Players hold a key to talk; audio goes up over the game's built-in data channel, is mixed
server-side, and streams back to all online players, with optional live speech-to-text
subtitles.

### Do I need to open ports or configure a UDP channel?
No. Voice uses the game's built-in data channel, with no extra network configuration on
either side.

### Does talk use bandwidth when nobody is speaking?
Almost none. Audio is heavily compressed, so idle moments cost near-zero downstream data.

### How do I enable subtitles?
Subtitles are transcribed by the **server** from a speech-to-text model. That model is **not bundled**
— download it with the installer shipped in the server package. The step is optional; voice chat works
without it.

1. On the server, open a command prompt **in the same folder as `bschat.dll`** (usually
   `plugins/bschat/`) and run:

   ```bat
   Install-SttModel.cmd
   ```

2. A menu of model tiers appears. Type a number and press Enter:

   | Your situation | Pick |
   | --- | --- |
   | Players mostly speak Chinese | **2** |
   | Players mix Chinese and English | **4** |
   | Cantonese speakers | **5** |
   | Low CPU or limited bandwidth | **1** |
   | Chinese only, accuracy first, strong CPU | **3** |

   The download is 25–570 MB and shows progress. When it finishes:

   ```
   安装完成。
   请重启服务端使配置生效，并确认客户端已开启字幕。
   ```

   To skip the menu, pass the tier id, for example `Install-SttModel.cmd bilingual-zh-en`.

3. **Restart the server.** The model loads at startup.
4. In the client, press **J** and make sure subtitles are on.

The model, runtime and configuration all land inside the mod folder; nothing else on the server is
touched. Re-run the script to switch tiers. Deleting `stt/` is harmless — it only holds
re-downloadable weights.

Full walkthrough, including troubleshooting, is in
[docs/getting-started.md](../docs/getting-started.md).

### Can I change the talk key?
Yes — press **J** in game to open the voice settings panel and bind another key, or edit `pttKey` and
`settingsKey` in the client `config.json`.

## Development Status and Roadmap

- The core voice engine, protocol, both adaptation layers and the unit tests are implemented; real
  device tuning of latency and audio quality is the next step.
- Planned later: proximity/distance audio, environmental reverb, VAD voice detection, and
  multi-room channels.

> [!TIP]
> **Testing focus:** when reporting latency, artifacts, or subtitle issues, please include
> both build versions, LeviLamina versions, logs, and your microphone/network environment.

## Known Limitations

- No stable release yet; configuration and new features may still change, but the v1 voice protocol
  only ever grows in a backwards-compatible way.
- Subtitles show the text only, without the speaker's name.
- The admin panel exposes the server voice switch only.
- No proximity/distance attenuation or environmental reverb yet (interfaces reserved for
  later versions).
- VAD automatic voice detection is wired but off by default.
- No standalone UDP channel and no multi-room/channel system yet.
- Mobile/companion capture is not yet supported.
- Compatibility must be re-verified after Minecraft or LeviLamina updates.

To report a reproducible issue, please [open an Issue](https://github.com/copper-lamp/BSChat/issues).

## Contributing

Contributions are welcome — ask questions via issues and open pull requests. 

Join the community on QQ group **861900673**.

## Acknowledgements

Special thanks to the maintainers and community of [LeviLamina](https://github.com/LiteLDev/LeviLamina) for the native
mod development platform and tooling; and to [Opus](https://opus-codec.org/),
[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx), [ONNX Runtime](https://github.com/microsoft/onnxruntime), and
[nlohmann/json](https://github.com/nlohmann/json) for providing audio codec, offline
transcription, and JSON parsing capabilities.

## License

BSChat is free software released under the
[GNU Affero General Public License v3.0](LICENSE) (or any later version). See
`LICENSE` for details. Third-party dependencies remain under their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the
[licenses/](licenses/) directory.
