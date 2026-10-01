# STT 模型选型与安装

## 需求

### 背景

服务端 STT 当前硬编码只支持一类模型：流式 Zipformer transducer（`SherpaStt.cpp:110-113` 只填 `transducer.encoder/decoder/joiner`）。该模型是 2023 年初发布的 `streaming-zipformer-bilingual-zh-en-2023-02-20`，中文能力弱，且没有任何自动化部署链路——模型与运行库全靠人工放置到 `stt-download/stage/`。

### 目标

1. 用户可自行选择 STT 模型档位，覆盖「极速 / 标准 / 高精度 / 中英双语 / 粤语」等不同需求。
2. 选择后自动完成下载、解压、校验、配置写入，用户重启服务端即可用。
3. 支持 sherpa-onnx 在线识别器支持的全部模型族，而非仅 transducer。
4. 模型缺失时静默降级（现有行为）不破坏语音主链路。

### 非目标

- 本期不做离线（offline）识别器通路。FireRed / SenseVoice / Paraformer-large 等均需 `SherpaOnnxCreateOfflineRecognizer`（`c-api.h:1269`），属第二期范围。
- 不做 VAD 端点检测接入。
- 不做 hotwords 上下文偏置（`c-api.h` 支持 `hotwords_file`，但需先有可注入的词表来源）。

### 关键约束（决定选型）

`c-api.h:242-279` 的 `SherpaOnnxOnlineModelConfig` 只暴露五个模型族：

| 字段 | 模型族 |
|---|---|
| `transducer` | encoder / decoder / joiner 三件套 |
| `paraformer` | encoder / decoder 两件套 |
| `zipformer2_ctc` | 单个 `model` |
| `nemo_ctc` | 单个 `model` |
| `t_one_ctc` | 单个 `model` |

`c-api.h:235` 明确要求「set only one of」。**注意 `fire_red_asr_ctc` 只存在于 Offline 配置（`c-api.h:1115`），在线侧不可用**——这是选型的硬边界。

## 架构

### 一、模型族配置化

`SttModelConfig`（`Config.h:22-30`）原为 transducer 硬编码形态，扩展为按模型族装配：

```
SttModelConfig
  ├─ modelType   "transducer" | "paraformer" | "zipformer2_ctc"
  ├─ encoderPath / decoderPath / joinerPath
  ├─ modelPath        // zipformer2_ctc 用（单文件）
  ├─ tokensPath
  ├─ libraryPath
  ├─ threads / partialIntervalMs
```

`modelType` 缺省为 `transducer`，保证旧配置文件零改动继续工作。

`SherpaStt` 构造时按 `modelType` 走不同装配分支，只填对应字段，其余保持零值（`c-api.h:1070-1103` 注释说明未配置的族会被忽略）。

### 二、脚本与运行时目录

```
<bschat 模组目录>/
  ├─ Install-SttModel.cmd              # 本脚本，模组根即脚本所在目录
  ├─ stt/
  │   ├─ runtime/                      # sherpa DLL + onnxruntime（同目录，依赖解析需要）
  │   │   ├─ sherpa-onnx-c-api.dll
  │   │   └─ onnxruntime.dll
  │   └─ models/<模型ID>/
  │       ├─ ...onnx
  │       └─ tokens.txt
  └─ config/bschat.json                # 脚本写入 sttModel 段
```

`SherpaStt.cpp:72-73` 使用 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`，因此 `sherpa-onnx-c-api.dll` 与 `onnxruntime.dll` **必须同目录**，否则依赖解析失败。

相对路径基准为 `modRoot`（`ServerMod.cpp:391`，即 `config/` 的父目录），脚本写入相对路径以保证可移植。脚本用 `%~dp0` 定位自身目录作为 `ModRoot`，与该基准一致——即模型落在 bschat 模组目录内，而不是它的上级目录。

### 三、模型菜单

| ID | 语种 | 体积(int8) | RTF | modelType | 定位 |
|---|---|---|---|---|---|
| `zh-14m` | 中 | 25 MB | 0.038 | zipformer2_ctc | 极速，低配服务器 |
| `zh-standard` | 中 | 155 MB | 0.15 | transducer | 纯中文标准 |
| `zh-xlarge` | 中 | 570 MB | 0.46 | transducer | 纯中文高精度 |
| `bilingual-zh-en` | 中英+方言 | 227 MB | 0.15 | paraformer | **默认**，国际服 |
| `trilingual-zh-cantonese-en` | 中英粤+方言 | 229 MB | 0.14 | paraformer | 粤语服 |

体积为打包后 `tar.bz2` 大小；RTF 取自 sherpa-onnx 官方实测（单线程）。

### 四、脚本行为

`Install-SttModel.cmd`，随服务端发布包分发，不嵌入 BDS 进程——下载解压是分钟级操作，必须与 BDS 分离。

部署目录只分发这一个文件，不保留辅助脚本。它是双格式文件：marker 之前是 cmd 引导层，marker 之后是内嵌的 PowerShell 5.1 实现。运行时先把 payload 抽取到 `%TEMP%` 下的临时 `.ps1` 执行，结束后删除，因此目录里不留残留。

流程：展示菜单 → 选择 → 下载（带进度）→ 解压 → 校验必需文件 → 写配置 → 提示重启。

传参：`Install-SttModel.cmd zh-14m` 静默安装指定档位，供自动化场景使用。

cmd 引导层有两处必须遵守的约束（均为实测踩坑）：

1. marker 之前的行**必须纯 ASCII**。cmd.exe 按控制台代码页逐字节解析脚本，注释里的非 ASCII 字节会破坏下一行的 `%VAR%` 展开。
2. 文件**不能带 UTF-8 BOM**。BOM 会被当作命令，`@echo off` 变成 `'﻿@echo' 不是内部或外部命令`。
3. 转发参数用 `goto` 标签而非 `if/else` 块。括号块内 cmd 在解析期就展开全部 `%VAR%`，相邻的 `-ModRoot` 值会与后一个参数粘连。

## 执行

### 阶段一：配置与运行时

1. `Config.h` / `Config.cpp`：`SttModelConfig` 加 `modelType`、`modelPath`；JSON 读写同步。`ServerMod.cpp:392-401` 的 resolve 补 `modelPath`。
2. `SherpaStt.h` / `.cpp`：`Options` 同步加字段；构造按 `modelType` 装配；`modeling_unit` 由固定 `cjkchar` 改为随模型族选择。
3. `ServerRuntime.cpp:387-403`：透传新字段。

### 阶段二：安装脚本

1. 模型清单以 PowerShell 数据表描述（ID / 名称 / 语种 / 体积 / RTF / 下载 URL / 必需文件 / modelType / 文件到配置字段的映射）。
2. 下载走 `Invoke-WebRequest`，显示进度百分比。
3. 解压用 `tar.exe`（Windows 10+ 内置），无需额外依赖。
4. 配置写入用 `ConvertFrom-Json` / `ConvertTo-Json`，`sttModel` 整段重建以清掉上次安装的残留路径。

### 阶段三：分发与文档

1. `xmake.lua` `after_build`：服务端构建时把 `scripts/Install-SttModel.cmd` 拷到模组根目录，不建 `scripts/` 子目录。其余 `scripts/*.ps1` 是构建与冒烟测试用的开发工具，不进发布包。
2. `THIRD_PARTY_NOTICES.md` 补录各模型来源与许可状态。
3. `docs/getting-started.md` 补安装步骤。
4. `CHANGELOG.md` 记录变更。

## 备注

### 与其它模块的关系

- `IStt`（`core/pipeline/IStt.h`）接口不变，`SherpaStt` 仍是唯一实现，混音层（`ServerMixer.cpp:98-116`）与协议层无感知。
- `SttModelConfig` 属 `bschat-core`，被 `ServerMod`（配置加载）与 `ServerRuntime`（引擎装配）共用，两处需同步改。
- 脚本不参与 xmake 编译，只做文件分发。

### 许可状态（已确认）

设计文档 `bschat-v1-design.md:189` 要求「具体模型许可需随模型文件核对记录」。已于 2026-10-01 逐项核实上游模型注册页：

| 模型族 | 上游 | 许可 |
|---|---|---|
| icefall zipformer（档位 2/3） | k2-fsa/icefall | Apache-2.0 |
| icefall zipformer CTC（档位 1） | k2-fsa/icefall | Apache-2.0 |
| streaming-paraformer（档位 4） | damo/speech_paraformer_asr_nat-zh-cn-16k-common-vocab8404-online | Apache-2.0 |
| streaming-paraformer（档位 5） | dengcunqin/speech_paraformer-large_asr_nat-zh-cantonese-en-16k-vocab8501-online | Apache-2.0 |
| sherpa-onnx 运行时 | k2-fsa/sherpa-onnx | Apache-2.0 |

依据：两个 paraformer 上游的 ModelScope 模型页均标注「Apache License 2.0」，双语模型的 Hugging Face 卡片带 `apache-2.0` 标签。`k2-fsa/sherpa-onnx` 发布的 ONNX 转换沿用原许可。

BSChat 不分发模型权重，只在安装时下载到用户机器，因此上述声明是告知性质。详见 `THIRD_PARTY_NOTICES.md`。

### 风险

- `third_party/sherpa-onnx/c-api.h` 为 1.13.8 版本。菜单中 `zh-standard` / `zh-xlarge` 为 2025-06 模型，导出时使用动态 shape（文件名无 `chunk-16-left-128` 后缀），**在 1.13.8 上能否正常流式解码未经验证**。
- `paraformer` 族**不支持时间戳**（官方文档明示），`result->timestamps` 为空。当前代码不使用时间戳，无影响。
- `resample48kTo16k`（`SherpaStt.cpp:192-200`）硬编码 48k→16k 三点平均，与 `config.audio.sampleRate` 无联动。改动音频采样率会导致静默识别错误，本期不处理。
- 单 worker 线程串行消费多说话者，`maxQueued=64` 超限时丢弃最旧 `Feed` 帧（丢音频而非降级）。本期不处理。
- 各模型在真实游戏语音（背景音乐、键盘声、多人）下的准确率**未实测**，菜单排序依据的是通用语料基准。

### TODO

- 阶段二：离线识别器通路（FireRed asr2 CTC / AED、SenseVoice），用于 PTT 与 VAD 端点后的 final 结果重算。
- hotwords 上下文偏置，玩家 ID 与服务器专有名词注入。
- 降采样与 `audio.sampleRate` 联动。
- 多说话者并发识别（当前单 worker 串行）。
