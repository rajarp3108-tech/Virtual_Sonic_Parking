/*
 * status_client.cpp - FR-13: a small TCP client for the status server.
 *
 * It connects to 127.0.0.1:9000 by default, prints the STATUS line it
 * receives, and optionally reconnects `count` times so the demo shows
 * repeated status updates plus a disconnect/reconnect cycle.
 *
 * Build: make -C client
 * Run:   ./build/status_client --count 5 --interval 1000
 */

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

void print_usage()
{
	std::cout << "Usage: status_client [--host <ip>] [--port <n>] "
		     "[--count <n>] [--interval <ms>]\n"
		  << "\n"
		  << "  --host      server address (default 127.0.0.1)\n"
		  << "  --port      server port (default 9000)\n"
		  << "  --count     how many status requests to send (default 1)\n"
		  << "  --interval  wait between requests in ms (default 1000)\n";
}

/*
 * One request/response exchange. Returns false on any network error so the
 * caller can report a clean message instead of aborting.
 */
bool request_status(const std::string& host, int port, std::string* out)
{
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		std::cerr << "[client] socket() failed: " << std::strerror(errno)
			  << "\n";
		return false;
	}

	struct sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = ::htons(static_cast<uint16_t>(port));

	if (::inet_pton(AF_INET, host.c_str(), &server.sin_addr) != 1) {
		std::cerr << "[client] not a valid IPv4 address: " << host
			  << "\n";
		::close(fd);
		return false;
	}

	if (::connect(fd, reinterpret_cast<struct sockaddr*>(&server),
		      sizeof(server)) < 0) {
		std::cerr << "[client] connect(" << host << ":" << port
			  << ") failed: " << std::strerror(errno) << "\n";
		::close(fd);
		return false;
	}

	/* A read timeout, so a server that accepts the connection and then
	 * says nothing cannot hang the client forever. */
	struct timeval timeout{};
	timeout.tv_sec = 5;
	::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	std::cout << "[client] connected to " << host << ":" << port << "\n";

	/* The server answers immediately, so there is nothing to send. The
	 * shutdown(SHUT_WR) half-close below is what tells the server that
	 * this client will not send anything. */
	::shutdown(fd, SHUT_WR);

	/* TCP is a byte stream, so the loop keeps reading until the server
	 * closes and recv() returns 0. A single recv() is not guaranteed to
	 * return the whole line. */
	std::string message;
	char buffer[256];
	for (;;) {
		const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			std::cerr << "[client] recv() failed: "
				  << std::strerror(errno) << "\n";
			::close(fd);
			return false;
		}
		if (n == 0)
			break; /* the server closed: the message is complete */

		message.append(buffer, static_cast<size_t>(n));
		if (message.size() >= 1024) {
			std::cerr << "[client] response too large, giving up\n";
			::close(fd);
			return false;
		}
	}
	::close(fd);

	if (message.empty()) {
		std::cerr << "[client] no data received (server closed early)\n";
		return false;
	}

	*out = message;
	while (!out->empty() && (out->back() == '\n' || out->back() == '\r'))
		out->pop_back();
	return true;
}

/*
 * std::stoi throws on garbage input, which would print a C++ exception
 * message and abort. Every numeric argument goes through this instead.
 */
int parse_int_arg(const char* name, const std::string& text)
{
	try {
		size_t consumed = 0;
		const int value = std::stoi(text, &consumed);
		if (consumed != text.size())
			throw std::invalid_argument("trailing characters");
		return value;
	} catch (const std::exception&) {
		std::cerr << "[client] " << name
			  << " expects a whole number, got: " << text << "\n";
		return -1;
	}
}

} // namespace

int main(int argc, char* argv[])
{
	std::string host = "127.0.0.1";
	int port = 9000;
	int count = 1;
	int interval_ms = 1000;

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		auto need_value = [&](const char* name) -> std::string {
			if (i + 1 >= argc) {
				std::cerr << "[client] " << name
					  << " needs a value\n";
				return "";
			}
			return std::string(argv[++i]);
		};

		if (arg == "--help" || arg == "-h") {
			print_usage();
			return 0;
		} else if (arg == "--host") {
			host = need_value("--host");
			if (host.empty())
				return 1;
		} else if (arg == "--port") {
			port = parse_int_arg("--port", need_value("--port"));
			if (port <= 0 || port > 65535) {
				std::cerr << "[client] --port must be between 1 and 65535\n";
				return 1;
			}
		} else if (arg == "--count") {
			count = parse_int_arg("--count", need_value("--count"));
			if (count < 0) {
				std::cerr << "[client] --count cannot be negative\n";
				return 1;
			}
		} else if (arg == "--interval") {
			interval_ms =
				parse_int_arg("--interval", need_value("--interval"));
			if (interval_ms < 0) {
				std::cerr << "[client] --interval cannot be negative\n";
				return 1;
			}
		} else {
			std::cerr << "[client] unknown option: " << arg << "\n";
			print_usage();
			return 1;
		}
	}

	if (count < 1)
		count = 1;

	int received = 0;
	for (int n = 0; n < count; ++n) {
		std::string message;
		if (request_status(host, port, &message)) {
			std::cout << "[client] <- " << message << "\n";
			++received;
		} else {
			/* Keep going: one failed poll must not end the demo. */
			std::cout << "[client] request " << (n + 1) << "/"
				  << count << " failed\n";
		}

		if (n + 1 < count && interval_ms > 0)
			std::this_thread::sleep_for(
				std::chrono::milliseconds(interval_ms));
	}

	std::cout << "[client] done, " << received << "/" << count
		  << " status messages received\n";
	return received > 0 ? 0 : 1;
}
