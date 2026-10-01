/*
 * test_main.cpp - the single entry point of the test suite.
 *
 * Every test file registers its own tests with TEST_LIST and provides no
 * main(). This file owns the one main(), so all five test translation units
 * link into a single binary. If a test file were given its own main(), the
 * link step would fail with a duplicate main - which is the whole reason this
 * file exists.
 *
 * Run from the repository root, because the tests write their temporary files
 * into build/:
 *     make -C tests
 *     ./build/run_tests
 */
#include "test_harness.h"

int main()
{
	return run_all();
}
