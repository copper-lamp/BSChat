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

## 依赖关系

Dear-OreUI 是 voicechat 的可选运行时能力，不是硬依赖。voicechat 通过 `LoadLibraryW` 加载 `DearOreUI.dll`；缺失、版本不匹配或 API 查询失败时，语音模组应继续正常加载和启用。客户端 tooth 清单只声明 LeviLamina 依赖，不声明 Dear-OreUI 硬依赖。

## 备注

- 已确认参考源码包含公开 C ABI、Settings page scope 以及 page/panel 注册接口。
- 已从 GitHub Release `v0.1.2` 下载并核对 `DearOreUI-windows-x64.zip`；其中的 `DearOreUI.dll` 导出 `DearOreUI_QueryApi`。下载物未提交到 Git，构建时应由部署环境提供。
- 客户端新增可选动态加载器：从 `mods/DearOreUI/DearOreUI.dll` 加载 ABI，协商 protocol 1，注册 `voicechat` 模组并注册 `PageScope::Settings` 面板。注册已延迟到 voicechat enable 阶段，确保 DearOreUI 已完成自身 enable 和页面运行时初始化。Dear-OreUI 不存在或 API 未就绪时加载器返回 false，不阻塞语音模组。
- 已保存 UI 注册句柄，并在卸载时先 `unregisterUi`、再 `unregisterMod`，最后释放 DLL。
- 已通过 VS Developer Command Prompt 完成 client target 构建，产物位于 `bin/voicechat/voicechat.dll`，可以开始客户端安装测试。
- 完整实现仍需要真实客户端验证 DLL 加载、页面注入和卸载顺序；API 变更方法必须在游戏主线程调用。设置页只有在 DearOreUI runtime 已启用并成功识别 Settings 页面时才会挂载，启动日志中的模组加载成功不等同于页面已显示。
- 在获得这些材料前，不能宣称已完成原版 Settings 运行时注入。
- 该模块与 `ClientRuntime`、配置和 i18n/UI 数据模型关联；Dear-OreUI 只负责页面承载，语音设备和网络状态仍由客户端核心负责。
