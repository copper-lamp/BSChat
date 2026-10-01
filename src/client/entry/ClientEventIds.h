#pragma once

#include "ll/api/event/EventId.h"

// LeviLamina 的客户端事件头文件没有 include guard（例如 UIRenderEvent.h 直接以
// #include 开头），同一编译单元内重复包含会导致类重定义。因此这里只做前置声明，
// 事件类型的定义由使用方各自包含一次。
// 26.20.7 起这些命名空间是 inline namespace（ll::event::inline client），
// 前置声明必须同样带 inline，否则 clang 报 -Winline-namespace-reopened-noninline，
// 且与后续头文件的定义不匹配。
namespace ll::event {
inline namespace client {
class ClientJoinLevelEvent;
class ClientExitLevelEvent;
} // namespace client
inline namespace world {
class ClientLevelTickEvent;
} // namespace world
inline namespace input {
class KeyInputEvent;
} // namespace input
inline namespace render {
class AfterUIRenderEvent;
} // namespace render
} // namespace ll::event

// LeviLamina 发布包由 MSVC 编译，内置事件 ID 取自 MSVC 的 __FUNCSIG__，形如
// "ll::event::client::ClientJoinLevelEvent"，保留 inline namespace 前缀（client/world/input/render）。
// 本模组由 clang-cl 编译，__PRETTY_FUNCTION__ 会省略 inline namespace，得到
// "ll::event::ClientJoinLevelEvent"。两者 FNV1a 哈希不同，EventBus 中不存在对应事件条目，
// addListener 会直接返回 false，导致所有监听器注册失败。
// 这里把客户端各事件的 getEventId 显式绑定到 SDK 侧的规范 ID，供客户端所有编译单元共用；
// 新增客户端事件监听时必须在此登记。
// 事件条目本身仍由 SDK 的 hook 型 emitter 创建并转发游戏事件，此处不做任何替代实现。
namespace ll::event {

template <>
inline constexpr EventIdView getEventId<client::ClientJoinLevelEvent> =
    EventIdView{"ll::event::client::ClientJoinLevelEvent"};

template <>
inline constexpr EventIdView getEventId<client::ClientExitLevelEvent> =
    EventIdView{"ll::event::client::ClientExitLevelEvent"};

template <>
inline constexpr EventIdView getEventId<world::ClientLevelTickEvent> =
    EventIdView{"ll::event::world::ClientLevelTickEvent"};

template <>
inline constexpr EventIdView getEventId<input::KeyInputEvent> = EventIdView{"ll::event::input::KeyInputEvent"};

template <>
inline constexpr EventIdView getEventId<render::AfterUIRenderEvent> =
    EventIdView{"ll::event::render::AfterUIRenderEvent"};

} // namespace ll::event
