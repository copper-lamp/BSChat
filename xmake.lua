add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- LeviLamina SDK，按 target_type 拉取服务端或客户端构建（开发基线 26.10.14）
add_requires("levilamina 26.10.14", {configs = {target_type = get_config("target_type")}})
add_requires("levibuildscript")

-- 核心引擎依赖：Opus 编解码（静态链接）、nlohmann-json（header-only，配置序列化）
add_requires("libopus v1.5.2", {configs = {shared = false}})
add_requires("nlohmann_json")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

-- 与 LeviLamina 模板一致的 Windows/Clang 编译标志
function apply_common_windows_flags()
    if is_plat("windows") then
        local vc_tools = os.getenv("VCToolsInstallDir")
        if vc_tools then
            add_linkdirs(path.join(vc_tools, "atlmfc", "lib", "x64"))
        end
        add_defines("NOMINMAX", "UNICODE", "_UNICODE")
        set_exceptions("none") -- 避免与 /EHa 冲突
        add_cxflags("/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
end

-- 核心引擎：纯 C++ 静态库，零 LeviLamina 依赖，可独立单测
target("voicechat-core")
    set_kind("static")
    set_languages("c++20")
    apply_common_windows_flags()
    add_packages("libopus", "nlohmann_json")
    add_includedirs("src", {public = true}) -- 头文件统一以 core/... 引用
    add_files("src/core/**.cpp")
    add_headerfiles("src/core/**.h")

-- 模组本体：按 target_type 编译服务端或客户端适配层
target("voicechat")
    set_kind("shared")
    set_languages("c++20")
    apply_common_windows_flags()
    add_deps("voicechat-core")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_packages("levilamina")
    add_includedirs("src", "src/core", {public = true})
    add_files("src/shared/**.cpp")
    add_headerfiles("src/shared/**.h")
    if is_config("target_type", "server") then
        add_includedirs("src/server", "third_party/sherpa-onnx")
        add_files("src/server/**.cpp")
    else
        add_includedirs("src/client")
        if os.isdir("$(projectdir)/../lib/Dear-OreUI/src") then
            add_includedirs("$(projectdir)/../lib/Dear-OreUI/src")
            add_defines("VOICECHAT_HAS_DEAR_OREUI=1")
        end
        add_files("src/client/**.cpp")
        add_headerfiles("src/client/**.h")
        if is_plat("windows") then
            add_syslinks("ole32", "uuid")
        end
    end

-- 单元测试：host 运行，链接 core 静态库与第三方依赖，不进入默认构建。
-- 额外编译 server/session、server/mixer、server/stt（零 LeviLamina 依赖的纯逻辑层），
-- 覆盖会话切分/限速、混音 tick/待发队列、STT worker 管线单测。
target("voicechat-tests")
    set_default(false)
    set_kind("binary")
    set_languages("c++20")
    apply_common_windows_flags()
    add_deps("voicechat-core")
    add_packages("libopus", "nlohmann_json")
    add_includedirs("src", "tests", "third_party/sherpa-onnx")
    add_files(
        "tests/**.cpp",
        "src/server/session/*.cpp",
        "src/server/mixer/*.cpp",
        "src/server/stt/*.cpp",
        "src/server/entry/ServerRuntime.cpp",
        "src/server/admin/*.cpp",
        "src/client/audio/AgcProcessor.cpp",
        "src/client/entry/ClientRuntime.cpp",
        "src/client/input/Triggers.cpp",
        "tests/client/**.cpp"
    )
