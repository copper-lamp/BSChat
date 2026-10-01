# Getting Started with BSChat

BSChat is distributed as **two separate builds**: a **server** plugin and a
**client** mod. Both must be installed and running on the same LeviLamina baseline for
voice to work.

Common prerequisites:

- [LeviLamina](https://lamina.levimc.org/) `26.51.6` on both ends.
- Windows x64.

Releases are published per Minecraft version line, and the version suffix names the line a build
targets — `v0.1.3-mc26.51` is the **MC 26.51.x** line. Pick the release whose suffix matches the
Minecraft version you run; a build from another line will not load.

## 1. Install the server build

1. Set up LeviLamina on your Bedrock Dedicated Server.
2. Copy the **server** build into `plugins/bschat/`.
3. Restart the server.

## 2. Install the client build

1. Install LeviLamina on each player's Bedrock client.
2. Copy the **client** build into `mods/bschat/`.
3. Restart the game.

> [!IMPORTANT]
> Verify you installed the correct build on the correct side. A server plugin will not
> load in `mods/`, and a client mod will not load in `plugins/`.

## 3. Talk

Join the world and **hold V** to speak; release it to go quiet.

If you hear nothing, check:

- The microphone is available to your client and not occupied by another program.
- Both the server and client versions are compatible (same LeviLamina baseline).

## 4. Optional: enable subtitles

Subtitles are produced by the **server**, from a speech-to-text model. That model is **not bundled**
— it is downloaded by an installer that ships in the server package. This step is entirely optional;
voice chat works without it.

### 4.1 Run the model installer

On the server, open a command prompt **in the same folder** that contains `bschat.dll`
(for example `plugins/bschat/`) and run:

```bat
Install-SttModel.cmd
```

You will see a menu of model tiers. Pick one by typing its number and pressing Enter:

```
  BSChat STT 模型安装
  --------------------------------------------------------------
  当前状态: 未配置模型

  [1] 极速 · 纯中文      中文 · 25 MB · RTF 0.038
      体积最小，低配服务器或带宽受限时使用

  [2] 标准 · 纯中文      中文 · 155 MB · RTF 0.15
      14k 小时中文语料训练，纯中文场景推荐

  [3] 高配 · 纯中文      中文 · 570 MB · RTF 0.46
      纯中文精度天花板，需要较强的服务端 CPU

  [4] 中英双语           中文 / 英语 / 多种方言 · 227 MB · RTF 0.15
      国际服默认档，可处理中英混说

  [5] 中英粤三语         中文 / 粤语 / 英语 · 229 MB · RTF 0.14
      需要粤语支持时使用

  [0] 取消

  请选择模型编号:
```

Which tier to choose:

| Situation | Tier |
|---|---|
| Most players speak Chinese | **2** |
| Players mix Chinese and English | **4** |
| Cantonese speakers | **5** |
| Weak CPU or limited bandwidth | **1** |
| Chinese only, accuracy matters most, strong CPU | **3** |

The download is 25–570 MB depending on the tier. Progress is shown as it runs.

On success you will see:

```
==> 写入配置
    已更新 config/bschat.json
    modelType = paraformer

安装完成。
请重启服务端使配置生效，并确认客户端已开启字幕。
```

To install without the menu — useful for scripted setup — pass the tier id:

```bat
Install-SttModel.cmd bilingual-zh-en
```

Re-running the installer lets you switch tiers later. It rebuilds the `sttModel` section of the
config, so the paths always point at the model you just chose.

### 4.2 What got installed

Everything lands inside the mod folder; nothing else on the server is touched.

```
plugins/bschat/
  ├─ bschat.dll
  ├─ Install-SttModel.cmd
  ├─ stt/
  │   ├─ runtime/                 sherpa-onnx + onnxruntime DLLs
  │   └─ models/bilingual-zh-en/  the model you picked
  └─ config/bschat.json           sttModel section now filled in
```

Re-downloading the same tier skips the download. `stt/` is safe to delete at any time — it only
holds re-downloadable weights.

### 4.3 Finish up

1. **Restart the server.** The model is loaded at startup.
2. In the client, open the voice panel with **J** and make sure subtitles are on.

While a player talks, their text appears on everyone's screen. A line marked `[stt]` in the server
log confirms transcription is running.

### 4.4 If it does not work

Check the server log for a line like:

```
[stt] engine: sttEnabled=true available=true ...
```

- **`available=false`** — the model or runtime is missing. Re-run the installer.
- **No `[stt]` line at all** — speech-to-text is disabled in `config/bschat.json`; set
  `sttEnabled` to `true`.
- **Subtitles show but with no player name** — the display name could not be read at handshake time.
  The text is still correct.

Speech-to-text failing never affects voice chat.

## Next steps

- Tune options in `config.json`.
- Build from source: see [building](building.md).
- Review current implementation status: see [bschat-fullscope-execution.md](bschat-fullscope-execution.md).
- Report issues or suggest features by opening an issue.