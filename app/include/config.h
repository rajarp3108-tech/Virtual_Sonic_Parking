/*
 * config.h - FR-14: thresholds, polling interval and network settings come
 * from a small key = value file instead of being hard-coded.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct MonitorConfig {
	std::string device_path = "/dev/parking_sensor";
	std::string log_path = "logs/parking.log";

	/* FR-06: how often the monitor polls the device. */
	int poll_interval_ms = 1000;

	/* State machine thresholds (cm). */
	int warning_threshold_cm = 50;
	int danger_threshold_cm = 20;

	/* FR-12/FR-13: IPv4 TCP status server. */
	bool tcp_enabled = true;
	std::string bind_address = "127.0.0.1";
	int tcp_port = 9000;

	/*
	 * Load the configuration from a file. Missing file is not an error:
	 * the built-in defaults are kept and a note is printed. Malformed
	 * lines are reported as warnings so a typo never crashes the monitor.
	 * Throws std::runtime_error only if the file cannot be read at all.
	 */
	static MonitorConfig load(const std::string& path);
};

#endif /* CONFIG_H */
