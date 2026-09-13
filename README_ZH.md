---
title: Betterlanguagechat
tags:
  - levilamina
  - mod
  - voice-chat
  - minecraft
aliases:
  - Betterlanguagechat
---

<div align="center">

# Betterlanguagechat

**Minecraft 基岩版 · 游戏内实时语音聊天。**

进服即聊，无需打字、无需额外软件、无需开放任何端口。语音经游戏自带数据通道传输，延迟低、无人说话时几乎不消耗带宽，可选实时语音转文字字幕。

[![LeviLamina](https://img.shields.io/badge/LeviLamina-26.40.x-blue)](https://github.com/LiteLDev/LeviLamina)
[![平台](https://img.shields.io/badge/Platform-Server%20%2F%20Client-success)](https://github.com/LiteLDev/LeviLamina)
[![状态](https://img.shields.io/badge/Status-Alpha-orange)](CHANGELOG.md)
[![许可证 CC0-1.0](https://img.shields.io/badge/License-CC0--1.0-lightgrey)](LICENSE)

</div>

> [!NOTE]
> **English version: [README.md](README.md).**

> [!IMPORTANT]
> Betterlanguagechat 目前处于 **alpha 阶段**，可在朋友之间的小规模服务器上试用，但功能与行为仍可能调整。体验前请务必备份你的客户端与服务端。

## 项目简介

Betterlanguagechat 是以 LeviLamina 为载体的基岩版游戏内实时语音聊天模组。同一个项目产出两种构建——**服务端**插件与**客户端**模组——让玩家按下一个按键说话，即可被服务器上的所有人实时听到。

目标很简单：**进服、按住按键、开麦说话。**

## 功能特性

- **实时互通** — 语音近乎即时送达给每一位在线玩家。
- **按键说话（PTT）** — 按住 **V** 发言，松开即静音，随时掌控隐私。
- **高效省资源** — 音频高倍压缩，无人说话时几乎零带宽占用。
- **无需开放端口** — 语音走游戏自带数据通道，玩家端零额外配置。
- **可选实时字幕** — 服务端语音转文字，把对话实时显示在屏幕上，不错过任何内容。
- **服务端与客户端双端** — 从服务器到玩家全程一致体验。

## 安装

Betterlanguagechat 以**两个独立构建**分发，两端都必须安装语音才能生效。

| 构建 | 安装到 | 说明 |
|---|---|---|
| 服务端 | `plugins/voicechat/` | 部署于基岩版专用服务器（BDS） |
| 客户端 | `mods/voicechat/` | 部署于每位玩家的基岩版客户端 |

1. 确保服务端与客户端均运行 [LeviLamina](https://lamina.levimc.org/)（基线 26.40.x）。
2. 在 BDS 服务器安装**服务端**构建。
3. 在每位玩家的基岩版客户端安装**客户端**构建。
4. 全部重启后进入世界即可。

> [!TIP]
> 如果听不到任何声音，先确认你的麦克风对该客户端可用，并核对两侧安装的是正确构建（服务端 / 客户端）。

## 使用方法

游戏内按住 **V** 说话，松开即静音。

| 操作 | 输入 |
|---|---|
| 按键说话 | 按住 **V** |
| 说话 | 按住 **V** 期间 |
| 停止说话 | 松开 **V** |

更多选项（游戏命令、强制静音、字幕开关）通过模组 `config.json` 配置。

## 配置

面向玩家的选项位于模组 `config.json`。

**客户端：**

| 键 | 默认值 | 说明 |
|---|---|---|
| `pttKey` | `V` | 按住说话的按键 |
| `subtitleEnabled` | `true` | 显示语音转文字字幕 |
| `vadEnabled` | `false` | 自动语音检测（接口预留） |

**服务端：**

| 键 | 默认值 | 说明 |
|---|---|---|
| `voiceEnabled` | `true` | 语音总开关 |
| `sttEnabled` | `false` | 开启语音转文字字幕 |
| `whisperModelPath` | *（空）* | 字幕所用的 Whisper 模型文件 |

## 兼容性

| 项目 | 内容 |
|---|---|
| 加载器 | LeviLamina `26.40.*` |
| 平台 | Windows x64 |
| 服务端安装 | `plugins/voicechat/` |
| 客户端安装 | `mods/voicechat/` |

## 常见问题

### 为什么听不到任何声音？
先确认服务端与客户端构建都安装在正确一侧，并检查麦克风是否被其它程序占用。

### 需要开放端口吗？
不需要。语音走游戏自带数据通道。

### 语音会占用很多带宽吗？
几乎不会。无人说话时，模组几乎不发送任何数据。

### 能换说话按键吗？
可以。在客户端 `config.json` 的 `pttKey` 中设置为其它按键即可。

## 项目状态

Betterlanguagechat 正处于 **alpha** 阶段，积极开发中。

- 核心语音引擎、协议与单元测试已就绪。
- 服务端与客户端适配层正在开发。
- 路线图见 [docs](../docs/)，历史记录见 [CHANGELOG.md](CHANGELOG.md)。

## 反馈

发现 bug 或有好的建议？欢迎提交 issue 或 pull request——期待你的贡献。

## 许可证

本项目基于 [CC0-1.0](LICENSE) 许可证开源。详情见 `LICENSE`。