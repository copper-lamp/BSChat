# Dear-OreUI 集成

## 需求

在不 patch Minecraft 内部 Settings ABI 的前提下，为语音聊天提供可选的原版 OreUI 设置页面或设置面板，并在 Dear-OreUI 未安装、版本不匹配或运行时尚未就绪时保持客户端可启动。

## 架构

Dear-OreUI 参考源码位于本工作区外的 `D:\BSChat\lib\Dear-OreUI`。其公开集成边界为：

```text
DearOreUI_QueryApi(protocol)
    -> IDearOreUIApi
    -> registerMod(ModManifest)
    -> PageScope::Settings
    -> registerPage / registerPanel / registerComponent
```

公开 C ABI 头文件是 `src/bridge/DearOreUIBridge.h`，C++ 门面是 `src/api/IDearOreUIApi.h`。API 文档明确要求变更方法在游戏主线程调用。该 API 支持 `PageScope::Settings`，但它不是 LeviLamina 原版 Settings 内部 ABI 的直接暴露，因此本项目不直接调用 `OreUI::EntryPoints::Settings` 或 patch游戏代码。

当前语音聊天仓库没有把 Dear-OreUI 源码复制进 `src`，也没有链接未确认版本的 DLL。这样可以避免因为缺少 Dear-OreUI mod、ABI 版本不一致或 DLL 搜索路径错误而导致客户端加载失败。

## 备注

- 已确认参考源码包含公开 C ABI、Settings page scope 以及 page/panel 注册接口。
- `D:\BSChat\lib\Dear-OreUI` 当前未提供可供本项目直接链接的匹配 `DearOreUI.dll`/`.lib`；参考项目自身的 xmake target 负责生成其 native mod。
- 完整实现仍需要确定 Dear-OreUI release/commit、协议版本、匹配 DLL 部署位置，以及客户端真实测试环境。
- 在获得这些材料前，不能宣称已完成原版 Settings 运行时注入。
- 该模块与 `ClientRuntime`、配置和 i18n/UI 数据模型关联；Dear-OreUI 只负责页面承载，语音设备和网络状态仍由客户端核心负责。
