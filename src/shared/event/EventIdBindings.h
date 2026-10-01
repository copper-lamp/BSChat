#pragma once

#include "ll/api/event/EventId.h"

// LeviLamina 把事件类型放在 inline namespace 里（namespace ll::event::inline client { ... }）。
// EventBus 的条目键是 getEventId<T>()，其默认值取 reflection::type_unprefix_name_v<T>：
// MSVC 的 __FUNCSIG__ 不打印 inline namespace，clang-cl 的 __PRETTY_FUNCTION__ 打印。SDK 由
// MSVC 编译，于是同一个事件在 SDK 与本模组两侧得到不同字符串、哈希也不同，EventBus 里查不到
// 对应条目，emplaceListener 静默返回空监听器（症状：模组加载成功但监听全部失效，日志无报错）。
//
// 因此这里把双端用到的事件逐个绑定到 SDK 侧的规范 ID（inline 段已剥离）。
// 校验方式：从 SDK 的 LeviLamina.dll 里 grep 事件名，SDK 侧一定是无子命名空间的形式，
// 例如 "ll::event::PlayerJoinEvent" / "ll::event::KeyInputEvent"，带 inline 段的写法一律是错的。
// 事件条目本身仍由 SDK 的 hook 型 emitter 创建并转发游戏事件，此处不做任何替代实现。
//
// LeviLamina 的客户端事件头文件没有 include guard（例如 UIRenderEvent.h 直接以 #include 开头），
// 同一编译单元内重复包含会导致类重定义。因此这里只做前置声明，事件类型的定义由使用方各自包含一次。
// 前置声明必须同样带 inline，否则 clang 报 -Winline-namespace-reopened-noninline，
// 且与后续头文件的定义不是同一个命名空间，属于静默的类型不匹配。
namespace ll::event {
inline namespace client {
class ClientJoinLevelEvent;
class ClientExitLevelEvent;
} // namespace client
inline namespace world {
class ClientLevelTickEvent;
class ServerLevelTickEvent;
} // namespace world
inline namespace input {
class KeyInputEvent;
} // namespace input
inline namespace render {
class AfterUIRenderEvent;
} // namespace render
inline namespace player {
class PlayerJoinEvent;
class PlayerDisconnectEvent;
} // namespace player
} // namespace ll::event

namespace ll::event {

template <>
inline constexpr EventIdView getEventId<client::ClientJoinLevelEvent> = EventIdView{"ll::event::ClientJoinLevelEvent"};

template <>
inline constexpr EventIdView getEventId<client::ClientExitLevelEvent> = EventIdView{"ll::event::ClientExitLevelEvent"};

template <>
inline constexpr EventIdView getEventId<world::ClientLevelTickEvent> = EventIdView{"ll::event::ClientLevelTickEvent"};

template <>
inline constexpr EventIdView getEventId<input::KeyInputEvent> = EventIdView{"ll::event::KeyInputEvent"};

template <>
inline constexpr EventIdView getEventId<render::AfterUIRenderEvent> = EventIdView{"ll::event::AfterUIRenderEvent"};

template <>
inline constexpr EventIdView getEventId<player::PlayerJoinEvent> = EventIdView{"ll::event::PlayerJoinEvent"};

template <>
inline constexpr EventIdView getEventId<player::PlayerDisconnectEvent> =
    EventIdView{"ll::event::PlayerDisconnectEvent"};

template <>
inline constexpr EventIdView getEventId<world::ServerLevelTickEvent> = EventIdView{"ll::event::ServerLevelTickEvent"};

} // namespace ll::event
