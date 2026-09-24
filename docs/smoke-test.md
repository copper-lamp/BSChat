# 单设备自动冒烟测试

## 需求

在只有一台 Windows 设备时，自动完成 Betterlanguagechat 的客户端/BDS 模组部署、启动前置检查、进程日志采集和测试结果归档。该方案不伪造第二个真实客户端：它可以自动验证部署和 BDS 启动，也可以由一个真实客户端连接 BDS 后采集日志；双向语音听感仍需要人工按键和语音输入确认。

## 架构

`scripts/Invoke-VoiceChatSmokeTest.ps1` 提供两种模式：

- 默认部署检查：备份现有 `plugins/voicechat` 与 `mods/voicechat`，安装服务端和客户端 artifact，验证 DLL 与 manifest 平台字段，并生成 `smoke-results/<timestamp>/result.json`。
- `-Launch`：启动 BDS，等待标准输出出现启动/监听迹象，随后可用 `-ClientCommand` 启动客户端命令；未提供客户端命令时暂停在“等待手动客户端连接”阶段，并继续采集 BDS 日志。

示例：

```powershell
pwsh -File scripts/Invoke-VoiceChatSmokeTest.ps1 `
  -BdsRoot 'D:\BDS' `
  -ClientRoot 'D:\LeviLaminaClient' `
  -ServerArtifact 'D:\artifacts\server\voicechat' `
  -ClientArtifact 'D:\artifacts\client\voicechat'
```

启动 BDS 并等待手动启动客户端：

```powershell
pwsh -File scripts/Invoke-VoiceChatSmokeTest.ps1 `
  -BdsRoot 'D:\BDS' `
  -ClientRoot 'D:\LeviLaminaClient' `
  -ServerArtifact 'D:\artifacts\server\voicechat' `
  -ClientArtifact 'D:\artifacts\client\voicechat' `
  -Launch -TestDurationSeconds 60
```

如果客户端实例可以由命令行启动，可提供 `-ClientCommand`。该参数会在客户端根目录下通过 PowerShell 启动，例如：

```powershell
-ClientCommand 'Start-Process "D:\\Launcher\\launcher.exe" -ArgumentList "--instance","voicechat-test" -Wait'
```

## 单设备测试流程

1. 构建并分别保存 server/client 两份 artifact；不要把 client manifest 当成 server artifact。
2. 运行脚本完成备份和安装。
3. 启动 BDS，确认日志中出现 `voicechat` 加载成功以及 `voicechat server listeners`/等价启用日志。
4. 启动客户端并连接本机 BDS 地址。
5. 确认客户端日志出现 `voicechat 已加载` 和 `voicechat client listeners enabled`。
6. 在客户端按住 PTT 键（默认 `V`），持续说话后释放；观察 BDS 与客户端日志是否出现握手、音频设备和异常信息。
7. 由于只有一个真实客户端，无法验证“另一名玩家听到声音”。可以先使用已有 host/loopback 测试验证协议和服务端混音，再进行单客户端设备采集/播放检查。
8. 检查 `smoke-results/<timestamp>/result.json`、`bds.stdout.log`、`bds.stderr.log`、`client.stdout.log` 和 `client.stderr.log`。

## 模组内置自检（进入服务器后自动跑）

`scripts/Invoke-VoiceChatSmokeTest.ps1` 只覆盖“部署 + 启动 + 日志采集”。链路本身是否通畅由模组内置的 `vc::client::SmokeTest` 自动判定：客户端进入世界后立即武装，握手完成即开始，全程在客户端主线程由 `ClientLevelTickEvent` 推进，不额外起线程。

执行步骤（时间轴相对客户端打开自检的时刻）：

1. `WaitingForReady`：等待 `ClientRuntime` 进入 `Ready`（即收到服务端 `Welcome`）。超时 5000 ms 判定 `handshake = FAIL`；进入 `Failed` 状态立即判负。
2. `Uploading`：调用 `ClientRuntime::setTalking(true)` 打开上行闸门，然后每 20 ms 合成一帧 440 Hz、幅度 0.25 的正弦 PCM 交给 `submitPcm`，持续 1500 ms。该路径与真实按键完全一致，走的是 **AGC → Opus 编码 → 协议上行**，不绕过编解码器。
3. `AwaitingDownlink`：`setTalking(false)` 关闭上行，等待服务端回传 `MixStream`，超时 3000 ms。

客户端日志判定行（前缀统一为 `[smoke]`）：

| 行 | PASS 条件 |
| --- | --- |
| `[smoke] handshake = PASS` | 收到 `Welcome` 并进入 `Ready` |
| `[smoke] uplink = PASS` | `ClientRuntime::sentAudioFrames() > 0`，即确有编码后的包发出 |
| `[smoke] downlink = PASS` | `receivedMixFrames() > 0` 且 `playedMixFrames() > 0`，即收到并解码播放了回传 |
| `[smoke] overall = PASS` | 以上三项全部成立 |

对应的服务端日志判定行（由 `ServerRuntime::setLogSink` 注入的出口写出）：

| 行 | 含义 |
| --- | --- |
| `[smoke] handshake = PASS (session established, ...)` | 收到 `Hello`，建会话并回发 `Welcome` |
| `[smoke] uplink = PASS (first voice frame accepted, bytes=...)` | 首个语音帧通过校验并入抖动缓冲；被拒/被丢时输出 `uplink rejected` 或 `uplink frame dropped` 并附带原因 |
| `[smoke] downlink = PASS (server sent first MixStream to a listener)` | 服务端已向监听者发出首个混音帧 |

单客户端场景下，服务端 `downlink = PASS` 与客户端 `downlink = PASS` 同时出现，即证明“上行 → 服务端解码 → 混音 → 回传 → 客户端解码播放”的完整闭环成立，无需第二名玩家。真正的“另一名玩家听感”仍需第二个真实客户端确认。

## 备注与限制

- `ServerRuntime` 与 `SmokeTest` 均零 LeviLamina 依赖，诊断日志经 `LogSink`（`std::function<void(bool, std::string const&)>`）由外层 `ServerMod`/`ClientMod` 注入；未注入时全部诊断静默丢弃，所以宿主单测不受影响。
- 自检只发合成正弦音，不涉及麦克风采集，因此 WASAPI 采集设备不可用不会让自检失败；但此时 `playedMixFrames_` 仍会计数（渲染 sink 是运行时回调），真实听感仍需人工确认。
- 自检在 `ClientExitLevelEvent` 时随 `smokeTest_` 销毁，重进世界会重新武装并重跑一次。
- 脚本不会自动修改版本号，也不会创建 tag。
- 脚本会移动已有安装目录为带时间戳的 `.smoke-backup-*` 目录；测试结束后的恢复/清理应由操作者根据结果决定。
- 自动判断“能否听到声音”需要第二个真实客户端或音频回环设备。单设备、单客户端场景下，内置自检已能自动覆盖协议与编解码链路，仅听感需人工确认。
