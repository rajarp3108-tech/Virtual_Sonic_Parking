/*
 * tcp_status_server.h - FR-12: IPv4 TCP server that publishes the current
 * distance and state to any connected client.
 *
 * Wire format (fixed by the SRS):
 *   STATUS distance=37 state=WARNING\n
 *
 * A dedicated thread accepts connections and answers with a snapshot of the
 * latest status, so a late-connecting client immediately sees valid data.
 */
#ifndef TCP_STATUS_SERVER_H
#define TCP_STATUS_SERVER_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "telemetry.h"

/* Build the exact wire message. Free function so it can be unit tested. */
std::string format_status_message(int distance_cm, ParkingState state);

class TCPStatusServer {
public:
	TCPStatusServer(std::string bind_address, int port);
	~TCPStatusServer();

	TCPStatusServer(const TCPStatusServer&) = delete;
	TCPStatusServer& operator=(const TCPStatusServer&) = delete;

	/* Bind, listen and start the accept thread. Throws on socket error. */
	void start();

	/* Publish the newest status. Cheap and thread safe. */
	void publish(int distance_cm, ParkingState state);

	void stop();
	bool running() const { return running_.load(); }
	int port() const { return port_; }

private:
	void accept_loop();
	/* Answer one client and close the connection. */
	void serve_client(int client_fd);

	std::string bind_address_;
	int port_;
	int listen_fd_;

	std::string latest_message_;
	ParkingState latest_state_;
	int latest_distance_cm_;
	mutable std::mutex mtx_;

	std::thread thread_;
	std::atomic<bool> running_;
};

#endif /* TCP_STATUS_SERVER_H */
