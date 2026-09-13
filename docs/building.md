# Building Betterlanguagechat

This guide explains how to build Betterlanguagechat from source with xmake.

## Prerequisites

- Windows x64
- [xmake](https://xmake.io/)
- A Clang / Clang-CL toolchain configured for xmake

> [!NOTE]
> The build pulls LeviLamina SDK packages automatically. The repository compiles into a
> server build or a client build depending on the `--target_type` option (default:
> `server`).

## Build the module

Configure and build the **server** version (default):

```bash
xmake f -y -p windows -a x64 -m release
xmake build voicechat
```

Build the **client** version:

```bash
xmake f -y -p windows -a x64 -m release --target_type=client
xmake build voicechat
```

## Run unit tests

The core engine has zero LeviLamina dependency and runs as a host binary:

```bash
xmake f -y -p windows -a x64 -m release
xmake build voicechat-tests
xmake run voicechat-tests
```

The `voicechat-tests` target is `set_default(false)`, so it is not built by default.

## Targets

| Target | Kind | Description |
|---|---|---|
| `voicechat-core` | static library | Pure C++ core engine (codec / protocol / audio / pipeline / config) |
| `voicechat` | module | Main module; picks `src/server` or `src/client` by `target_type` |
| `voicechat-tests` | binary | Core unit tests (host, no LeviLamina) |

## Deployment

After a successful build, place the output in the correct directory:

| Target build type | Deploy to |
|---|---|
| `server` | `plugins/voicechat/` |
| `client` | `mods/voicechat/` |

## Dependencies

| Dependency | Purpose |
|---|---|
| LeviLamina `26.40.*` | Mod loader SDK (server or client flavor) |
| Opus (`libopus` v1.5.2) | Audio encode/decode (static link) |
| nlohmann-json | JSON configuration parsing (header-only) |
| whisper.cpp | Server-side speech-to-text (server build) |

## Project layout

```
docs/            Module documentation (design, getting started, building)
src/core/        Pure C++ core engine (independent of LeviLamina)
src/shared/      LeviLamina glue shared by both builds
src/server/      Server adaptation layer (server target)
src/client/      Client adaptation layer (client target)
tests/           Core unit tests
```

See the design document for details:
[docs/voicechat-v1-design.md](voicechat-v1-design.md).