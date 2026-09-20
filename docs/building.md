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

The core engine and host-test adapters have zero LeviLamina dependency and run as a host binary:

```bash
xmake f -y -p windows -a x64 -m release
xmake build voicechat-tests
xmake run voicechat-tests
```

The `voicechat-tests` target is `set_default(false)`, so it is not built by default. If xmake reports a stale package lock, stop other xmake processes and remove the matching `package.lock` under `%LOCALAPPDATA%\\.xmake\\cache\\packages`; do not delete a lock held by an active build. The current machine has the ATL headers and `atls.lib`; configure through the Visual Studio Developer Command Prompt so both `INCLUDE` and `LIB` contain the `atlmfc` directories. LeviLamina's current package build reached the link step but the plain PowerShell environment failed to pass the ATL library path (`LNK1104 atls.lib`).

## Targets

| Target | Kind | Description |
|---|---|---|
| `voicechat-core` | static library | Pure C++ core engine (codec / protocol / audio / pipeline / config) |
| `voicechat` | module | Main module; picks `src/server` or `src/client` by `target_type` |
| `voicechat-tests` | binary | Core and host-test adapters (no LeviLamina) |

## Deployment

After a successful build, place the output in the correct directory:

| Target build type | Deploy to |
|---|---|
| `server` | `plugins/voicechat/` |
| `client` | `mods/voicechat/` |

## Configuration

The server speech-to-text configuration supports `libraryPath` for the sherpa-onnx shared library.

## Dependencies

| Dependency | Purpose |
|---|---|
| LeviLamina `26.10.14` | Mod loader SDK (server or client flavor) |
| Opus (`libopus` v1.5.2) | Audio encode/decode (static link) |
| nlohmann-json | JSON configuration parsing (header-only) |
| sherpa-onnx + ONNX Runtime | Server-side streaming Zipformer speech-to-text (server build) |

## Project layout

```
docs/            Module documentation (design, getting started, building)
src/core/        Pure C++ core engine (independent of LeviLamina)
src/shared/      LeviLamina glue shared by both builds
src/server/      Server adaptation layer (server target)
src/client/      Client adaptation layer (client target)
tests/           Core unit tests
```

See the current execution status in [voicechat-fullscope-execution.md](voicechat-fullscope-execution.md), and the module documents for [protocol](protocol.md), [mixer](mixer.md), [admin](admin.md), and [client](client.md).