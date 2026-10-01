/*
 * test_harness.h - a minimal test harness written for this project.
 *
 * Why not GoogleTest/Catch2? The project is meant to build on a plain Linux
 * box with only g++ and make. A 60-line harness gives CHECK/RUN_TEST and a
 * non-zero exit code, which is everything the CI script needs.
 *
 * Usage:
 *     #include "test_harness.h"
 *     void test_something() { CHECK_EQ(2 + 2, 4); }
 *     TEST_LIST(test_something, test_other)
 *
 * Every test file lists its tests with TEST_LIST and provides no main().
 * tests/test_main.cpp holds the single main(), which runs the combined
 * registry. That is what allows all five test files to link into one
 * binary: one main(), and each file contributes its own tests.
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace th {

inline int& checks_run()
{
	static int v = 0;
	return v;
}

inline int& checks_failed()
{
	static int v = 0;
	return v;
}

inline std::string& current_test()
{
	static std::string s = "<none>";
	return s;
}

/* Render any streamable value as text for the failure message. */
template <typename T>
std::string to_text(const T& value)
{
	std::ostringstream os;
	os << value;
	return os.str();
}

inline void report(bool ok, const std::string& expr, const char* file, int line)
{
	++checks_run();
	if (ok)
		return;
	++checks_failed();
	std::cout << "    FAIL  " << file << ":" << line << "  " << expr
		  << "\n";
}

} // namespace th

#define CHECK(expr)                                                          \
	th::report(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected)                                           \
	do {                                                                   \
		auto _a = (actual);                                              \
		auto _e = (expected);                                            \
		th::report(_a == _e,                                              \
			   std::string(#actual) + " == " + #expected + "  (got " + \
				   th::to_text(_a) + ", want " +               \
				   th::to_text(_e) + ")",                        \
			   __FILE__, __LINE__);                                  \
	} while (0)

#define CHECK_NE(actual, unexpected)                                         \
	do {                                                                   \
		auto _a = (actual);                                              \
		auto _u = (unexpected);                                          \
		th::report(_a != _u,                                              \
			   std::string(#actual) + " != " + #unexpected,            \
			   __FILE__, __LINE__);                                  \
	} while (0)

#define CHECK_CONTAINS(haystack, needle)                                     \
	do {                                                                   \
		std::string _h = (haystack);                                     \
		std::string _n = (needle);                                       \
		th::report(_h.find(_n) != std::string::npos,                      \
			   std::string(#haystack) + " contains '" + _n + "'",      \
			   __FILE__, __LINE__);                                  \
	} while (0)

/* A test is a plain void function. */
using TestFn = void (*)();

using TestEntry = std::pair<std::string, TestFn>;

/*
 * The registry is a function-local static, so it is constructed on first use
 * rather than during static initialisation. That matters: the TEST_LIST
 * registrars below run before main(), and a namespace-scope vector could not
 * be built before they touch it.
 */
inline std::vector<TestEntry>& registry()
{
	static std::vector<TestEntry> tests;
	return tests;
}

inline void register_test(const char* name, TestFn fn)
{
	registry().emplace_back(std::string(name), fn);
}

/* Runs every registered test. Returns 0 only if all of them pass. */
inline int run_all()
{
	int failed_tests = 0;
	const std::vector<TestEntry>& tests = registry();

	std::cout << "Running " << tests.size() << " test(s)\n";
	for (const auto& t : tests) {
		th::current_test() = t.first;
		const int before = th::checks_failed();
		std::cout << "  [ RUN  ] " << t.first << "\n";
		try {
			t.second();
		} catch (const std::exception& e) {
			++th::checks_failed();
			std::cout << "    FAIL  unexpected exception: " << e.what()
				  << "\n";
		} catch (...) {
			++th::checks_failed();
			std::cout << "    FAIL  unknown exception\n";
		}
		const int added = th::checks_failed() - before;
		if (added == 0)
			std::cout << "  [  OK  ] " << t.first << "\n";
		else
			++failed_tests;
	}

	std::cout << "\n" << th::checks_run() << " checks run, "
		  << th::checks_failed() << " failed\n";
	if (failed_tests == 0)
		std::cout << "ALL TESTS PASSED\n";
	else
		std::cout << "TESTS FAILED\n";
	return failed_tests == 0 ? 0 : 1;
}

/*
 * TEST_LIST(t1, t2, ...) registers the named tests in this translation unit.
 * It must be used exactly once per file, at file scope. There is deliberately
 * no main() here - tests/test_main.cpp owns the single entry point.
 */
namespace th_detail {
struct Registrar {
	Registrar(const char* name, TestFn fn) { register_test(name, fn); }
};
} // namespace th_detail

#define TEST_ENTRY(fn) const th_detail::Registrar th_reg_##fn(#fn, &fn);

#define TEST_LIST(...) __VA_ARGS__

#endif /* TEST_HARNESS_H */
