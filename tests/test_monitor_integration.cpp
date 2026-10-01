/*
 * test_monitor_integration.cpp - end-to-end test of the user-space half.
 *
 * It assembles the same object graph as main() does, but with the simulated
 * sensor and a fixed iteration count, so the whole chain runs in milliseconds
 * without a kernel module:
 *
 *   SimulatedSensorReader -> StateManager -> Logger -> TCPStatusServer
 *
 * Afterwards the log file is re-read to prove the states and events really
 * reached the file. This is the test that maps to the acceptance criteria
 * "the application correctly produces SAFE/WARNING/DANGER" and
 * "state changes and readings are logged".
 */
#include "config.h"
#include "logger.h"
#include "parking_monitor.h"
#include "sensor_reader.h"
#include "state_manager.h"
#include "telemetry.h"
#include "tcp_status_server.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test_harness.h"

namespace {

int pick_free_port()
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 0;
	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	int port = 0;
	if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr),
		   sizeof(addr)) == 0) {
		socklen_t len = sizeof(addr);
		if (::getsockname(fd,
				  reinterpret_cast<struct sockaddr*>(&addr),
				  &len) == 0)
			port = ntohs(addr.sin_port);
	}
	::close(fd);
	return port;
}

/* Build the full graph and run it for `iterations` polls. */
void run_monitor(const std::string& log_path, int iterations, bool with_tcp,
		 long* out_transitions)
{
	MonitorConfig cfg;
	cfg.poll_interval_ms = 1;
	cfg.warning_threshold_cm = 50;
	cfg.danger_threshold_cm = 20;
	cfg.log_path = log_path;
	cfg.tcp_enabled = with_tcp;
	cfg.bind_address = "127.0.0.1";
	cfg.tcp_port = pick_free_port();

	ThresholdPolicy policy(cfg.warning_threshold_cm,
			       cfg.danger_threshold_cm);

	std::unique_ptr<TCPStatusServer> server;
	if (with_tcp)
		server = std::make_unique<TCPStatusServer>("127.0.0.1",
							   cfg.tcp_port);

	auto monitor = std::make_unique<ParkingMonitor>(
		std::move(cfg),
		std::make_unique<SimulatedSensorReader>(10, 150, 10),
		std::make_unique<StateManager>(policy),
		std::make_unique<Logger>(log_path), std::move(server));

	monitor->set_max_iterations(iterations);
	monitor->run();

	if (out_transitions)
		*out_transitions = monitor->transitions();
}

} // namespace

void test_full_run_writes_every_state()
{
	const std::string path = "build/test_monitor_integration.log";
	std::remove(path.c_str());

	long transitions = 0;
	/* 30 polls at 10 cm per poll covers 150 down to 0 and back: all three
	 * states must appear. */
	run_monitor(path, 30, false, &transitions);

	std::ifstream in(path);
	std::string text;
	std::string line;
	while (std::getline(in, line)) {
		text += line;
		text += '\n';
	}
	in.close();

	CHECK_CONTAINS(text, "state=SAFE");
	CHECK_CONTAINS(text, "state=WARNING");
	CHECK_CONTAINS(text, "state=DANGER");
	CHECK_CONTAINS(text, "event=state_change");
	CHECK_CONTAINS(text, "event=periodic_read");
	CHECK_CONTAINS(text, "monitor_start");
	CHECK_CONTAINS(text, "monitor_stop");
	CHECK(transitions >= 4);

	std::remove(path.c_str());
}

/* 150 -> 140 -> ... -> 20 crosses into DANGER at exactly 20 cm. */
void test_danger_appears_at_the_expected_distance()
{
	const std::string path = "build/test_monitor_danger.log";
	std::remove(path.c_str());

	run_monitor(path, 14, false, nullptr);

	std::ifstream in(path);
	std::string line;
	bool found = false;
	while (std::getline(in, line)) {
		if (line.find("state=DANGER") != std::string::npos) {
			found = true;
			/* The first DANGER reading must be distance=20. */
			CHECK_CONTAINS(line, "distance=20");
			break;
		}
	}
	in.close();
	CHECK(found);

	std::remove(path.c_str());
}

void test_readings_are_monotonic_then_reverse()
{
	const std::string path = "build/test_monitor_sequence.log";
	std::remove(path.c_str());

	run_monitor(path, 20, false, nullptr);

	std::ifstream in(path);
	std::vector<int> distances;
	std::string line;
	while (std::getline(in, line)) {
		const size_t key = line.find("distance=");
		if (key == std::string::npos)
			continue;
		distances.push_back(std::stoi(line.substr(key + 9)));
	}
	in.close();

	CHECK(distances.size() >= 18);
	/* First readings go down ... */
	if (distances.size() >= 3) {
		CHECK(distances[1] < distances[0]);
		CHECK(distances[2] < distances[1]);
	}
	/* ... then come back up after the minimum is reached. */
	bool went_up = false;
	for (size_t i = 3; i < distances.size(); ++i) {
		if (distances[i] > distances[i - 1]) {
			went_up = true;
			break;
		}
	}
	CHECK(went_up);

	std::remove(path.c_str());
}

void test_monitor_survives_a_failing_sensor()
{
	/* FR-16: a source that always throws must be reported, counted and
	 * must not take the monitor down. */
	MonitorConfig cfg;
	cfg.poll_interval_ms = 1;
	cfg.log_path = "build/test_monitor_errors.log";
	cfg.tcp_enabled = false;

	class FailingSource : public ISensorSource {
	public:
		int read_distance_cm() override
		{
			throw std::runtime_error("simulated device failure");
		}
		const char* name() const override { return "failing"; }
	};

	ThresholdPolicy policy(50, 20);
	ParkingMonitor monitor(std::move(cfg),
			       std::make_unique<FailingSource>(),
			       std::make_unique<StateManager>(policy),
			       std::make_unique<Logger>("build/test_monitor_errors.log"),
			       nullptr);
	monitor.set_max_iterations(3);
	monitor.run();

	CHECK_EQ(monitor.iterations(), static_cast<long>(3));
	CHECK_EQ(monitor.read_errors(), static_cast<long>(3));

	std::remove("build/test_monitor_errors.log");
}

TEST_LIST(TEST_ENTRY(test_full_run_writes_every_state)
	  TEST_ENTRY(test_danger_appears_at_the_expected_distance)
	  TEST_ENTRY(test_readings_are_monotonic_then_reverse)
	  TEST_ENTRY(test_monitor_survives_a_failing_sensor))
