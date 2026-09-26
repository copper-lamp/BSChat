# 单设备自动冒烟测试

## 需求

在只有一台 Windows 设备时，自动完成 BSChat 的客户端/BDS 模组部署、启动前置检查、进程日志采集和测试结果归档。该方案不伪造第二个真实客户端：它可以自动验证部署和 BDS 启动，也可以由一个真实客户端连接 BDS 后采集日志；双向语音听感仍需要人工按键和语音输入确认。

## 架构

`scripts/Invoke-BSChatSmokeTest.ps1` 提供两种模式：

- 默认部署检查：备份现有 `plugins/bschat` 与 `mods/bschat`，安装服务端和客户端 artifact，验证 DLL 与 manifest 平台字段，并生成 `smoke-results/<timestamp>/result.json`。
- `-Launch`：启动 BDS，等待标准输出出现启动/监听迹象，随后可用 `-ClientCommand` 启动客户端命令；未提供客户端命令时暂停在“等待手动客户端连接”阶段，并继续采集 BDS 日志。

示例：

```powershell
pwsh -File scripts/Invoke-BSChatSmokeTest.ps1 `
  -BdsRoot 'D:\BDS' `
  -ClientRoot 'D:\LeviLaminaClient' `
  -ServerArtifact 'D:\artifacts\server\bschat' `
  -ClientArtifact 'D:\artifacts\client\bschat'
```

启动 BDS 并等待手动启动客户端：

```powershell
pwsh -File scripts/Invoke-BSChatSmokeTest.ps1 `
  -BdsRoot 'D:\BDS' `
  -ClientRoot 'D:\LeviLaminaClient' `
  -ServerArtifact 'D:\artifacts\server\bschat' `
  -ClientArtifact 'D:\artifacts\client\bschat' `
  -Launch -TestDurationSeconds 60
```

如果客户端实例可以由命令行启动，可提供 `-ClientCommand`。该参数会在客户端根目录下通过 PowerShell 启动，例如：

```powershell
-ClientCommand 'Start-Process "D:\\Launcher\\launcher.exe" -ArgumentList "--instance","bschat-test" -Wait'
```

## 单设备测试流程

1. 构建并分别保存 server/client 两份 artifact；不要把 client manifest 当成 server artifact。
2. 运行脚本完成备份和安装。
3. 启动 BDS，确认日志中出现 `bschat` 加载成功以及 `bschat server listeners`/等价启用日志。
4. 启动客户端并连接本机 BDS 地址。
5. 确认客户端加载并启用；由于 GUI 客户端通常没有可见控制台，可直接查看客户端纯文本日志（见下文）。
6. 客户端连接 BDS 后，确认双方的日志均出现 `onJoin`/玩家加入记录；随后在游戏内输入服务端命令 `/bsc test`，再确认日志出现 `[smoke]` 阶段结果。
7. 在客户端按住 PTT 键（默认 `V`），持续说话后释放；观察日志中的音频设备状态和异常信息。
8. 由于只有一个真实客户端，无法验证“另一名玩家听到声音”。可以先使用已有 host/loopback 测试验证协议和服务端混音，再进行单客户端设备采集/播放检查。
9. 检查 `smoke-results/<timestamp>/result.json`、`bds.stdout.log`、`bds.stderr.log`、`client.stdout.log` 和 `client.stderr.log`；模组本身的诊断日志请查看对应的 `bschat-*.log` 文件。

## 模组纯文本日志

客户端与服务端模组都会在自己的 LeviLamina 配置目录下创建纯文本日志，并在进程启动时清空旧文件：

- 客户端：`<客户端实例>/mods/bschat/config/bschat-client.log`（实际位置以模组配置目录为准）。
- BDS 服务端：`<BDS>/plugins/bschat/config/bschat-server.log`（实际位置以模组配置目录为准）。

日志采用 UTF-8 文本，每行格式为 `[Unix毫秒时间戳] [级别] 消息`。每条消息立即 flush，以便崩溃后保留已写入的诊断；即使没有控制台，也能读取加载、事件监听器、玩家加入、WASAPI、自检结果及卸载记录。路径无法写入时日志功能静默停用，不影响语音模组运行。

## 模组内置自检（进入服务器后手动输入指令触发）

`scripts/Invoke-BSChatSmokeTest.ps1` 只覆盖“部署 + 启动 + 日志采集”。链路本身是否通畅由模组内置的 `bsc::client::SmokeTest` 判定：客户端进入世界后处于待命状态，需要玩家输入 `/bsc test` 才会开始，全程在客户端主线程由 `ClientLevelTickEvent` 推进，不额外起线程。

触发方式（专用服务器场景下必须走服务端命令）：

1. 客户端连接 BDS 并进入世界。
2. 在游戏内输入 `/bsc test`（该命令由服务端模组用 `CommandRegistrar::getServerInstance()` 注册，权限 `Any`，flag `NotCheat`，只能在游戏内由玩家执行）。
3. 服务端把命令转成 `Control(SmokeTest)` 消息发给发起者；客户端收到后只置一个原子标志，由 `ClientLevelTickEvent`（主线程）取出并调用 `SmokeTest::begin()`，避免在网络线程里驱动运行时状态。若此时客户端运行时尚未就绪（未进世界），只写一条警告日志并忽略。

> 早期版本把命令注册在客户端（`getClientInstance()`）。实测在专用服务器上，客户端注册表会被服务端下发的命令表覆盖，玩家输入 `/bsc test` 只会得到“未知命令”。因此触发命令固定在服务端。

执行步骤（时间轴相对命令触发的时刻）：

1. `WaitingForReady`：等待 `ClientRuntime` 进入 `Ready`（即收到服务端 `Welcome`）。超时 5000 ms 判定 `handshake = FAIL`；进入 `Failed` 状态立即判负。
2. `Uploading`：调用 `ClientRuntime::setTalking(true)` 打开上行闸门，然后**按配置帧长节拍**（`audio.frameSizeMs`，默认 60 ms）合成一帧 440 Hz、幅度 0.25 的正弦 PCM 交给 `submitPcm`，持续 1500 ms（约 25 帧）。节拍必须等于帧长，保证与真实采集同样的实时速率；早期固定每 20 ms 喂一帧 60 ms 音频等于 3 倍速上行，会触发服务端会话限速（40 帧/秒）而刷 `uplink frame dropped` 告警。该路径与真实按键完全一致，走的是 **AGC → Opus 编码 → 协议上行**，不绕过编解码器。
3. `AwaitingDownlink`：`setTalking(false)` 关闭上行，等待服务端回传 `MixStream`，超时 3000 ms。
4. `LocalTone`：本机直接播一段 440 Hz（10 帧 × 60 ms ≈ 600 ms）。服务端混音对每个接收者都排除发送者本人（`ServerMixer` 的 `speaker->id() == receiver->id()` 跳过），所以**单客户端回环下来听到的必然是静音**，`downlink` 只是链路级判定；可听感只能靠这一段本机播放，或者第二个真实客户端。

客户端日志判定行（前缀统一为 `[smoke]`）：

| 行 | PASS 条件 |
| --- | --- |
| `[smoke] handshake = PASS` | 收到 `Welcome` 并进入 `Ready` |
| `[smoke] uplink = PASS` | `ClientRuntime::sentAudioFrames() > 0`，即确有编码后的包发出 |
| `[smoke] downlink = PASS` | `receivedMixFrames() > 0` 且 `playedMixFrames() > 0`，即收到并解码播放了回传 |
| `[smoke] localTone = PASS` | 本机播放自检的帧已交给渲染 sink（听感需人工确认） |
| `[smoke] overall = PASS` | 以上前三项全部成立 |
| `[smoke] render device wrote N frames` | 由 `ClientMod` 追加：渲染设备实际写入的帧数，区分“交给了 sink”和“真的出声” |

对应的服务端日志判定行（由 `ServerRuntime::setLogSink` 注入的出口写出）：

| 行 | 含义 |
| --- | --- |
| `[smoke] handshake = PASS (session established, ...)` | 收到 `Hello`，建会话并回发 `Welcome` |
| `[smoke] uplink = PASS (first voice frame accepted, bytes=...)` | 首个语音帧通过校验并入抖动缓冲；被拒/被丢时输出 `uplink rejected` 或 `uplink frame dropped` 并附带原因 |
| `[smoke] downlink = PASS (server sent first MixStream to a listener)` | 服务端已向监听者发出首个混音帧 |

单客户端场景下，服务端 `downlink = PASS` 与客户端 `downlink = PASS` 同时出现，即证明“上行 → 服务端解码 → 混音 → 回传 → 客户端解码播放”的完整闭环成立，无需第二名玩家。真正的“另一名玩家听感”仍需第二个真实客户端确认。

## 用真实音频做听感验证（`/bsc play`）

自检的判定都是链路级的（`downlink` 单客户端必然静音、`localTone` 只覆盖本机扬声器）。要用真实音频判断传输质量：

1. 准备一段 WAV（推荐 48kHz；其它采样率/声道也可以，服务端会下混单声道并线性重采样），放到 `<bschat 配置目录>/audio/`，或直接用绝对路径。
2. 进服后输入 `/bsc play music.wav`（或 `/bsc play D:\path\to\music.wav`）：音频会作为独立声源混入下行，你会直接从扬声器听到。
3. `/bsc stop` 停止回放。

实现要点：文件声源用虚拟声源标识（全 `0xFF`）参与混音，不等于任何接收者本人，因此不受自我抑制影响；走的是与真人语音完全相同的「混音 → Opus 编码 → MixStream 下发」链路，协议无改动。详见 `bschat-server-playback.md`。

> 默认编码参数是**语音档**（单声道 20kbps + DTX），音乐听感会明显发闷、高频丢失。要评估音乐质量，请临时把 `bitrateKbps` 调高（如 64~96）并关闭 `enableDtx`——那是参数造成的，不是链路缺陷。
> 只支持 WAV，mp3 需要先外部转码。

## 备注与限制

- `ServerRuntime` 与 `SmokeTest` 均零 LeviLamina 依赖，诊断日志经 `LogSink`（`std::function<void(bool, std::string const&)>`）由外层 `ServerMod`/`ClientMod` 注入；未注入时全部诊断静默丢弃，所以宿主单测不受影响。
- 自检只发合成正弦音，不涉及麦克风采集，因此 WASAPI 采集设备不可用不会让自检失败；但此时 `playedMixFrames_` 仍会计数（渲染 sink 是运行时回调），真实听感仍需人工确认。
- 自检在 `ClientExitLevelEvent` 时随 `smokeTest_` 销毁；重进世界后需要再次输入 `/bsc test` 触发。
- **单客户端听不到自己的声音是设计使然**：服务端按接收者逐个混音并跳过发送者本人（自我抑制，S-FR-10），所以回环的 `MixStream` 对说话者本人是静音，`downlink = PASS` 只证明“回传链路通”，不证明“能听到”。可听感验证有两条路：本机播放自检（`localTone`）验证扬声器链路；真正的双人听感需要第二个真实客户端。
- 命令在服务端注册并在服务端执行，客户端不注册任何命令；客户端只是接收 `Control(SmokeTest)` 后在自己的主线程启动 `SmokeTest`，因此服务端未安装 bschat 时命令不存在（`handshake` 也就不会因等待 `Welcome` 超时判负，命令根本不会出现）。
- 服务端下行原先只认 `PlayerJoinEvent` 填的 `Player*`，而该事件触发晚于客户端首个 `Hello`（实测相差约 30 秒），期间 `Welcome` 会被静默丢弃、客户端每 5 秒重发 `Hello`。现在 `GamePacketTransport` 在收到首个数据包时就登记该 peer 的 `Player*` 作为回发兜底（玩家离线/停用时注销），并在两者都拿不到目标时写明确告警，不再静默丢包。
- 脚本不会自动修改版本号，也不会创建 tag。
- 脚本会移动已有安装目录为带时间戳的 `.smoke-backup-*` 目录；测试结束后的恢复/清理应由操作者根据结果决定。
- 自动判断“能否听到声音”需要第二个真实客户端或音频回环设备。单设备、单客户端场景下，内置自检已能自动覆盖协议与编解码链路，仅听感需人工确认。
