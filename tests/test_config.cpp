/*
 * test_config.cpp - FR-14: the configuration file is parsed, comments and
 * junk lines are tolerated, and broken values never crash the monitor.
 */
#include "config.h"

#include <cstdio>
#include <fstream>
#include <string>

#include "test_harness.h"

namespace {

std::string write_temp_config(const std::string& contents)
{
	const std::string path = "build/test_config.tmp";
	std::ofstream out(path, std::ios::out | std::ios::trunc);
	out << contents;
	out.close();
	return path;
}

} // namespace

void test_defaults_when_no_file_is_given()
{
	MonitorConfig cfg = MonitorConfig::load("");
	CHECK(cfg.device_path == "/dev/parking_sensor");
	CHECK(cfg.poll_interval_ms == 1000);
	CHECK(cfg.warning_threshold_cm == 50);
	CHECK(cfg.danger_threshold_cm == 20);
	CHECK(cfg.tcp_enabled == true);
	CHECK(cfg.tcp_port == 9000);
}

void test_missing_file_falls_back_to_defaults()
{
	/* A missing file prints a note and keeps the defaults - it must not
	 * throw, because the monitor should still start. */
	MonitorConfig cfg =
		MonitorConfig::load("build/this_file_does_not_exist.conf");
	CHECK(cfg.warning_threshold_cm == 50);
	CHECK(cfg.tcp_port == 9000);
}

void test_full_file_is_parsed()
{
	const std::string path = write_temp_config(
		"# parking test config\n"
		"device_path = /dev/parking_sensor\n"
		"poll_interval_ms = 250\n"
		"warning_threshold_cm = 80\n"
		"danger_threshold_cm  = 30    # inline comment\n"
		"log_path = logs/test.log\n"
		"tcp_enabled = false\n"
		"bind_address = 0.0.0.0\n"
		"tcp_port = 9100\n");

	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.device_path == "/dev/parking_sensor");
	CHECK(cfg.poll_interval_ms == 250);
	CHECK(cfg.warning_threshold_cm == 80);
	CHECK(cfg.danger_threshold_cm == 30);
	CHECK(cfg.log_path == "logs/test.log");
	CHECK(cfg.tcp_enabled == false);
	CHECK(cfg.bind_address == "0.0.0.0");
	CHECK(cfg.tcp_port == 9100);

	std::remove(path.c_str());
}

void test_whitespace_and_unknown_keys_are_tolerated()
{
	const std::string path = write_temp_config(
		"\n"
		"   \n"
		"   poll_interval_ms   =   750   \n"
		"this_line_has_no_equals_sign\n"
		"unknown_key = 42\n"
		"# comment only\n"
		"danger_threshold_cm=15\n");

	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.poll_interval_ms == 750);
	CHECK(cfg.danger_threshold_cm == 15);
	CHECK(cfg.warning_threshold_cm == 50); /* default kept */

	std::remove(path.c_str());
}

void test_non_numeric_value_keeps_the_default()
{
	const std::string path = write_temp_config(
		"poll_interval_ms = soon\n"
		"tcp_port = 12345\n");

	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.poll_interval_ms == 1000); /* bad line ignored */
	CHECK(cfg.tcp_port == 12345);		/* good line applied */

	std::remove(path.c_str());
}

/* The state machine would be meaningless with danger >= warning. */
void test_impossible_thresholds_are_repaired()
{
	const std::string path = write_temp_config(
		"warning_threshold_cm = 20\n"
		"danger_threshold_cm = 50\n");

	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.warning_threshold_cm == 50);
	CHECK(cfg.danger_threshold_cm == 20);

	std::remove(path.c_str());
}

void test_out_of_range_port_is_repaired()
{
	const std::string path = write_temp_config("tcp_port = 99999\n");
	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.tcp_port == 9000);
	std::remove(path.c_str());
}

void test_tiny_poll_interval_is_repaired()
{
	const std::string path = write_temp_config("poll_interval_ms = 1\n");
	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.poll_interval_ms == 1000);
	std::remove(path.c_str());
}

void test_boolean_spellings()
{
	const std::string path = write_temp_config(
		"tcp_enabled = YES\n");
	MonitorConfig cfg = MonitorConfig::load(path);
	CHECK(cfg.tcp_enabled == true);

	std::remove(path.c_str());
}

TEST_LIST(TEST_ENTRY(test_defaults_when_no_file_is_given)
	  TEST_ENTRY(test_missing_file_falls_back_to_defaults)
	  TEST_ENTRY(test_full_file_is_parsed)
	  TEST_ENTRY(test_whitespace_and_unknown_keys_are_tolerated)
	  TEST_ENTRY(test_non_numeric_value_keeps_the_default)
	  TEST_ENTRY(test_impossible_thresholds_are_repaired)
	  TEST_ENTRY(test_out_of_range_port_is_repaired)
	  TEST_ENTRY(test_tiny_poll_interval_is_repaired)
	  TEST_ENTRY(test_boolean_spellings))
