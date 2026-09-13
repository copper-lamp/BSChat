
<div align="center">

# Better Language Chat

**Real-time, in-game voice chat for Minecraft Bedrock.**

Talk to your teammates the moment you join the world 

no typing, no separate apps, no
extra ports. Voice streams over the game's own data channel with low latency and near-zero
bandwidth when nobody is speaking, with optional live speech-to-text subtitles.

[![LeviLamina](https://img.shields.io/badge/LeviLamina-26.40.x-blue)](https://github.com/LiteLDev/LeviLamina)
[![Platform](https://img.shields.io/badge/Platform-Server%20%2F%20Client-success)](https://github.com/LiteLDev/LeviLamina)
[![Status](https://img.shields.io/badge/Status-Alpha-orange)](CHANGELOG.md)
[![License CC0-1.0](https://img.shields.io/badge/License-CC0--1.0-lightgrey)](LICENSE)
[![中文](README_ZH.md)](README_ZH.md)

</div>

> [!IMPORTANT]
> Betterlanguagechat is currently in **alpha**. It is stable enough to try on friendly
> servers, but features and behavior may still change. Keep a backup before experimenting,
> and back up your client/server before installing.

## Overview

Betterlanguagechat brings live voice chat to Minecraft Bedrock as a LeviLamina module.
One project ships two builds — a **server** plugin and a **client** mod — so players can
press a key, talk, and be heard by everyone on the server in real time.

The goal is simple: **join a world, hold a key, start talking.**

## Features

- **Real-time communication** — near-instant voice delivery to every online player.
- **Push-to-talk** — hold **V** to speak, release to go quiet. Instant privacy.
- **Efficient by design** — audio is heavily compressed, and quiet moments cost almost no
  bandwidth.
- **No extra ports** — voice uses the game's own data channel. No port-forwarding, no
  separate tooling on the player side.
- **Optional live subtitles** — server-side speech-to-text turns what's said into
  on-screen text, so nothing is missed.
- **Server and client builds** — one consistent experience from the server to every player.

## Installation

Betterlanguagechat is distributed as **two separate builds**. Both ends must be installed
for voice to work.

| Build | Install to | Notes |
|---|---|---|
| Server | `plugins/voicechat/` | Run on your Bedrock Dedicated Server |
| Client | `mods/voicechat/` | Run on each player's Bedrock client |

1. Make sure both server and client run [LeviLamina](https://lamina.levimc.org/)
   (baseline 26.40.x).
2. Install the **server** build on the BDS server.
3. Install the **client** build on each player's Bedrock client.
4. Restart everything, then join the world.

> [!TIP]
> If you can't hear anyone, first confirm your microphone is available to your client, and
> verify you installed the correct side (server vs. client) on each end.

## Usage

In-game, hold **V** to talk and release it to go quiet.

| Action | Input |
|---|---|
| Push-to-talk | Hold **V** |
| Speak | While holding **V** |
| Stop speaking | Release **V** |

Additional options (chat commands, mute controls, subtitle toggle) are configured through
the module settings in `config.json`.

## Configuration

Player-facing settings live in the module's `config.json`.

**Client:**

| Key | Default | Description |
|---|---|---|
| `pttKey` | `V` | Key held to talk |
| `subtitleEnabled` | `true` | Show speech-to-text subtitles |
| `vadEnabled` | `false` | Automatic voice detection (reserved) |

**Server:**

| Key | Default | Description |
|---|---|---|
| `voiceEnabled` | `true` | Master voice switch |
| `sttEnabled` | `false` | Enable speech-to-text subtitles |
| `whisperModelPath` | *(empty)* | Whisper model file for subtitles |

## Compatibility

| Aspect | Value |
|---|---|
| Loader | LeviLamina `26.40.*` |
| Platform | Windows x64 |
| Server install | `plugins/voicechat/` |
| Client install | `mods/voicechat/` |

## Frequently Asked Questions

### Why can't I hear anyone?
First verify both the server and client builds are installed on the correct side, and that
your microphone isn't being used by another program.

### Do I need to open any ports?
No. Voice travels over the game's built-in data channel.

### Does talk affect my bandwidth?
Very little. When nobody is speaking, the module sends almost no data at all.

### Can I change the talk key?
Yes — set a different key in `pttKey` in the client `config.json`.

## Project Status

Betterlanguagechat is in active **alpha** development.

- Core voice engine, protocol, and unit tests are in place.
- Server and client adaptation layers are being built next.
- See the roadmap in [docs](../docs/) and history in [CHANGELOG.md](CHANGELOG.md).

## Reporting Issues

Found a bug or have a suggestion? Open an issue or submit a pull request — contributions
are welcome.

## License

This project is open source under the [CC0-1.0](LICENSE) license. See `LICENSE` for details.
