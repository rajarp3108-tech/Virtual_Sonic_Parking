/*
 * main.cpp - command line entry point for the parking monitor.
 *
 * Responsibilities kept here on purpose (thin main):
 *   - parse the command line
 *   - load the configuration file
 *   - build the object graph (composition root)
 *   - install signal handlers for a clean shutdown
 *   - optionally detach into the background (daemon mode)
 *
 * FR-10  background/daemon execution
 * FR-11  signal handling and clean resource release
 * FR-15  foreground demo mode for the presentation
 */

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "logger.h"
#include "parking_monitor.h"
#include "sensor_reader.h"
#include "state_manager.h"
#include "telemetry.h"
#include "tcp_status_server.h"

namespace {

const char* kProgramName = "parking_monitor";

void print_usage()
{
	std::cout << "Usage: " << kProgramName << " [options]\n"
		  << "\n"
		  << "Virtual ultrasonic parking sensor monitor.\n"
		  << "\n"
		  << "Options:\n"
		  << "  --config <file>     configuration file (key = value)\n"
		  << "  --device <path>     override the device path\n"
		  << "                      (default /dev/parking_sensor)\n"
		  << "  --log <file>        override the log file path\n"
		  << "  --interval <ms>     override the polling interval\n"
		  << "  --warning <cm>      WARNING threshold (default 50)\n"
		  << "  --danger <cm>       DANGER threshold (default 20)\n"
		  << "  --host <ip>         TCP bind address (default 127.0.0.1)\n"
		  << "  --port <n>          TCP port (default 9000)\n"
		  << "  --no-tcp            do not start the TCP status server\n"
		  << "  --simulate          use the in-process simulated sensor\n"
		  << "                      instead of the kernel driver, so the\n"
		  << "                      demo runs without root or QEMU\n"
		  << "  --daemon            detach and run in the background\n"
		  << "  --iterations <n>    stop after n polls (0 = run forever)\n"
		  << "  --help              this text\n"
		  << "\n"
		  << "Signals: SIGINT / SIGTERM stop the monitor cleanly.\n";
}

/*
 * Signal handler.
 *
 * Rules for a handler: do almost nothing. It only sets an atomic flag; the
 * control loop notices it and unwinds normally so that every destructor runs
 * and the log file is flushed. printf() inside a handler is not async-signal
 * safe and is deliberately not used.
 */
extern "C" void handle_stop_signal(int signum)
{
	(void)signum;
	g_stop_requested.store(true);
}

void install_signal_handlers()
{
	struct sigaction sa{};
	sa.sa_handler = handle_stop_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* no SA_RESTART: blocking calls should return EINTR */

	::sigaction(SIGINT, &sa, nullptr);
	::sigaction(SIGTERM, &sa, nullptr);

	/* A client that vanishes mid-send must not kill the process. */
	::signal(SIGPIPE, SIG_IGN);
}

/*
 * Create every missing folder in the path of `path`, e.g.
 * "logs/archive/parking.log" creates logs/ and logs/archive/.
 *
 * mkdir() is called with the "directory already exists" case ignored, so this
 * is safe to call on every start. A failure is not fatal: Logger::start()
 * reports the real problem with the full path.
 */
void ensure_parent_directory(const std::string& path)
{
	const size_t last_slash = path.find_last_of('/');
	if (last_slash == std::string::npos || last_slash == 0)
		return;

	const std::string parent = path.substr(0, last_slash);
	const bool absolute = !parent.empty() && parent[0] == '/';

	std::string built = absolute ? "/" : "";
	size_t pos = absolute ? 1 : 0;

	while (pos <= parent.size()) {
		const size_t next = parent.find('/', pos);
		const std::string component = parent.substr(
			pos, next == std::string::npos ? std::string::npos
							: next - pos);
		if (!component.empty()) {
			if (!built.empty() && built.back() != '/')
				built += "/";
			built += component;
			::mkdir(built.c_str(), 0755);
		}
		if (next == std::string::npos)
			break;
		pos = next + 1;
	}
}

/*
 * Classic double-fork daemonisation.
 *
 *  1st fork  - the parent exits, so the shell gets its prompt back.
 *  setsid()  - new session leader, the process loses the controlling terminal.
 *  2nd fork  - guarantees the daemon can never re-acquire a terminal.
 *  redirect  - stdin goes to /dev/null, stdout/stderr go to the log file.
 */
bool daemonise(const std::string& log_path)
{
	pid_t pid = ::fork();
	if (pid < 0) {
		std::cerr << "[main] fork() failed: " << std::strerror(errno)
			  << "\n";
		return false;
	}
	if (pid > 0)
		::_exit(0); /* the first parent leaves */

	if (::setsid() < 0) {
		std::cerr << "[main] setsid() failed: " << std::strerror(errno)
			  << "\n";
		return false;
	}

	pid = ::fork();
	if (pid < 0) {
		std::cerr << "[main] second fork() failed: "
			  << std::strerror(errno) << "\n";
		return false;
	}
	if (pid > 0)
		::_exit(0); /* the session leader leaves */

	::umask(022);

	int devnull = ::open("/dev/null", O_RDONLY);
	if (devnull >= 0) {
		::dup2(devnull, STDIN_FILENO);
		if (devnull > STDERR_FILENO)
			::close(devnull);
	}

	int fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		::dup2(fd, STDOUT_FILENO);
		::dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			::close(fd);
	} else {
		std::cerr << "[main] warning: cannot open " << log_path
			  << " for daemon output\n";
	}

	/* Printed after the redirect, so this line lands in the log file -
	 * which is where the real pid has to be recorded, because the
	 * terminal that started the daemon is no longer connected. */
	std::cerr << "[main] daemon started, pid " << ::getpid() << ", output in "
		  << log_path << "\n";
	return true;
}

} // namespace

int main(int argc, char* argv[])
{
	/* Command line values. Each flag records only whether it was given,
	 * so a file value and a flag value can be applied in that order. */
	std::string config_path;
	std::string device_override, log_override, host_override;
	long interval_override = 0;
	int warning_override = 0, danger_override = 0, port_override = 0;
	long max_iterations = 0;

	bool use_simulator = false;
	bool run_as_daemon = false;
	bool no_tcp = false;
	bool device_set = false, log_set = false, host_set = false;
	bool interval_set = false, warning_set = false, danger_set = false;
	bool port_set = false;

	/* std::stol / std::stoi throw on garbage input. Letting that escape
	 * would print a C++ exception message and abort with SIGABRT, so every
	 * numeric argument goes through these two helpers instead. */
	auto parse_long = [](const char* name, const std::string& text) {
		try {
			size_t consumed = 0;
			const long value = std::stol(text, &consumed);
			if (consumed != text.size())
				throw std::invalid_argument("trailing characters");
			return value;
		} catch (const std::exception&) {
			std::cerr << "[main] " << name
				  << " expects a whole number, got: " << text << "\n";
			std::exit(EXIT_FAILURE);
		}
	};

	auto parse_int = [&](const char* name, const std::string& text) {
		const long value = parse_long(name, text);
		if (value < -2147483648L || value > 2147483647L) {
			std::cerr << "[main] " << name << " is out of range: " << text
				  << "\n";
			std::exit(EXIT_FAILURE);
		}
		return static_cast<int>(value);
	};

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];

		auto need_value = [&](const char* name) -> std::string {
			if (i + 1 >= argc) {
				std::cerr << "[main] " << name
					  << " needs a value\n";
				std::exit(EXIT_FAILURE);
			}
			return std::string(argv[++i]);
		};

		if (arg == "--help" || arg == "-h") {
			print_usage();
			return EXIT_SUCCESS;
		} else if (arg == "--config") {
			config_path = need_value("--config");
		} else if (arg == "--device") {
			device_override = need_value("--device");
			device_set = true;
		} else if (arg == "--log") {
			log_override = need_value("--log");
			log_set = true;
		} else if (arg == "--host") {
			host_override = need_value("--host");
			host_set = true;
		} else if (arg == "--interval") {
			interval_override =
				parse_long("--interval", need_value("--interval"));
			interval_set = true;
		} else if (arg == "--warning") {
			warning_override =
				parse_int("--warning", need_value("--warning"));
			warning_set = true;
		} else if (arg == "--danger") {
			danger_override =
				parse_int("--danger", need_value("--danger"));
			danger_set = true;
		} else if (arg == "--port") {
			port_override = parse_int("--port", need_value("--port"));
			port_set = true;
		} else if (arg == "--iterations") {
			max_iterations =
				parse_long("--iterations", need_value("--iterations"));
			if (max_iterations < 0) {
				std::cerr << "[main] --iterations cannot be negative\n";
				return EXIT_FAILURE;
			}
		} else if (arg == "--no-tcp") {
			no_tcp = true;
		} else if (arg == "--simulate") {
			use_simulator = true;
		} else if (arg == "--daemon") {
			run_as_daemon = true;
		} else {
			std::cerr << "[main] unknown option: " << arg << "\n";
			print_usage();
			return EXIT_FAILURE;
		}
	}

	/* ---- configuration: file first, command line wins (FR-14) ---- */
	MonitorConfig config = MonitorConfig::load(config_path);

	if (device_set)
		config.device_path = device_override;
	if (log_set)
		config.log_path = log_override;
	if (host_set)
		config.bind_address = host_override;
	if (interval_set)
		config.poll_interval_ms = static_cast<int>(interval_override);
	if (warning_set)
		config.warning_threshold_cm = warning_override;
	if (danger_set)
		config.danger_threshold_cm = danger_override;
	if (port_set)
		config.tcp_port = port_override;
	if (no_tcp)
		config.tcp_enabled = false;

	/* Validate after the overrides so bad CLI values are caught too. */
	if (config.danger_threshold_cm >= config.warning_threshold_cm) {
		std::cerr << "[main] --danger must be lower than --warning\n";
		return EXIT_FAILURE;
	}
	if (config.poll_interval_ms < 1) {
		std::cerr << "[main] --interval must be positive\n";
		return EXIT_FAILURE;
	}
	if (config.tcp_enabled &&
	    (config.tcp_port <= 0 || config.tcp_port > 65535)) {
		std::cerr << "[main] --port must be between 1 and 65535\n";
		return EXIT_FAILURE;
	}

	ensure_parent_directory(config.log_path);
	install_signal_handlers();

	if (run_as_daemon) {
		/* Printed before forking, and flushed explicitly, because the
		 * first parent leaves with _exit() and would otherwise drop the
		 * message. The pid itself is only known after the double fork,
		 * so it is written to the log file instead - see daemonise(). */
		std::cout << "[main] detaching: output and errors go to "
			  << config.log_path << "\n"
			  << "[main] stop it later with: pkill -x " << kProgramName
			  << std::endl;
		if (!daemonise(config.log_path)) {
			std::cerr << "[main] could not enter daemon mode\n";
			return EXIT_FAILURE;
		}
	}

	try {
		/* ---- composition root: build the object graph ---- */

		/* FR-02 / FR-04: the monitor only knows the interface. */
		std::unique_ptr<ISensorSource> source;
		if (use_simulator) {
			source = std::make_unique<SimulatedSensorReader>();
		} else {
			/* Throws when the driver is not loaded; main turns
			 * that into a clear message plus a hint. */
			source = std::make_unique<DeviceSensorReader>(
				config.device_path);
		}

		/* FR-07: policy object + stateful manager. */
		ThresholdPolicy policy(config.warning_threshold_cm,
				       config.danger_threshold_cm);
		auto state = std::make_unique<StateManager>(policy);

		/* FR-09: logger with its own writer thread. */
		auto logger = std::make_unique<Logger>(config.log_path);

		/* FR-12: IPv4 TCP status server. */
		std::unique_ptr<TCPStatusServer> server;
		if (config.tcp_enabled) {
			server = std::make_unique<TCPStatusServer>(
				config.bind_address, config.tcp_port);
		}

		ParkingMonitor monitor(std::move(config), std::move(source),
				       std::move(state), std::move(logger),
				       std::move(server));
		monitor.set_max_iterations(max_iterations);
		monitor.run();

		return EXIT_SUCCESS;
	} catch (const std::exception& e) {
		/* FR-16: every failure exits with a readable message. */
		std::cerr << "[main] fatal: " << e.what() << "\n";
		std::cerr << "[main] tip: run with --simulate to demo the "
			     << "user-space side without the kernel driver\n";
		return EXIT_FAILURE;
	}
}
