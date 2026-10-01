/*
 * parking_monitor.h - the application controller (MainController in the
 * class diagram). It owns the service objects by composition and runs the
 * single control loop:
 *
 *   read distance -> evaluate state -> log -> publish to TCP -> sleep
 */
#ifndef PARKING_MONITOR_H
#define PARKING_MONITOR_H

#include <atomic>
#include <memory>
#include <string>

#include "config.h"
#include "logger.h"
#include "sensor_reader.h"
#include "state_manager.h"
#include "tcp_status_server.h"

/* Set from the signal handler installed in main(). */
extern std::atomic<bool> g_stop_requested;

class ParkingMonitor {
public:
	/*
	 * All collaborators are injected so the class can be tested with
	 * fakes and so ownership is explicit. unique_ptr = single owner.
	 */
	ParkingMonitor(MonitorConfig config,
		       std::unique_ptr<ISensorSource> source,
		       std::unique_ptr<StateManager> state,
		       std::unique_ptr<Logger> logger,
		       std::unique_ptr<TCPStatusServer> server);

	~ParkingMonitor();

	/* Blocking control loop. Returns when a stop is requested. */
	void run();

	/* Non-blocking stop used by the signal handler path. */
	void request_stop();

	/* Optional: stop after N iterations (0 = run until stopped). Used by
	 * the integration test and by scripted demos. */
	void set_max_iterations(long n) { max_iterations_ = n; }

	long iterations() const { return iterations_; }
	long read_errors() const { return read_errors_; }
	long transitions() const;

private:
	void log_startup();
	void log_shutdown();

	MonitorConfig config_;
	std::unique_ptr<ISensorSource> source_;
	std::unique_ptr<StateManager> state_;
	std::unique_ptr<Logger> logger_;
	std::unique_ptr<TCPStatusServer> server_;

	std::atomic<bool> stop_requested_;
	long max_iterations_;
	long iterations_;
	long read_errors_;
};

#endif /* PARKING_MONITOR_H */
