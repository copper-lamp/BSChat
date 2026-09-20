# Client 模块

## 需求
完成麦克风采集、按键/VAD 触发、Opus 编码上传、混音流播放、抖动缓冲和字幕展示，并接入 i18n/UI 生命周期。

## 实际状态
客户端入口已按 LeviLamina 26.10.14 的已确认签名接入：`ClientMod.cpp` 使用 `LL_REGISTER_MOD` 注册 load/enable/disable/unload，并监听 `ClientJoinLevelEvent`、`ClientExitLevelEvent`、`ClientLevelTickEvent` 与 `KeyInputEvent`。运行时通过 `GamePacketTransport`、本地玩家状态适配器和 steady clock 注入 `ClientRuntime`；PTT 仅依据配置虚拟键码切换 talking 状态。未加入任何 UI、ImGui 或 OreUI 假设。

当前仍未完成麦克风采集、播放、字幕 UI 和设备管理，因此不能宣称客户端功能链路完整。WASAPI 当前仅完成默认设备探测。Dear-OreUI Settings 面板已完成可选动态加载和注册，并已成功完成 client target 构建；真实客户端显示效果仍待安装测试。

## 风险与 TODO
- Ninja 锁和 ATL 头文件阻塞已排除；ATL `atls.lib` 也已确认存在，但 LeviLamina 包链接阶段仍报告 `LNK1104 atls.lib`，需修复 xmake/levibuildscript 的 LIB 传递或链接目录。
- 需要真实 Bedrock 客户端设备联调，确认音频线程和主线程边界。
- 需要实现 WASAPI `IAudioClient` 采集/渲染、Opus 上下行接线、设备失败提示、热切换和资源释放。
- 需要实现字幕数据模型、i18n 文案、图标资源与 HUD 渲染。
- 已取得 `D:\BSChat\lib\Dear-OreUI` 参考源码，并从 GitHub Release `v0.1.2` 核对 `DearOreUI.dll` 导出 `DearOreUI_QueryApi`。客户端新增可选动态加载器：尝试加载 `mods/DearOreUI/DearOreUI.dll`，注册 `voicechat` 的 Settings 面板；缺少 Dear-OreUI 时不阻塞语音模组。真实客户端页面注入和卸载顺序仍需验收。
- 客户端与服务端协议能力协商、位置上报及双端互聊仍需真机验收。
