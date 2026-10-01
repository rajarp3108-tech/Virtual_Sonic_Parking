#include "logger.h"

#include <ctime>
#include <iostream>
#include <stdexcept>
#include <utility>

Logger::Logger(const std::string& path)
    : path_(path), running_(false), started_(false), written_(0)
{
}

Logger::~Logger()
{
	/* Destructor must never throw, so stop() is the only exit path. */
	stop();
}

void Logger::start()
{
	bool expected = false;
	if (!started_.compare_exchange_strong(expected, true))
		return;

	out_.open(path_, std::ios::out | std::ios::app);
	if (!out_.is_open()) {
		started_.store(false);
		throw std::runtime_error("cannot open log file: " + path_);
	}

	running_.store(true);
	worker_ = std::thread(&Logger::worker_loop, this);
}

void Logger::log(const TelemetryRecord& rec)
{
	if (!running_.load())
		return;

	{
		std::lock_guard<std::mutex> lock(mtx_);
		pending_.push(format_record(rec));
	}
	cv_.notify_one();
}

void Logger::log_event(const std::string& message)
{
	if (!running_.load())
		return;

	{
		std::lock_guard<std::mutex> lock(mtx_);
		pending_.push(format_timestamp(static_cast<long>(::time(nullptr))) +
			      " | event=" + message);
	}
	cv_.notify_one();
}

void Logger::worker_loop()
{
	for (;;) {
		std::string line;

		{
			std::unique_lock<std::mutex> lock(mtx_);
			/* Wait until there is work or a stop is requested. */
			cv_.wait(lock, [this] {
				return !pending_.empty() || !running_.load();
			});

			/* On stop, drain whatever is left and then leave. */
			if (pending_.empty()) {
				if (!running_.load())
					return;
				continue;
			}

			line = std::move(pending_.front());
			pending_.pop();
		}

		out_ << line << '\n';
		/* Flush per line: a crash must not lose the newest event. */
		out_.flush();
		written_.fetch_add(1);
	}
}

void Logger::stop()
{
	/* Held for the whole function: a second caller (for example the
	 * destructor after an explicit stop) must wait rather than closing
	 * the stream while the first caller is still joining the worker. */
	std::lock_guard<std::mutex> stopping(stop_mtx_);

	if (!started_.load())
		return;

	if (running_.exchange(false)) {
		cv_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

	if (out_.is_open())
		out_.close();

	started_.store(false);
}
