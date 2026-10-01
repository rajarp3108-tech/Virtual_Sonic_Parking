/*
 * logger.h - FR-09: append timestamped events to a log file.
 *
 * Implemented as a worker thread owning the file, fed by a queue protected
 * by a mutex and a condition variable. This keeps disk I/O off the polling
 * loop (performance) and demonstrates threads + STL queue from the syllabus.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "telemetry.h"

class Logger {
public:
	explicit Logger(const std::string& path);
	~Logger();

	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	/* Open the file and start the writer thread. Throws on open failure. */
	void start();

	/* Hand a record to the writer thread. */
	void log(const TelemetryRecord& rec);

	/* Plain informational line (startup, shutdown, errors). */
	void log_event(const std::string& message);

	/* Stop the thread, drain the queue, flush and close. Idempotent. */
	void stop();

	long written() const { return written_.load(); }
	const std::string& path() const { return path_; }

private:
	void worker_loop();

	std::string path_;
	std::ofstream out_;
	std::queue<std::string> pending_;
	mutable std::mutex mtx_;
	/* Serialises stop() so two callers cannot both join the worker and
	 * close the stream underneath each other. */
	mutable std::mutex stop_mtx_;
	std::condition_variable cv_;
	std::thread worker_;
	std::atomic<bool> running_;
	std::atomic<bool> started_;
	std::atomic<long> written_;
};

#endif /* LOGGER_H */
