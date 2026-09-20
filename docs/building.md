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

The `voicechat-tests` target is `set_default(false)`, so it is not built by default. If xmake reports a stale package lock, stop other xmake processes and remove the matching `package.lock` under `%LOCALAPPDATA%\\.xmake\\cache\\packages`; do not delete a lock held by an active build. The current machine has the ATL headers and `atls.lib`; configure and build through the Visual Studio Developer Command Prompt so both `INCLUDE` and `LIB` contain the `atlmfc` directories. The client target now builds successfully with this environment. The reliable command pattern is:

```powershell
cmd /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "LIB=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\atlmfc\lib\x64;%LIB%" && set "INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\atlmfc\include;%INCLUDE%" && xmake f -a x64 -m debug -p windows --target_type=client -y && xmake build voicechat'
```

The deployable client artifact is generated at `bin/voicechat/voicechat.dll`. The server target uses the same module target with `--target_type=server`; verify it separately before deployment. The current client build was verified with the Visual Studio Developer Command Prompt and produced the deployable DLL.

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