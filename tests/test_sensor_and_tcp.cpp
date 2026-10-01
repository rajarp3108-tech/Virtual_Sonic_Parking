/*
 * test_sensor_and_tcp.cpp - FR-03/FR-04 (distance simulation contract) and
 * FR-12/FR-13 (TCP status message + a real client/server exchange).
 *
 * The TCP part opens a socket on 127.0.0.1 on an ephemeral port, so it needs
 * no fixed port and cannot clash with a running monitor.
 */
#include "sensor_reader.h"
#include "tcp_status_server.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test_harness.h"

namespace {

/* Ask the kernel for a free port by binding to port 0, then release it. */
int pick_free_port()
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 0;

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
	addr.sin_port = 0; /* 0 = let the kernel choose */

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

/* Minimal client: connect, read until the server closes, return the text. */
std::string fetch_status(int port)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return "";

	struct sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = ::htons(static_cast<uint16_t>(port));
	::inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

	if (::connect(fd, reinterpret_cast<struct sockaddr*>(&server),
		      sizeof(server)) < 0) {
		::close(fd);
		return "";
	}

	/* TCP is a stream, so a single recv() is not guaranteed to return
	 * the whole line - read until recv() reports end of file. */
	std::string text;
	char buffer[256];
	for (;;) {
		const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
		if (n <= 0)
			break;
		text.append(buffer, static_cast<size_t>(n));
	}
	::close(fd);
	return text;
}

} // namespace

/* ---------------------------------------------------------------- */
/* Simulated sensor                                                  */
/* ---------------------------------------------------------------- */

void test_simulated_sensor_starts_at_maximum()
{
	SimulatedSensorReader sim(10, 150, 10);
	CHECK_EQ(sim.read_distance_cm(), 140);
}

void test_simulated_sensor_walks_down_then_up()
{
	SimulatedSensorReader sim(10, 150, 10);

	/* Collect 60 readings: a full triangle wave over a 150..10 cm range
	 * takes (150-10)/10 = 14 down steps and 14 up steps = 28 readings. */
	std::vector<int> values;
	for (int i = 0; i < 60; ++i)
		values.push_back(sim.read_distance_cm());

	int lowest = values[0];
	int highest = values[0];
	for (int v : values) {
		if (v < lowest)
			lowest = v;
		if (v > highest)
			highest = v;
	}
	CHECK_EQ(lowest, 10);
	CHECK_EQ(highest, 150);
}

void test_simulated_sensor_never_leaves_its_range()
{
	SimulatedSensorReader sim(20, 100, 7);
	for (int i = 0; i < 500; ++i) {
		int d = sim.read_distance_cm();
		CHECK(d >= 20);
		CHECK(d <= 100);
	}
}

void test_simulated_sensor_reset()
{
	SimulatedSensorReader sim(10, 150, 10);
	sim.read_distance_cm();
	sim.read_distance_cm();
	sim.reset();
	CHECK_EQ(sim.ticks(), 0);
	CHECK_EQ(sim.read_distance_cm(), 140);
}

/* ---------------------------------------------------------------- */
/* TCP status message + real exchange                               */
/* ---------------------------------------------------------------- */

void test_status_message_format()
{
	/* Exactly the format from SRS section 12. */
	CHECK(format_status_message(37, ParkingState::Warning) ==
	      "STATUS distance=37 state=WARNING\n");
	CHECK(format_status_message(18, ParkingState::Danger) ==
	      "STATUS distance=18 state=DANGER\n");
	CHECK(format_status_message(82, ParkingState::Safe) ==
	      "STATUS distance=82 state=SAFE\n");
}

void test_server_reports_the_published_status()
{
	const int port = pick_free_port();
	CHECK(port > 0);

	TCPStatusServer server("127.0.0.1", port);
	server.publish(42, ParkingState::Warning);
	server.start();
	CHECK(server.running());

	/* Give the accept thread a moment to reach the accept() call. */
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	const std::string message = fetch_status(port);
	CHECK_CONTAINS(message, "STATUS");
	CHECK_CONTAINS(message, "distance=42");
	CHECK_CONTAINS(message, "state=WARNING");

	server.stop();
	CHECK(server.running() == false);
}

void test_server_serves_a_second_client_after_reconnect()
{
	const int port = pick_free_port();
	CHECK(port > 0);

	TCPStatusServer server("127.0.0.1", port);
	server.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	server.publish(10, ParkingState::Danger);
	const std::string first = fetch_status(port);
	CHECK_CONTAINS(first, "state=DANGER");

	/* Disconnect, update, reconnect - the demo does exactly this. */
	server.publish(150, ParkingState::Safe);
	const std::string second = fetch_status(port);
	CHECK_CONTAINS(second, "state=SAFE");
	CHECK_CONTAINS(second, "distance=150");

	server.stop();
}

void test_server_without_clients_still_starts_and_stops()
{
	const int port = pick_free_port();
	TCPStatusServer server("127.0.0.1", port);
	server.start();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	server.stop();
	CHECK(server.running() == false);
	/* A second stop() must be harmless. */
	server.stop();
}

void test_server_rejects_a_bad_bind_address()
{
	bool threw = false;
	try {
		TCPStatusServer server("not-an-ip", 9100);
		server.start();
		server.stop();
	} catch (const std::exception&) {
		threw = true;
	}
	CHECK(threw);
}

void test_port_in_use_is_reported()
{
	const int port = pick_free_port();
	TCPStatusServer first("127.0.0.1", port);
	first.start();

	bool threw = false;
	try {
		/* SO_REUSEADDR allows rebinding TIME_WAIT sockets, but a
		 * second active listener on the same port must still fail. */
		TCPStatusServer second("127.0.0.1", port);
		second.start();
		second.stop();
	} catch (const std::exception&) {
		threw = true;
	}
	first.stop();
	CHECK(threw);
}

TEST_LIST(TEST_ENTRY(test_simulated_sensor_starts_at_maximum)
	  TEST_ENTRY(test_simulated_sensor_walks_down_then_up)
	  TEST_ENTRY(test_simulated_sensor_never_leaves_its_range)
	  TEST_ENTRY(test_simulated_sensor_reset)
	  TEST_ENTRY(test_status_message_format)
	  TEST_ENTRY(test_server_reports_the_published_status)
	  TEST_ENTRY(test_server_serves_a_second_client_after_reconnect)
	  TEST_ENTRY(test_server_without_clients_still_starts_and_stops)
	  TEST_ENTRY(test_server_rejects_a_bad_bind_address)
	  TEST_ENTRY(test_port_in_use_is_reported))
