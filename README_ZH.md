
<div align="center">
  <h1>Better Speech Chat</h1>
  <p><strong>进服开麦，即刻畅聊。</strong></p>
  <p>面向 LeviLamina 的开源 Minecraft 基岩版游戏内实时语音聊天模组，服务端混音、客户端收发，可选实时语音转写字幕。</p>
  <p>
    <img src="https://img.shields.io/badge/release-v0.0.0-4c8bf5?style=flat-square" alt="BSChat v0.0.0">
    <img src="https://img.shields.io/badge/Minecraft%20Bedrock-Windows%20x64-62b47a?style=flat-square" alt="Windows x64 Minecraft 基岩版">
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue?style=flat-square" alt="AGPL-3.0 许可证"></a>
  </p>
  <p>
    <img src="https://img.shields.io/badge/LeviLamina-26.10.14-7b68ee?style=flat-square" alt="LeviLamina 26.10.14">
  </p>
  <p>
    <a href="../docs/getting-started.md">快速上手</a>
    ·
    <a href="https://github.com/copper-lamp/BSChat/releases">发行版本</a>
    ·
    <a href="CHANGELOG.md">更新日志</a>
    ·
    <a href="https://github.com/copper-lamp/BSChat/issues">问题反馈</a>
    ·
    <a href="README.md">English</a>
  </p>
</div>

> [!WARNING]
> 模组目前仍处于早期开发阶段，现有版本均为测试版本。请备份重要世界与客户端/服务端数据；在 Minecraft、LeviLamina 或本模组版本发生变化后，不保证先前版本仍然兼容。

Better Speech Chat 将你在游戏里的语音带到身边——按住按键说话，声音经游戏自带数据通道实时传给服务器上的每一个人，服务端把多方语音混合后回传；还可以开启实时语音转文字，把谁在说什么直接显示在屏幕上。一个项目、两套构建：服务端插件 + 客户端模组，从服务器到玩家体验一致。

## 快速开始

> [!IMPORTANT]
> Better Speech Chat 以服务端和客户端两个独立构建分发，两端都必须安装且使用相同的 LeviLamina 基线，否则无法握手。

1. 在服务器上安装 [LeviLamina](https://lamina.levimc.org/)（基线 26.10.14）。
2. 在每位玩家的基岩版客户端安装 LeviLamina 客户端。
3. 安装对应构建：服务端 → `plugins/bschat/`，客户端 → `mods/bschat/`。
4. 重启后进服，按住 **V** 说话，松开结束。

完整安装与配置说明见[快速上手指南](../docs/getting-started.md)。

## 功能特性

- **实时语音** — 进服即聊，近乎无延迟，多人同时说话也不卡。
- **按键说话（PTT）** — 按住 **V** 发言，松开即静音，随时掌控麦克风。
- **超省带宽** — 音频高倍压缩，无人说话时几乎零流量占用。
- **无需开放端口** — 语音走游戏自带的连接通道，玩家端零额外设置。
- **实时字幕** — 服务端本地语音转写，以屏幕字幕呈现，听不清也能看懂。
- **服务端与客户端双端** — 一个项目产出两种构建，部署即用。
- **优雅降级** — 麦克风、模型或网络异常时只隔离自身，不拖垮游戏。
- **国际化** — 面向玩家的文案接入 LeviLamina i18n 体系。

## 本版更新

`v0.0.0` 是首个开发版。当前已完成核心语音引擎、协议与单元测试，服务端与客户端适配层正在推进。完整历史见[更新日志](CHANGELOG.md)。

> [!IMPORTANT]
> 目前发布的仍是测试版本，可能在后续直接进行破坏性的配置或协议更新。请在使用后及时关注更新日志。

## 兼容性

| 端 | 构建 | 安装路径 |
| ------------------ | ---------------- | ------------------------ |
| 服务端（BDS） | 服务端构建 | `plugins/bschat/` |
| 客户端（基岩版） | 客户端构建 | `mods/bschat/` |

以上均面向 **Windows x64** 平台，运行于 **LeviLamina 26.10.14**。

> [!TIP]
> 语音无需开放独立端口，也不用配置 UDP 通道——它复用游戏自带的连接。

## 从源码构建

使用 xmake 在 Windows x64 上构建，通过 `--target_type` 产出服务端或客户端版本。构建命令、输出结构与依赖说明见[源码构建指南](../docs/building.md)。

## 命令

以下命令随开发进度逐步提供（规划中）：

| 命令 | 说明 |
| -------------------------------- | ---------------------------------- |
| `/bsc help` | 显示语音命令帮助。 |
| `/bsc mute <player>` | 强制静音指定玩家。 |
| `/bsc unmute <player>` | 解除指定玩家静音。 |
| `/bsc toggle` | 开关自身麦克风。 |

## 语言

- 服务端：中文、英文
- 客户端：中文、英文

模组面向玩家的文案接入 LeviLamina i18n，翻译资源随双端构建分发。

## 常见问题

### BSChat 是什么？
它是面向 Windows x64 LeviLamina 的 Minecraft 基岩版游戏内实时语音聊天模组。玩家按住按键说话，语音经游戏自带数据通道上行，服务端混音后回传所有在线玩家，可选实时语音转文字字幕。

### 需要开放端口或配置 UDP 通道吗？
不需要。语音走游戏内置的数据通道，玩家与服务端都无需额外网络配置。

### 无人说话时占用带宽吗？
几乎不占用。音频充分压缩，无人说话时下行数据趋近于零。

### 怎么开启字幕？
在服务端开启 `sttEnabled`，并按 `sttModel.libraryPath`、`encoderPath`、`decoderPath`、`joinerPath`、`tokensPath` 提供 sherpa-onnx 流式 Zipformer 模型文件，客户端保持开启字幕即可。模型缺失时语音转文字自动停用，语音主链路不受影响。

### 可以更换说话按键吗？
可以。在客户端 `config.json` 中修改 `pttKey` 即可更改按键说话绑定。

## 开发状态与计划

- 核心语音引擎、协议与单元测试已完成；服务端/客户端适配层开发中。
- 后续将重点完善双端真实环境下的延迟与音频质量，欢迎测试并反馈。
- 后续计划包括近聊/距离音频、环境混响、VAD 自动语音检测与多人房间体系。

> [!TIP]
> **测试重点：** 报告延迟、杂音或字幕问题时，请尽量附上两端构建版本、LeviLamina 版本、日志与麦克风/网络环境。

## 已知限制

- 仍未正式发布，版本之间可能发生破坏性变更。
- 当前不提供位置/距离衰减与环境混响（接口已预留，后续版本实现）。
- VAD 自动语音检测接口已就位，但默认关闭。
- 暂无独立 UDP 通道，暂无多人房间/频道体系。
- v0 末暂不支持移动端/伴侣应用采集。
- Minecraft 或 LeviLamina 更新后，需重新确认兼容性。

如需报告可复现问题，请[提交 Issue](https://github.com/copper-lamp/BSChat/issues)。

## 参与贡献

欢迎通过 Issue 提问、提交 Pull Request。提交前请阅读项目根目录的 `AGENTS.md` 了解开发规范（提交信息头英文、正文中文；禁止 emoji；模块文档与代码一致）。

## 致谢

特别感谢 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 的维护者与社区提供的原生模组开发平台与工具；感谢 [Opus](https://opus-codec.org/)、[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)、[ONNX Runtime](https://github.com/microsoft/onnxruntime) 与 [nlohmann/json](https://github.com/nlohmann/json) 项目为语音编解码、离线流式转写与配置解析提供了基础能力。

## 许可证

BSChat 是以 [GNU Affero 通用公共许可证 v3.0](LICENSE)（可含更高版本）发布的自由软件。详见 `LICENSE`。第三方依赖保留各自许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 与 [licenses/](licenses/) 目录。
