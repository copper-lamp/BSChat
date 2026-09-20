# 客户端 P4 核心层

## 需求
提供可在 host 编译的输入触发、AGC、握手/PTT/位置上报逻辑，并避免伪造平台 SDK 调用。

## 架构
`ClientRuntime` 只依赖客户端传输接口 `IClientTransport`（当前为 `pipeline::ITransport` 的兼容别名）、`IPlayerState` 和 `IClock`。`PttTrigger`/`VadTrigger` 将输入状态转换为统一回调；`AgcProcessor` 对 PCM 做 RMS 目标增益。握手成功后发送 PTT 控制、音频帧和 `PosUpdateMessage`。

## 备注
WASAPI、ImGui、Dear-OreUI 和游戏玩家状态尚未在此层直接实现，需由载具 adapter 注入；当前不声明真机验证。客户端 host 测试覆盖触发器、AGC 和握手；本次使用 LLVM clang++ 直接编译并运行通过。xmake 全量目标仍受本机 ninja 包 filelock 错误阻塞。传输仍复用 `pipeline::ITransport`，后续可提供命名兼容的 `IClientTransport` 别名而不改变协议。
