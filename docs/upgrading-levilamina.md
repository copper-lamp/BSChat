# 升级 LeviLamina / MC 版本

这份文档记录把 BSChat 从一条 MC 版本线迁到另一条（比如 26.10 → 26.20）时的完整检查清单、
已踩过的坑，以及每一步的验证方式。按顺序走完即可，不需要重新摸索。

## 需求

一次版本迁移要达成的是：

- `xmake.lua` 声明的 SDK 版本、发布用的 `tooth.json` 依赖、以及所有对外文档中的基线版本号
  三者一致，不允许只改一处。
- server 与 client 两个 target 都能在新宿主版本上编译、链接成功，且没有缺失符号。
- 迁移不改变模组对外行为：协议、配置格式、面板、HUD 全部保持原样，只换宿主。
- 迁移过程产生的判断（哪些 override 还有效、哪些 SDK 行为变了）写回文档，不能只留在脑子里。

不改需求，只换宿主版本。所以任何"顺手重构"都不属于本次迁移范围。

## 架构

版本号在仓库里有五个落点，缺一个都会留下不一致：

| 落点 | 内容 | 漏掉的后果 |
|---|---|---|
| `xmake.lua` | `add_requires("levilamina <ver>")` | 本地/CI 仍编旧 SDK，与 tooth 声明不符 |
| `tooth.json` | 两个 variant 的 `dependencies` | 用户装到宿主版本与模组实际编译版本不符，运行时加载失败 |
| `README.md` / `README_ZH.md` | badge、安装步骤、兼容性段落 | 用户按错版本装宿主 |
| `docs/getting-started.md` / `docs/building.md` | 前置条件表、依赖表 | 与 README 矛盾 |
| `THIRD_PARTY_NOTICES.md` | LeviLamina 版本 + 许可证 | 第三方声明失准 |

代码侧只有一处需要按SDK 版本审查：`src/shared/event/EventIdBindings.h`（见下）。

### 为什么必须手工绑定事件 ID

EventBus 的条目键是 `getEventId<T>()`，其默认值取 `ll::reflection::type_unprefix_name_v<T>`，而它按
编译器分叉取名：

- LeviLamina 发布包由 **MSVC** 编译，用 `__FUNCSIG__`。MSVC **不打印 inline namespace**，事件类型
  即便声明在 `namespace ll::event::inline client` 里，得到的也是 `ll::event::ClientJoinLevelEvent`。
- 本模组由 **clang-cl** 编译，用 `__PRETTY_FUNCTION__`。clang **会打印 inline namespace**，同样一个
  类型得到 `ll::event::client::ClientJoinLevelEvent`。

两者 FNV1a 哈希不同，EventBus 里不存在对应条目，`emplaceListener` 返回空监听器且不报错，表现为
"模组加载了但所有监听器静默失效"。

因此 `EventIdBindings.h` 把**双端**用到的事件逐个显式绑定到 SDK 侧的规范 ID（**去掉 inline 段**）。
**升级 SDK 后必须重新核对这份清单**：新增监听要登记；SDK 若改了命名空间形态（inline / 非 inline）
也要跟着改，否则字符串对不上，症状同样是监听器静默失效。

校验方式只有一个，别靠推断——直接从 SDK 的 DLL 里读出 SDK 侧真实使用的名字：

```powershell
# LeviLamina.dll 在 xmake 包目录的 bin/ 下
Select-String -Path <pkg>/bin/LeviLamina.dll -Pattern "ll::event::" -Encoding ascii -AllMatches |
  ForEach-Object { $_.Matches.Value } | Sort-Object -Unique
```

26.40.6 的 client DLL 里全部是 `ll::event::PlayerJoinEvent` / `ll::event::KeyInputEvent` 这种**无子
命名空间**的形式；任何带 `client::` / `world::` / `player::` 的写法都是错的。历史文档曾断言 MSVC
会保留 inline namespace 前缀，与 DLL 实测相反，已按实测更正。

### inline namespace 是最容易漏的一处

26.20.7 起 `ll::event` 下的所有子命名空间（`client` / `command` / `entity` / `input` / `io` /
`player` / `render` / `server` / `world`）都是 inline namespace，不只是客户端那几个。`EventIdBindings.h`
里的前置声明如果还写成非 inline，clang 会报：

```
warning: inline namespace reopened as a non-inline namespace [-Winline-namespace-reopened-noninline]
```

更要紧的是它同时意味着前置声明和后续头文件的定义不是同一个命名空间，属于静默的类型不匹配。
所以升级后第一次编译要专门扫一遍这个警告。

## 执行

### 1. 确认目标版本真实存在

先看本机缓存和上游 tag，不要凭记忆写版本号：

```powershell
Get-ChildItem "$env:LOCALAPPDATA\.xmake\packages\l\levilamina" -Directory | Select-Object Name
git ls-remote --tags https://github.com/LiteLDev/LeviLamina.git | Select-String "<mc_line>"
```

本机没有的版本要靠 `xmake repo -u` 拉包定义，否则 `xmake f` 会报找不到版本而不是报编译错误。

### 2. 改版本号并拉依赖

改 `xmake.lua` 和 `tooth.json`，然后：

```powershell
cmd /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && xmake repo -u && xmake f -a x64 -m release -p windows --target_type=server -y'
```

### 3. 编译 SDK 时降并行度

LeviLamina 是从源码编译的，每个 TU 都拉进整套 MC 头。16 线程机器上并行编译会因内存耗尽让
clang 崩在一个看起来毫不相关的文件上：

```
error: clang frontend command failed due to signal
clang-cl: note: diagnostic msg: C:\...\Temp\PlayerDisconnectEvent-d753bb.cpp
```

这不是 SDK 的 bug，是内存不够。诊断信息里的文件名（`PlayerDisconnectEvent.cpp`）和真实瓶颈毫无
关系，不要顺着它去查事件代码。直接降并行：

```powershell
xmake build -j 4 bschat
```

失败日志在 `%LOCALAPPDATA%\.xmake\cache\packages\<id>\l\levilamina\<ver>\installdir.failed\logs\install.txt`。

### 4. 编两个 target

server 和 client 各编一遍，`-r` 强制全量以免缓存掩盖问题：

```powershell
xmake f -a x64 -m release -p windows --target_type=server -y; xmake build -j 4 bschat
xmake f -a x64 -m release -p windows --target_type=client -y; xmake build -r -j 4 bschat
```

两个 target 输出到同一个 `bin/bschat/bschat.dll`，后一次会覆盖前一次。每次编完立刻把产物拷到
`artifacts/server/bschat/` 或 `artifacts/client/bschat/`，并把该副本 `manifest.json` 的 `platform`
改成对应值。

### 5. 验证产物 flavor

`xmake f` 写`.xmake\windows\x64\xmake.conf`。依赖安装失败时新选项**不会**被持久化，之后的
`xmake build` 会静默重编上一种 flavor。确认选项真的写进去了：

```powershell
Select-String -Path .xmake\windows\x64\xmake.conf -Pattern 'target_type'
dumpbin /DEPENDENTS bin\bschat\bschat.dll
```

server 产物不能引用客户端事件。反过来（client 产物当server 插件加载）会在加载时报
`The specified procedure could not be found`，并列出 `ll::event::client::ClientJoinLevelEvent` /
`ll::event::input::KeyInputEvent`。看到这两个符号就说明 flavor 搞反了。

### 6. 核对产物里的事件 ID 字符串

这一步不能省。编译通过只说明头文件对得上，**运行时监听能否注册取决于字符串**，而字符串在 SDK 里
是什么只有 DLL 知道。改完 `EventIdBindings.h` 后，把产物和 SDK DLL 都读一遍对比：

```powershell
function Get-AsciiStrings($path, $pattern) {
  $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($path))
  [regex]::Matches($text, $pattern) | ForEach-Object { $_.Value } | Sort-Object -Unique
}
# 模组产物里绑定的 ID
Get-AsciiStrings bin\bschat\bschat.dll 'll::event::[A-Za-z_:]*'
# SDK 侧真实使用的 ID
Get-AsciiStrings <levilamina包>/bin/LeviLamina.dll 'll::event::[A-Za-z_:]*'
```

两边必须逐条相等。任何只出现在模组产物里、或两边写法不一致的 ID，都会让对应监听静默失效，而且
**不会产生任何日志**。

### 7. 扫警告

编译输出里逐条看 `warning:`。迁移引入的新警告通常意味着 SDK 行为变了，不能因为不影响构建就
放过。26.40.6 迁移的唯一新警告是 `-Winconsistent-dllimport`（来自 `Packet::getRuntimeId` 的重复
兜底定义），改用 SDK 导出实现后消失；`-Winline-namespace-reopened-noninline` 对应第 2 节说的
`EventIdBindings.h` 修正。

## 备注

### 已验证的迁移记录

| 从 | 到 | 代码改动 | 结论 |
|---|---|---|---|
| 26.10.14 | 26.20.7 | `ClientEventIds.h` 四个命名空间补 `inline` | server / client 均编译链接通过，无缺失符号 |
| 26.20.7 | 26.40.6 | 见下面「26.40.x 上的新发现」四条 | server / client 均编译链接通过，零警告，两边 flavor 与事件 ID 均已核对 |
| 26.40.6 | 26.51.6 | **无代码改动**，仅换 pin（见下节） | server / client 均编译链接通过，零警告，两边 flavor 与事件 ID 均已核对 |

### 26.51.x 上的新发现

**这次迁移零代码改动，是目前唯一一次纯 pin 迁移。** 迁移前把项目 include 的全部 59 个 SDK 头在
`v26.40.6` 与 `v26.51.6` 之间逐文件做了内容比对，结论如下（引用的行号以 26.51.6 为准）：

- **头文件路径与位置完全不变。** 客户端头仍在 `src-client/`、服务端头在 `src-server/`、公共头在
  `src/`，`add_includedirs("src-client")` 的引用方式不用动（这一布局 26.40.6 就已经是这样了）。
- **本项目实际使用的全部 API 签名未变**，包括 `RectangleArea`（仍是 `TypedStorage<4,4,float>` 的
  `_x0/_x1/_y0/_y1`，仍无 5 参构造）、`ScreenView::mSize`、`ClientInstance::getFontHandle()`、
  `AppPlatform::loadImage(mce::Image&, Core::Path const&)`（仍在 `#ifdef LL_PLAT_C` 内）、
  `TexturePtr::mClientTexture`、`MinecraftUIRenderContext::drawImage/flushImages`。
- **8 个事件类型（`ClientJoinLevelEvent`、`ClientExitLevelEvent`、`ClientLevelTickEvent`、
  `KeyInputEvent`、`AfterUIRenderEvent`、`PlayerJoinEvent`、`PlayerDisconnectEvent`、
  `ServerLevelTickEvent`）的定义逐字节一致**，`EventId.h` / `EventBus.h` / `I18n.h` /
  `TargetedBedrock.h` 也零差异。inline namespace 形态不变，SDK 仍由 MSVC 编译，
  `EventIdBindings.h` 的 8 条绑定继续有效 —— 但按纪律仍做了 DLL 实测核对（见执行第 6 步）。
- **SDK 的依赖集合逐条相同**（`entt v4.0.0`、`fmt 11.2.0`、`mimalloc`、`cpr`、`libhat`、
  `demangler`、`rapidjson 2025.02.05` 等），`rapidjson` 覆盖依旧不需要。`bedrockdata` 跟着
  SDK 走（26.40.6 是 `v26.40.8-server.9` / `v26.40.5-client.9`，26.51.6 是
  `v26.51.1-server.7` / `v26.51.1-client.7`）。

有内容差异但**与本项目无关**的头（下次迁移不必重查，已逐条确认未调用）：

| 头 | 变化 | 为何不影响 |
|---|---|---|
| `ll/api/memory/MemoryOperators.h` | SDK 内部从 `allocate`/`release` 改名 `_allocate`/`_release`，并给 `operator delete` 加 null 检查 | 本项目只 `#define LL_MEMORY_OPERATORS` 后 include 该头、自己不实现分配器 |
| `mc/client/game/ClientInstance.h` | `setServerPingTime` / `getServerPingTime` 等虚函数改签名，大批 `UntypedStorage` 成员改尺寸 | 用到的 `getFontHandle()` 完全未变 |
| `mc/client/gui/screens/ScreenView.h` | 游戏内部私有方法被删/移动（`getInputAreas`、`reload` 等） | 用到的 `mSize` 未变 |
| `mc/deps/application/AppPlatform.h` | pimpl `Members` 拆分，247 行差异 | `loadImage` 逐字节未变 |
| `ServerNetworkHandler.h` / `NetworkIdentifier.h` | 大量增量（PubSub connector、`getCorrelationId` 移除） | 只用 `getServerNetworkHandler()` 取实例 |
| `CommandOrigin.h` / `CommandOutput.h` / `ServerPlayer.h` / `Player.h` | `Random::generateUUID()` 加参数、`swing()` 加 `HandSlot` 参数、`isExternalCommunicationAllowed` 新增 | 均未调用 |
| `Font.h` / `FontHandle.h` / `TextureGroup.h` / `PathView.h` / `Image.h` / `optional_ref.h` / `ResourceLocation.h` / `BinaryStream.h` | `T const&` → `T const` 风格改写、成员增删 | 语义等价或未使用 |

`ll/` 下唯一被删的是 `io/DefaultSink.{h,cpp}`（改为 `ConsoleSink` / `DefaultSinks` /
`RotatePolicy`），以及 `core/tweak` 与 `core/network` 下的内部实现文件，均非公开 API。

### 26.40.x 上的新发现

> 下面四条是 26.40.x 首次引入并已在 26.51.x 上复核仍然成立（除 `Packet::getRuntimeId` 那条的
> 前提见文末）。26.51.x 自身的差异见上面「26.51.x 上的新发现」。

**事件 ID 必须去掉 inline 段（本次迁移最严重的一处）。** 26.40.6 的头里 `ll::event` 下所有子命名
空间都是 inline，于是 `getEventId<T>` 的默认值在两侧算出不同字符串，双端全部事件监听静默失效。
历史绑定（`ll::event::client::ClientJoinLevelEvent`、`ll::event::player::PlayerJoinEvent` …）全部
是错的。已把 `src/client/entry/ClientEventIds.h` 扩成 `src/shared/event/EventIdBindings.h`，服务端与
客户端共 8 个事件统一绑定到无 inline 段的规范 ID，并以 SDK DLL 实测为准。详见第 2 节与第 6 步。

**`RectangleArea` 不再提供 5 参构造。** 26.10.14 里
`RectangleArea(float x0, float y0, float x1, float y1, bool checkForValidity)` 只在 `LL_PLAT_C` 下
声明；26.40.6 整个构造函数和 `grow` / `translate` 一起从公开头消失，只剩 public 成员
`_x0/_x1/_y0/_y1`（注意声明顺序是 x0,x1,y0,y1，不是 x0,y0,x1,y1）。`HudRenderer.cpp` 改为
`makeRect()` 按成员赋值，等价于旧构造的 `checkForValidity=false`。

**贴图加载与访问 API 被收窄。** `AppPlatform::loadTexture` / `loadTextureFromStream` 已移除，
`loadImage(mce::Image&, Core::Path const&)` 仍在（按扩展名分派），是现在唯一可用的解码入口；
`TexturePtr::getClientTexture()` / `operator*` 也被移除，而 `TexturePtr::mClientTexture` 是
`shared_ptr<BedrockTextureData const>`，要取 `mce::ClientTexture` 得写成
`texture.mClientTexture->mClientTexture.get()`，并且要额外包含
`mc/deps/minecraft_renderer/renderer/BedrockTextureData.h`（否则是不完整类型）与
`mc/deps/minecraft_renderer/resources/ClientTexture.h`（`TexturePtr.h` 已不再前置声明它）。
`StatusIcons.cpp` / `StatusIcons.h` 按此改完，`drawImage` / `flushImages` 签名未变。

**`Packet::getRuntimeId` 不再需要兜底。** 26.10.14 时代 SDK 头声明了这个虚函数但包导入库不含
对应符号，clang-cl 为 `PacketBase` 实例化 vtable 时会留下未定义引用，只能在模组里手写一份。
26.40.6 起它在头里带 `LLNDAPI`（`ll/api/network/packet/Packet.h:56`），导入库中能查到
`?getRuntimeId@Packet@network@ll@@UEBA_KXZ`，SDK 源码 `src/ll/api/network/packet/Packet.cpp`
的实现就是 `doHash(getName())`，与手写版语义一致。留着反而遮蔽 SDK 实现并触发
`-Winconsistent-dllimport`。判断方法：
`dumpbin /LINKERMEMBER:2 <levilamina>/lib/LeviLamina.lib | findstr getRuntimeId`，查得到就能删。

**rapidjson 覆盖已失效。** 26.40.6 的 `xmake.lua` 自己写着 `add_requires("rapidjson 2025.02.05")`，
`add_requireconfs` 覆盖不再需要，删掉后 `xmake f -c` 重新解析依然编过
（`xmake show -t bschat` 里头路径仍指向 `2025.02.05`）。下次升级不必再验证这一项，除非 SDK 又把
rapidjson 钉回 `v1.1.0`——那时表现是
`rapidjson/document.h(319,82): error: cannot assign to non-static data member 'length'`，
首当其冲的是 `src/shared/MemoryOperators.cpp`。

**事件命名空间形态没变。** 26.40.6 的头仍是 `namespace ll::event::inline client`，inline 修正继续
有效。

### 版本号与 MC 版本线的对应

LeviLamina 的 `major.minor` 就是 MC 的 `major.minor`（`docs/main/contents/versions.md` 里
`26.20.x ↔ MC 26.20.5`、`26.10.x ↔ MC 26.10.4`），第三段是 SDK 自己的补丁号。已验证的对应关系：

| MC 线 | levilamina pin | bedrock-runtime-data 依赖 tag |
|---|---|---|
| 26.10 | `26.10.14` | — |
| 26.20 | `26.20.7` | — |
| 26.40 | `26.40.6` | `v26.40.8-server.9` / `v26.40.5-client.9` |
| 26.51 | `26.51.6` | `v26.51.1-server.7` / `v26.51.1-client.7` |

`release.yml` 的 "Verify pinned levilamina matches MC line" 取 pin 的前两段与 tag 里的
`-mc<...>` 比对，因此 26.51 这条线的发布 tag 必须是 `v<version>-mc26.51` 而不是 `-mc26.5`，
否则 CI 直接失败。

注意 `xmake-repo` 的 `levilamina/versions/` 里 **没有 `26_51_3`**（该 patch 号上游没发），
可用的是 `26_51_0/1/2/4/5/6`。确认某版本存在时看 `versions/versions.txt`，比翻 tag 可靠。

### 不要动的东西

- `ll_memory_operator_overrided` 宏和 `src/shared/MemoryOperators.cpp`：这是 LeviLamina 模板的必需
  部分，与 SDK 版本无关。
- 迁移过程中不要顺手重构业务代码。混在一起的改动会让"升级后行为变了"变得无法归因。

### 相关文档

- [building.md](building.md) — 构建命令、ATL 要求、CI 依赖安装注意事项
- [client.md](client.md) — 客户端适配层，含事件绑定与 HUD 渲染
- [getting-started.md](getting-started.md) — 用户侧安装步骤