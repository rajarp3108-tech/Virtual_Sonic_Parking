#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

std::string trim(const std::string& s)
{
	const char* ws = " \t\r\n";
	size_t begin = s.find_first_not_of(ws);
	if (begin == std::string::npos)
		return "";
	size_t end = s.find_last_not_of(ws);
	return s.substr(begin, end - begin + 1);
}

std::string to_lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
	return s;
}

int to_int(const std::string& value, const std::string& key)
{
	try {
		size_t used = 0;
		int v = std::stoi(value, &used);
		if (used != value.size())
			throw std::invalid_argument("trailing characters");
		return v;
	} catch (const std::exception&) {
		throw std::runtime_error("config: '" + key +
					 "' expects a whole number, got '" +
					 value + "'");
	}
}

bool to_bool(const std::string& value)
{
	std::string v = to_lower(trim(value));
	return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

MonitorConfig MonitorConfig::load(const std::string& path)
{
	MonitorConfig cfg;

	if (path.empty())
		return cfg;

	std::ifstream in(path);
	if (!in.is_open()) {
		/* A missing config file is not fatal: defaults still work. */
		std::cerr << "[config] " << path
			  << " not found, using built-in defaults\n";
		return cfg;
	}

	std::string line;
	int line_no = 0;

	while (std::getline(in, line)) {
		++line_no;

		size_t hash = line.find('#');
		if (hash != std::string::npos)
			line = line.substr(0, hash);

		line = trim(line);
		if (line.empty())
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos) {
			std::cerr << "[config] line " << line_no
				  << " ignored (no '='): " << line << "\n";
			continue;
		}

		std::string key = to_lower(trim(line.substr(0, eq)));
		std::string value = trim(line.substr(eq + 1));

		try {
			if (key == "device_path")
				cfg.device_path = value;
			else if (key == "log_path")
				cfg.log_path = value;
			else if (key == "poll_interval_ms")
				cfg.poll_interval_ms = to_int(value, key);
			else if (key == "warning_threshold_cm")
				cfg.warning_threshold_cm = to_int(value, key);
			else if (key == "danger_threshold_cm")
				cfg.danger_threshold_cm = to_int(value, key);
			else if (key == "tcp_enabled")
				cfg.tcp_enabled = to_bool(value);
			else if (key == "bind_address")
				cfg.bind_address = value;
			else if (key == "tcp_port")
				cfg.tcp_port = to_int(value, key);
			else
				std::cerr << "[config] line " << line_no
					  << ": unknown key '" << key
					  << "' ignored\n";
		} catch (const std::exception& e) {
			/* A bad value is reported, then the default is kept. */
			std::cerr << "[config] line " << line_no << ": "
				  << e.what() << " (default kept)\n";
		}
	}

	/* Sanity check: the state machine would be meaningless otherwise. */
	if (cfg.poll_interval_ms < 50) {
		std::cerr << "[config] poll_interval_ms too small, using 1000\n";
		cfg.poll_interval_ms = 1000;
	}
	if (cfg.danger_threshold_cm >= cfg.warning_threshold_cm) {
		std::cerr << "[config] danger_threshold_cm must be lower than "
			     << "warning_threshold_cm, using 20 and 50\n";
		cfg.danger_threshold_cm = 20;
		cfg.warning_threshold_cm = 50;
	}
	if (cfg.tcp_port <= 0 || cfg.tcp_port > 65535) {
		std::cerr << "[config] invalid tcp_port, using 9000\n";
		cfg.tcp_port = 9000;
	}

	return cfg;
}
