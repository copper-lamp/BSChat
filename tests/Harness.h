#pragma once

#include <cstdio>
#include <string>
#include <vector>

// 极简单测框架：零第三方依赖，纯 host 运行。
// 用法：TEST(用例名) { EXPECT_TRUE(...); EXPECT_EQ(a, b); }
// main 中调用 bsc::test::runAll()，返回非 0 表示存在失败。

namespace bsc::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int failureCount = 0;

inline void reportFailure(const char* file, int line, const std::string& expr) {
    std::fprintf(stderr, "    FAILED %s:%d: %s\n", file, line, expr.c_str());
    ++failureCount;
}

inline int runAll() {
    for (auto const& tc : registry()) {
        std::printf("[ RUN  ] %s\n", tc.name);
        int before = failureCount;
        tc.fn();
        if (failureCount == before) {
            std::printf("[  OK  ] %s\n", tc.name);
        } else {
            std::printf("[ FAIL ] %s (%d assertions failed)\n", tc.name, failureCount - before);
        }
    }
    std::printf("\nTotal failures: %d\n", failureCount);
    return failureCount == 0 ? 0 : 1;
}

} // namespace bsc::test

#define TEST(name) \
    static void name(); \
    static ::bsc::test::Registrar name##_reg_##__LINE__(#name, &name); \
    static void name()

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) ::bsc::test::reportFailure(__FILE__, __LINE__, #cond); \
    } while (0)

#define EXPECT_FALSE(cond) \
    do { \
        if ((cond)) ::bsc::test::reportFailure(__FILE__, __LINE__, #cond); \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        auto va = (a); \
        auto vb = (b); \
        if (!(va == vb)) \
            ::bsc::test::reportFailure(__FILE__, __LINE__, std::string(#a " == " #b)); \
    } while (0)

#define EXPECT_NEAR(a, b, eps) \
    do { \
        double va = (a); \
        double vb = (b); \
        if (va < vb - (eps) || va > vb + (eps)) \
            ::bsc::test::reportFailure(__FILE__, __LINE__, std::string(#a " ~= " #b)); \
    } while (0)
