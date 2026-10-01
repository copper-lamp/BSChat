
<div align="center">
  <h1>Better Speech Chat</h1>
  <p><strong>进服开麦，即刻畅聊。</strong></p>
  <p>面向 LeviLamina 的开源 Minecraft 基岩版游戏内实时语音聊天模组，服务端混音、客户端收发，可选实时语音转写字幕。</p>
  <p>
    <img src="https://img.shields.io/badge/release-v0.1.3--mc26.20-4c8bf5?style=flat-square" alt="BSChat v0.1.3-mc26.20">
    <img src="https://img.shields.io/badge/Minecraft%20Bedrock-Windows%20x64-62b47a?style=flat-square" alt="Windows x64 Minecraft 基岩版">
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue?style=flat-square" alt="AGPL-3.0 许可证"></a>
  </p>
  <p>
    <img src="https://img.shields.io/badge/LeviLamina-26.20.7-7b68ee?style=flat-square" alt="LeviLamina 26.20.7">
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
> 模组目前仍处于早期开发阶段，现有版本均为测试版本。请备份重要世界与客户端/服务端数据。语音协议已固定为 **v1**，后续只做向下兼容的追加式扩展，因此不同模组小版本之间仍可互通；但 Minecraft 或 LeviLamina 版本变化不保证兼容——这取决于游戏网络层与模组加载机制，不在本项目可控范围内。

Better Speech Chat 将你在游戏里的语音带到身边——按住按键说话，声音经游戏自带数据通道实时传给服务器上的每一个人，服务端把多方语音混合后回传；还可以开启实时语音转文字，把谁在说什么直接显示在屏幕上。一个项目、两套构建：服务端插件 + 客户端模组，从服务器到玩家体验一致。

## 快速开始

> [!IMPORTANT]
> Better Speech Chat 以服务端和客户端两个独立构建分发，两端都必须安装且使用相同的 LeviLamina 基线，否则无法握手。

1. 在服务器上安装 [LeviLamina](https://lamina.levimc.org/)（基线 26.20.7）。
2. 在每位玩家的基岩版客户端安装 LeviLamina 客户端。
3. 安装对应构建：服务端 → `plugins/bschat/`，客户端 → `mods/bschat/`。
4. 重启后进服，按住 **V** 说话，松开结束；按 **J** 打开语音设置面板。

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

`v0.1.3` 把服务端与客户端两个构建都迁到 **LeviLamina 26.20 版本线**（SDK `26.20.7`）。语音行为、v1 线路协议与配置格式均未改动，本次只更换宿主 SDK。`v0.1.2` 把语音转文字的模型选择权交给服主：服务端包内新增 `Install-SttModel.cmd`，运行后交互式选择档位，脚本自动下载 sherpa-onnx 运行时与所选模型并写入配置，提供纯中文（极速/标准/高配）与中英、中英粤三语共五档，全部为 sherpa-onnx 在线识别器的真流式模型。服务端不再把识别模型写死为 transducer，新增 `sttModel.modelType` 支持全部在线模型族，旧配置无需改动即可继续使用。`v0.1.1` 把语音协议固定为 **v1** 稳定契约并采用只追加的演进方式，同时补齐双端能力协商：客户端声明自身能力，服务端回传双方共同支持的子集，只有协商通过的功能才会启用，因此新旧版本可以互相连通，可选功能不可用时基础语音照常工作。完整历史见[更新日志](CHANGELOG.md)。

> [!IMPORTANT]
> 目前发布的仍是测试版本，配置项与新增功能仍可能调整；但语音协议已固定为 v1，只做向下兼容的追加式扩展，不会在 v1 内做破坏性变更。请在使用后及时关注更新日志。

## 兼容性

| 端 | 构建 | 安装路径 |
| ------------------ | ---------------- | ------------------------ |
| 服务端（BDS） | 服务端构建 | `plugins/bschat/` |
| 客户端（基岩版） | 客户端构建 | `mods/bschat/` |

以上均面向 **Windows x64** 平台，运行于 **LeviLamina 26.20.7**。

> [!IMPORTANT]
> 发布按 MC 版本线分开，版本号后缀标明这份构建适配哪条线（此处为 `-mc26.20`）。换线的构建无法加载，
> 请选择后缀与玩家所用 MC 版本一致的构建，并保证服务端与客户端取同一条线。

> [!TIP]
> 语音无需开放独立端口，也不用配置 UDP 通道——它复用游戏自带的连接。

## 从源码构建

使用 xmake 在 Windows x64 上构建，通过 `--target_type` 产出服务端或客户端版本。构建命令、输出结构与依赖说明见[源码构建指南](../docs/building.md)。

## 命令

| 命令 | 说明 |
| -------------------------------- | ---------------------------------- |
| `/bsc test` | 在本人客户端跑一次端到端自检，结果写入客户端日志。 |
| `/bsc play <file>` | 把一段 WAV 经语音下行播放（文件放在 `<服务端配置目录>/audio/`，或直接给绝对路径）。 |
| `/bsc stop` | 停止正在播放的 WAV。 |
| `/bsc admin` | 打开管理员面板，仅管理员（OP）可用。 |

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
字幕由**服务端**转写，识别模型**不随包附带**，需要用服务端包里的 `Install-SttModel.cmd` 自行下载。这一步完全可选，不装也不影响语音聊天。

1. 在服务端打开命令行，**进入 `bschat.dll` 所在目录**（通常是 `plugins/bschat/`），运行：

   ```bat
   Install-SttModel.cmd
   ```

2. 屏幕上会列出模型档位，输入编号回车即可选中：

   | 你的情况 | 选 |
   | --- | --- |
   | 玩家基本都说中文 | **2** |
   | 有人中英混说 | **4** |
   | 有粤语玩家 | **5** |
   | 配置较低或带宽有限 | **1** |
   | 纯中文、追求最高精度且 CPU 较强 | **3** |

   下载体积 25–570 MB 不等，过程中会显示进度。装完会看到：

   ```
   安装完成。
   请重启服务端使配置生效，并确认客户端已开启字幕。
   ```

   不想用菜单的话可以直接指定，例如 `Install-SttModel.cmd bilingual-zh-en`。

3. **重启服务端**。模型在启动时加载。
4. 客户端按 **J** 打开语音面板，确认字幕已开启。

模型、运行时和配置都落在模组目录内，不动服务端其他文件。重复运行脚本可以换档位；删掉 `stt/` 目录也不会有影响，那里面只是可重新下载的权重。

完整说明（含排错）见 [docs/getting-started.md](../docs/getting-started.md)。

### 可以更换说话按键吗？
可以。进服后按 **J** 打开语音设置面板即可改绑按键；也可以直接修改客户端 `config.json` 中的 `pttKey` 与 `settingsKey`。

## 开发状态与计划

- 核心语音引擎、协议、双端适配层与单元测试均已实现；下一步是真实设备下的延迟与音频质量调优。
- 后续计划包括近聊/距离音频、环境混响、VAD 自动语音检测与多人房间体系。

> [!TIP]
> **测试重点：** 报告延迟、杂音或字幕问题时，请尽量附上两端构建版本、LeviLamina 版本、日志与麦克风/网络环境。

## 已知限制

- 仍未正式发布；配置项与新增功能可能继续调整，但 v1 语音协议只做向下兼容的追加式扩展。
- 字幕只显示文本，不显示说话者名字。
- 管理员面板目前只暴露服务端语音开关。
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
