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

## 备注与限制

- 当前会话未发现本机 BDS 安装、客户端安装路径或正在运行的 BDS/客户端进程，因此尚未执行真实部署或连接测试。
- 脚本不会自动修改版本号，也不会创建 tag。
- 脚本会移动已有安装目录为带时间戳的 `.smoke-backup-*` 目录；测试结束后的恢复/清理应由操作者根据结果决定。
- 自动判断“能否听到声音”需要第二个真实客户端或音频回环设备。单设备、单客户端场景只能自动化部署、启动、日志和网络握手前置检查。
