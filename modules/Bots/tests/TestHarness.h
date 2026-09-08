/*
 * 2026 BFA-HavenCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef Bots_Tests_TestHarness_h__
#define Bots_Tests_TestHarness_h__

#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace BotsTest
{
    template <typename T>
    inline std::string Describe(T const& value)
    {
        if constexpr (std::is_same_v<T, std::string>)
            return "\"" + value + "\"";
        else if constexpr (std::is_same_v<T, char const*>)
            return std::string("\"") + value + "\"";
        else if constexpr (std::is_enum_v<T>)
            return std::to_string(static_cast<long long>(value));
        else if constexpr (std::is_arithmetic_v<T>)
            return std::to_string(value);
        else
            return "<value>";
    }

    struct TestCase
    {
        std::string Name;
        std::function<void()> Run;
    };

    inline std::vector<TestCase>& Registry()
    {
        static std::vector<TestCase> registry;
        return registry;
    }

    inline int& FailureCount()
    {
        static int failures = 0;
        return failures;
    }

    inline std::string& CurrentTest()
    {
        static std::string current;
        return current;
    }

    struct Registrar
    {
        Registrar(std::string name, std::function<void()> fn)
        {
            Registry().push_back({ std::move(name), std::move(fn) });
        }
    };

    inline void ReportFailure(char const* file, int line, std::string const& what)
    {
        ++FailureCount();
        std::printf("    FAIL %s:%d  %s  [%s]\n", file, line, what.c_str(), CurrentTest().c_str());
    }

    inline int RunAll()
    {
        int passed = 0;
        for (TestCase const& test : Registry())
        {
            CurrentTest() = test.Name;
            int const before = FailureCount();
            test.Run();
            if (FailureCount() == before)
                ++passed;
            else
                std::printf("  failed: %s\n", test.Name.c_str());
        }

        std::printf("\n%d test cases, %d passed, %d failed, %d assertions failed\n",
            static_cast<int>(Registry().size()), passed, static_cast<int>(Registry().size()) - passed, FailureCount());

        return FailureCount() == 0 ? 0 : 1;
    }
}

#define BOTS_TEST(name)                                                                          \
    static void name();                                                                          \
    static ::BotsTest::Registrar registrar_##name(#name, name);                                  \
    static void name()

#define BOTS_CHECK(cond)                                                                         \
    do { if (!(cond)) ::BotsTest::ReportFailure(__FILE__, __LINE__, "expected: " #cond); } while (false)

#define BOTS_CHECK_EQUAL(actual, expected)                                                       \
    do {                                                                                         \
        auto const botsActual = (actual);                                                        \
        auto const botsExpected = (expected);                                                    \
        if (!(botsActual == botsExpected))                                                       \
            ::BotsTest::ReportFailure(__FILE__, __LINE__,                                        \
                std::string(#actual) + " == " + #expected + " (got " +                           \
                ::BotsTest::Describe(botsActual) + ", want " + ::BotsTest::Describe(botsExpected) + ")"); \
    } while (false)

#endif // Bots_Tests_TestHarness_h__
