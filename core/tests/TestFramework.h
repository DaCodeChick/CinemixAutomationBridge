// Minimal zero-dependency test framework for the Cinemix core tests.
#ifndef CINEMIX_TEST_FRAMEWORK_H
#define CINEMIX_TEST_FRAMEWORK_H

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace testfw {

struct Failure {
    std::string test;
    int line;
    std::string message;
};

struct Registry {
    std::vector<std::pair<std::string, std::function<void()> > > tests;
    std::vector<Failure> failures;
    std::string current;

    static Registry& instance() {
        static Registry r;
        return r;
    }

    void fail(int line, const std::string& msg) {
        failures.push_back(Failure{current, line, msg});
        std::printf("  FAIL %s:%d: %s\n", current.c_str(), line, msg.c_str());
    }

    int runAll(const char* filter) {
        int ran = 0, passed = 0;
        for (size_t i = 0; i < tests.size(); ++i) {
            if (filter && tests[i].first.find(filter) == std::string::npos) continue;
            ++ran;
            current = tests[i].first;
            const size_t failBefore = failures.size();
            std::printf("[ RUN  ] %s\n", current.c_str());
            tests[i].second();
            if (failures.size() == failBefore) {
                std::printf("[  OK  ] %s\n", current.c_str());
                ++passed;
            } else {
                std::printf("[ FAIL ] %s\n", current.c_str());
            }
        }
        std::printf("\n%d/%d test cases passed, %d failure(s)\n",
                    passed, ran, int(failures.size()));
        return failures.empty() ? 0 : 1;
    }
};

} // namespace testfw

#define TESTFW_CONCAT_(a, b) a##b
#define TESTFW_CONCAT(a, b) TESTFW_CONCAT_(a, b)

#define TEST_CASE(name)                                                        \
    static void TESTFW_CONCAT(test_fn_, __LINE__)();                           \
    static const bool TESTFW_CONCAT(test_reg_, __LINE__) = []() -> bool {      \
        ::testfw::Registry::instance().tests.push_back(                        \
            std::make_pair(name, &TESTFW_CONCAT(test_fn_, __LINE__)));         \
        return true;                                                           \
    }();                                                                       \
    static void TESTFW_CONCAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond))                                                           \
            ::testfw::Registry::instance().fail(__LINE__, "CHECK failed: " #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        if (!((a) == (b))) {                                                   \
            std::ostringstream oss;                                            \
            oss << "CHECK_EQ failed: " #a " == " #b "  (values: " << (a)       \
                << " vs " << (b) << ")";                                       \
            ::testfw::Registry::instance().fail(__LINE__, oss.str());          \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        const double va = double(a), vb = double(b), ve = double(eps);         \
        if (va < vb - ve || va > vb + ve) {                                    \
            std::ostringstream oss;                                            \
            oss << "CHECK_NEAR failed: " #a " ~= " #b "  (values: " << va      \
                << " vs " << vb << ", eps " << ve << ")";                      \
            ::testfw::Registry::instance().fail(__LINE__, oss.str());          \
        }                                                                      \
    } while (0)

#endif // CINEMIX_TEST_FRAMEWORK_H
