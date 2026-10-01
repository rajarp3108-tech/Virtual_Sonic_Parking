/*
 * test_logger_and_telemetry.cpp - FR-09: log formatting and append
 * behaviour, checked without needing a real file-system heavy test.
 */
#include "logger.h"
#include "telemetry.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "test_harness.h"

namespace {

std::string read_file(const std::string& path)
{
	std::ifstream in(path);
	if (!in.is_open())
		return "";
	std::string all;
	std::string line;
	while (std::getline(in, line))
		all += line + "\n";
	return all;
}

int count_lines(const std::string& text)
{
	int n = 0;
	for (char c : text) {
		if (c == '\n')
			++n;
	}
	return n;
}

} // namespace

void test_state_names_are_uppercase()
{
	/* The log and the TCP wire both depend on these exact strings. */
	CHECK(std::string(to_string(ParkingState::Safe)) == "SAFE");
	CHECK(std::string(to_string(ParkingState::Warning)) == "WARNING");
	CHECK(std::string(to_string(ParkingState::Danger)) == "DANGER");
}

/* Appendix B of the SRS:
 * 2026-09-28 19:02:11 | distance=80 | state=SAFE | event=periodic_read */
void test_record_format_matches_the_srs()
{
	TelemetryRecord rec(80, ParkingState::Safe, "periodic_read");
	rec.timestamp = 0; /* 1970-01-01 00:00:00 UTC, exact and portable */

	const std::string line = format_record(rec);
	CHECK_CONTAINS(line, "distance=80");
	CHECK_CONTAINS(line, "state=SAFE");
	CHECK_CONTAINS(line, "event=periodic_read");
	CHECK_CONTAINS(line, " | ");
	/* 19 characters: "YYYY-MM-DD HH:MM:SS" */
	CHECK_CONTAINS(line.substr(0, 4), "19");
}

void test_timestamp_format_shape()
{
	const std::string ts = format_timestamp(0);
	CHECK_EQ(ts.size(), static_cast<size_t>(19));
	CHECK_EQ(ts[4], '-');
	CHECK_EQ(ts[7], '-');
	CHECK_EQ(ts[10], ' ');
	CHECK_EQ(ts[13], ':');
	CHECK_EQ(ts[16], ':');
}

void test_danger_record_format()
{
	TelemetryRecord rec(18, ParkingState::Danger, "state_change");
	const std::string line = format_record(rec);
	CHECK_CONTAINS(line, "distance=18");
	CHECK_CONTAINS(line, "state=DANGER");
	CHECK_CONTAINS(line, "event=state_change");
}

void test_empty_event_is_reported_as_none()
{
	TelemetryRecord rec(50, ParkingState::Warning, "");
	CHECK_CONTAINS(format_record(rec), "event=none");
}

void test_logger_writes_every_line()
{
	const std::string path = "build/test_logger.log";
	std::remove(path.c_str());

	{
		Logger logger(path);
		logger.start();
		for (int i = 0; i < 20; ++i) {
			TelemetryRecord rec(150 - i * 5, ParkingState::Safe,
					    "periodic_read");
			logger.log(rec);
		}
		logger.log_event("unit_test_event");
		logger.stop();
	}

	const std::string text = read_file(path);
	CHECK_EQ(count_lines(text), 21);
	CHECK_CONTAINS(text, "unit_test_event");
	CHECK_CONTAINS(text, "distance=150");
	CHECK_CONTAINS(text, "distance=55");

	std::remove(path.c_str());
}

/* The log is opened in append mode, so a restart must not wipe history. */
void test_logger_appends_instead_of_truncating()
{
	const std::string path = "build/test_logger_append.log";
	std::remove(path.c_str());

	for (int run = 0; run < 2; ++run) {
		Logger logger(path);
		logger.start();
		logger.log_event(run == 0 ? "first_run" : "second_run");
		logger.stop();
	}

	const std::string text = read_file(path);
	CHECK_EQ(count_lines(text), 2);
	CHECK_CONTAINS(text, "first_run");
	CHECK_CONTAINS(text, "second_run");

	std::remove(path.c_str());
}

/* Logging must be safe from the monitor thread while the writer works. */
void test_concurrent_logging_is_serialised()
{
	const std::string path = "build/test_logger_threads.log";
	std::remove(path.c_str());

	constexpr int kThreads = 4;
	constexpr int kPerThread = 25;

	{
		Logger logger(path);
		logger.start();

		std::vector<std::thread> writers;
		for (int t = 0; t < kThreads; ++t) {
			writers.emplace_back([&logger, t] {
				for (int i = 0; i < kPerThread; ++i) {
					TelemetryRecord rec(100 + t,
							    ParkingState::Safe,
							    "thread_" +
								    std::to_string(t));
					logger.log(rec);
				}
			});
		}
		for (auto& th : writers)
			th.join();

		logger.stop();
	}

	const std::string text = read_file(path);
	/* Every record must be a complete line - no interleaved garbage. */
	CHECK_EQ(count_lines(text), kThreads * kPerThread);
	CHECK_CONTAINS(text, "thread_0");
	CHECK_CONTAINS(text, "thread_3");

	std::remove(path.c_str());
}

void test_logger_reports_a_bad_path()
{
	bool threw = false;
	try {
		Logger logger("build/no_such_directory/x.log");
		logger.start();
	} catch (const std::exception&) {
		threw = true;
	}
	CHECK(threw);
}

TEST_LIST(TEST_ENTRY(test_state_names_are_uppercase)
	  TEST_ENTRY(test_record_format_matches_the_srs)
	  TEST_ENTRY(test_timestamp_format_shape)
	  TEST_ENTRY(test_danger_record_format)
	  TEST_ENTRY(test_empty_event_is_reported_as_none)
	  TEST_ENTRY(test_logger_writes_every_line)
	  TEST_ENTRY(test_logger_appends_instead_of_truncating)
	  TEST_ENTRY(test_concurrent_logging_is_serialised)
	  TEST_ENTRY(test_logger_reports_a_bad_path))
