# Building BSChat

This guide explains how to build BSChat from source with xmake.

## Prerequisites

- Windows x64
- [xmake](https://xmake.io/)
- A Clang / Clang-CL toolchain configured for xmake

> [!NOTE]
> The build pulls LeviLamina SDK packages automatically. The repository compiles into a
> server build or a client build depending on the `--target_type` option (default:
> `server`).

> [!IMPORTANT]
> The module is compiled with clang-cl while the LeviLamina release package is built with
> MSVC. LeviLamina derives built-in event IDs from compiler-specific type names
> (`ll::event::player::PlayerJoinEvent`), so this module binds the SDK's canonical IDs
> explicitly through `ll::event::getEventId<T>` specializations in
> `src/server/entry/ServerMod.cpp` and `src/client/entry/ClientMod.cpp`. Without those
> bindings every listener registration returns false and the module fails to enable. Add
> the same binding when subscribing to a new built-in event.

## Build the module

Configure and build the **server** version (default):

```bash
xmake f -y -p windows -a x64 -m release
xmake build bschat
```

Build the **client** version:

```bash
xmake f -y -p windows -a x64 -m release --target_type=client
xmake build bschat
```

## Run unit tests

The core engine and host-test adapters have zero LeviLamina dependency and run as a host binary:

```bash
xmake f -y -p windows -a x64 -m release
xmake build bschat-tests
xmake run bschat-tests
```

The `bschat-tests` target is `set_default(false)`, so it is not built by default. If xmake reports a stale package lock, stop other xmake processes and remove the matching `package.lock` under `%LOCALAPPDATA%\\.xmake\\cache\\packages`; do not delete a lock held by an active build. The current machine has the ATL headers and `atls.lib`; configure and build through the Visual Studio Developer Command Prompt so both `INCLUDE` and `LIB` contain the `atlmfc` directories. The client target now builds successfully with this environment. The reliable command pattern is:

```powershell
cmd /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && set "LIB=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\atlmfc\lib\x64;%LIB%" && set "INCLUDE=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\atlmfc\include;%INCLUDE%" && xmake f -a x64 -m debug -p windows --target_type=client -y && xmake build bschat'
```

Both builds write to the same path, `bin/bschat/bschat.dll`, and both produce a file literally named
`bschat.dll`. Switching `--target_type` therefore overwrites the previous build, and the two outputs are
different binaries (server builds define `LL_PLAT_S` and compile `src/server/**`; client builds compile
`src/client/**` and additionally link the whole LeviLamina archive). Always copy each build out to a
separate folder immediately after building, e.g. `artifacts/server/bschat/` and `artifacts/client/bschat/`,
and set that copy's `manifest.json` `platform` field to `server` or `client` respectively.

### Verifying which flavor a DLL is

A server build must not reference client events. Loading a client build as a server plugin fails at load
time with `The specified procedure could not be found` and a dependency diagnostic listing
`ll::event::client::ClientJoinLevelEvent` / `ll::event::input::KeyInputEvent`. Check before deploying:

```powershell
dumpbin /DEPENDENTS bin\bschat\bschat.dll
```

### Verifying that the server configuration took effect

`xmake f` writes `.xmake\windows\x64\xmake.conf`. If dependency installation fails, the new option is
**not** persisted and a later `xmake build` silently rebuilds the previous flavor. Always confirm the
option was written before trusting a server build:

```powershell
Select-String -Path .xmake\windows\x64\xmake.conf -Pattern 'target_type'
```

The server build must also compile `src\server\**` (visible with `xmake build -v bschat`), not `src\client\**`.

### ATL requirement

Building the LeviLamina **server** SDK variant requires `atls.lib` (the client variant does not). The
Visual Studio Developer Command Prompt resets `LIB`, so setting `LIB`/`INCLUDE` on the command line has no
effect. The machine instead has `atls.lib` copied next to the MSVC libraries, where the linker searches by
default:

```
<VS>\VC\Tools\MSVC\<ver>\lib\x64\atls.lib   (copied from ...\atlmfc\lib\x64\atls.lib)
```

Without this, `xmake f --target_type=server` aborts during SDK installation with
`LINK : fatal error LNK1104: cannot open file 'atls.lib'`.

## Targets

| Target | Kind | Description |
|---|---|---|
| `bschat-core` | static library | Pure C++ core engine (codec / protocol / audio / pipeline / config) |
| `bschat` | module | Main module; picks `src/server` or `src/client` by `target_type` |
| `bschat-tests` | binary | Core and host-test adapters (no LeviLamina) |

## Deployment

After a successful build, place the output in the correct directory:

| Target build type | Deploy to |
|---|---|
| `server` | `plugins/bschat/` |
| `client` | `mods/bschat/` |

> [!IMPORTANT]
> Deploy the **whole module directory**, not just the DLL. Besides `bschat.dll` and `manifest.json`,
> the module needs `lang/` (i18n), `panels/` (settings and admin panel definitions) and `icons/` (HUD status
> icon PNGs). `xmake`'s `after_build` copies all of them into `bin/bschat/`, but a deploy step that copies
> only the DLL plus manifest silently produces a hollow module: panels fail to open, text falls back to raw
> i18n keys, and the HUD shows text only. `scripts/Invoke-BSChatSmokeTest.ps1` copies the full tree and
> fails loudly when any of the three directories is missing. When updating an existing install, keep its
> `config/` directory.

## Configuration

The server speech-to-text configuration supports `libraryPath` for the sherpa-onnx shared library.

## Dependencies

| Dependency | Purpose |
|---|---|
| LeviLamina `26.10.14` | Mod loader SDK (server or client flavor) |
| Opus (`libopus` v1.5.2) | Audio encode/decode (static link) |
| nlohmann-json | JSON configuration parsing (header-only) |
| sherpa-onnx + ONNX Runtime | Server-side streaming Zipformer speech-to-text (server build) |

> [!IMPORTANT]
> `xmake.lua` overrides LeviLamina's `rapidjson` dependency to `2025.02.05` with
> `add_requireconfs("levilamina.rapidjson", {version = "2025.02.05", override = true})`.
> The `rapidjson v1.1.0` that LeviLamina `26.10.14` pins has a `GenericStringRef::operator=` that assigns
> to its `const SizeType length` member. Modern clang rejects that outright, and the MC header chain
> (`ll/api/memory/MemoryOperators.h` → `mc/deps/core/memory/IMemoryAllocator.h` →
> `mc/_HeaderOutputPredefine.h:101` → `rapidjson/document.h`) pulls rapidjson into every translation unit
> that touches an MC type, so `MemoryOperators.cpp` fails first:
>
> ```
> rapidjson/document.h(319,82): error: cannot assign to non-static data member 'length'
>     with const-qualified type 'const SizeType'
> ```
>
> `rapidjson` is header-only, and upstream removed that assignment from `2022.x` on. Do not
> "fix" this by hand-editing the installed package under `%LOCALAPPDATA%\.xmake\packages`: that patch is
> invisible to CI, which installs a pristine `v1.1.0` and fails again.

## Continuous integration

`build.yml` and `release.yml` build both `target_type` flavors on `windows-latest`. Two rules keep the
xmake dependency installation reproducible on those runners:

- **Precompiled packages are disabled** (`xmake global --policies=package.precompiled:n`). A package
  downloaded from upstream may be produced by a different toolchain than the runner's clang-cl / MSVC
  combination and can be installed without a usable library file, which shows up later as
  `ninja: error: '.../zlib/v1.3.1/<hash>/lib/zlib.lib', needed by 'src/curl.exe', missing`.
- **Only `~/AppData/Local/.xmake/packages` is cached**, keyed on the package repository revisions and
  `xmake.lua`, with `XMAKE_PKG_CACHEDIR` pointing at a per-run directory. The whole `.xmake` directory
  must not be cached: `cache/packages` keeps intermediate build trees from failed runs, and restoring
  them makes xmake treat a half-installed dependency as finished, so the failure repeats forever.

If a runner still fails while installing `libcurl` / `zlib`, delete the cached entry
(`xmake-packages-*`) in the repository's Actions cache before re-running.

## Project layout

```
docs/            Module documentation (design, getting started, building)
src/core/        Pure C++ core engine (independent of LeviLamina)
src/shared/      LeviLamina glue shared by both builds
src/server/      Server adaptation layer (server target)
src/client/      Client adaptation layer (client target)
tests/           Core unit tests
```

See the current execution status in [bschat-fullscope-execution.md](bschat-fullscope-execution.md), and the module documents for [protocol](protocol.md), [mixer](mixer.md), [admin](admin.md), and [client](client.md).