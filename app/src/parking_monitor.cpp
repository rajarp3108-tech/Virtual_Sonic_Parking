#include "parking_monitor.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>

std::atomic<bool> g_stop_requested{false};

ParkingMonitor::ParkingMonitor(MonitorConfig config,
			       std::unique_ptr<ISensorSource> source,
			       std::unique_ptr<StateManager> state,
			       std::unique_ptr<Logger> logger,
			       std::unique_ptr<TCPStatusServer> server)
    : config_(std::move(config)),
      source_(std::move(source)),
      state_(std::move(state)),
      logger_(std::move(logger)),
      server_(std::move(server)),
      stop_requested_(false),
      max_iterations_(0),
      iterations_(0),
      read_errors_(0)
{
}

ParkingMonitor::~ParkingMonitor()
{
	/* Order matters for a clean shutdown log line: stop the loop, flush
	 * the logger, then close the socket. The unique_ptr destructors
	 * handle the rest (RAII). */
	request_stop();
}

void ParkingMonitor::request_stop()
{
	stop_requested_.store(true);
}

long ParkingMonitor::transitions() const
{
	return state_ ? state_->transition_count() : 0;
}

void ParkingMonitor::log_startup()
{
	logger_->log_event("monitor_start");
	std::cout << "[monitor] source      = " << source_->name() << "\n"
		  << "[monitor] device      = " << config_.device_path << "\n"
		  << "[monitor] policy      = " << state_->policy().describe()
		  << "\n"
		  << "[monitor] poll        = " << config_.poll_interval_ms
		  << " ms\n"
		  << "[monitor] log file    = " << config_.log_path << "\n"
		  << "[monitor] tcp         = "
		  << (config_.tcp_enabled ? config_.bind_address + ":" +
					       std::to_string(config_.tcp_port)
					 : std::string("disabled"))
		  << "\n";
}

void ParkingMonitor::log_shutdown()
{
	logger_->log_event("monitor_stop");
}

void ParkingMonitor::run()
{
	/* Order of start-up: log file first, then the socket, then the loop. */
	logger_->start();
	log_startup();

	if (server_)
		server_->start();

	/* Ctrl-C / SIGTERM flips g_stop_requested in the signal handler. */
	std::cout << "[monitor] running - press Ctrl-C to stop\n";

	while (!stop_requested_.load() && !g_stop_requested.load()) {
		if (max_iterations_ > 0 && iterations_ >= max_iterations_)
			break;

		++iterations_;

		int distance_cm = 0;
		try {
			distance_cm = source_->read_distance_cm();
		} catch (const std::exception& e) {
			/* FR-16: a failed read is reported, counted and
			 * retried on the next iteration. The monitor does
			 * not die because of one bad read. */
			++read_errors_;
			std::cerr << "[monitor] read failed: " << e.what()
				  << "\n";
			logger_->log_event("read_error");
			std::this_thread::sleep_for(
				std::chrono::milliseconds(config_.poll_interval_ms));
			continue;
		}

		std::string event;
		bool changed = state_->update(distance_cm, &event);
		ParkingState state = state_->current();

		TelemetryRecord rec(distance_cm, state, event);
		logger_->log(rec);

		if (server_)
			server_->publish(distance_cm, state);

		if (changed) {
			std::cout << "[monitor] STATE CHANGE -> "
				  << to_string(state) << " at " << distance_cm
				  << " cm\n";
		} else {
			std::cout << "[monitor] distance=" << distance_cm
				  << " cm state=" << to_string(state) << "\n";
		}
		std::cout.flush();

		if (stop_requested_.load() || g_stop_requested.load())
			break;

		/* Sleep in short slices so a signal is noticed quickly
		 * instead of after a full poll interval. The last slice is
		 * trimmed to the remaining time, otherwise a short interval
		 * such as 1 ms would still block for 50 ms. */
		int slept = 0;
		while (slept < config_.poll_interval_ms &&
		       !stop_requested_.load() && !g_stop_requested.load()) {
			const int slice = std::min(50, config_.poll_interval_ms - slept);
			std::this_thread::sleep_for(std::chrono::milliseconds(slice));
			slept += slice;
		}
	}

	log_shutdown();

	/* Summary - this is what the integration test asserts on. */
	std::cout << "[monitor] stopping after " << iterations_
		  << " iterations, " << transitions() << " state transitions, "
		  << read_errors_ << " read errors, " << logger_->written()
		  << " log lines\n";

	if (server_)
		server_->stop();
	logger_->stop();
}
