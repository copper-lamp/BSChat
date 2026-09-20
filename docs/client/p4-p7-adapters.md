# 客户端 P4-P7 适配层

## 需求

P4-P7 需要把核心音频管线接到 Windows WASAPI、LeviLamina 客户端生命周期和界面层。适配必须使用仓库中实际可见的 SDK API，不以猜测的 ImGui、Dear-OreUI 或 Bedrock 客户端类名冒充完成。

## 架构

当前 xmake 可见的 LeviLamina 包只声明 `target_type=client`，并拉取 `bedrockdata ...-client.17`；仓库 checkout 中没有 `libs/third_party/SDK`、ImGui、Dear-OreUI 头文件，也没有客户端事件/渲染 API。现已加入 `src/client/audio/WasapiAudio.*`：它通过 Windows `MMDeviceEnumerator` 真实探测默认捕获/播放端点，向上层返回 HRESULT 和明确状态。该边界不伪造 PCM stream、游戏线程或 UI 回调。

P4-P7 后续应分别注入：真实客户端 tick/input 生命周期、WASAPI capture/render worker、UI backend，以及 UI 状态到配置/传输的绑定。依赖均应通过构造函数接口注入，避免核心库依赖平台 SDK。

## 备注

- 当前已确认的真实 API：xmake 的 `levilamina` client 配置、Windows WASAPI `IMMDeviceEnumerator`（系统 SDK）。
- 当前未确认：`libs/third_party/SDK`（路径不存在）、LeviLamina client event/renderer、ImGui、Dear-OreUI。故没有创建假头文件或猜测符号。
- Spike blocker：需要提供与 26.10.14 对应的客户端 SDK/headers，或锁定公开的客户端事件和渲染扩展包；拿到后才能实现加载/卸载、输入、ImGui/OreUI 绘制和真机验证。
- WASAPI 探测不是录放音完成证明；仍需设备格式协商、IAudioClient3/IAudioCaptureClient/IAudioRenderClient、线程退出和游戏卸载测试。
