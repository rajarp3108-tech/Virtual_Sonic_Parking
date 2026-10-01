#include "tcp_status_server.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

std::string format_status_message(int distance_cm, ParkingState state)
{
	/* Exactly the format required by the SRS section 12. */
	return std::string("STATUS distance=") + std::to_string(distance_cm) +
	       " state=" + to_string(state) + "\n";
}

TCPStatusServer::TCPStatusServer(std::string bind_address, int port)
    : bind_address_(std::move(bind_address)),
      port_(port),
      listen_fd_(-1),
      latest_state_(ParkingState::Safe),
      latest_distance_cm_(0),
      running_(false)
{
	latest_message_ = format_status_message(0, ParkingState::Safe);
}

TCPStatusServer::~TCPStatusServer()
{
	stop();
}

void TCPStatusServer::start()
{
	if (running_.load())
		return;

	/* AF_INET = IPv4, SOCK_STREAM = TCP. */
	listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd_ < 0)throw std::runtime_error(std::string("socket() failed: ") + std::strerror(errno));

	/* Without SO_REUSEADDR a restart within a minute fails with
	 * EADDRINUSE because the old socket is still in TIME_WAIT. */
	int yes = 1;
	if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes,
			 sizeof(yes)) < 0) {
		std::cerr << "[tcp] setsockopt(SO_REUSEADDR) failed: "
			  << std::strerror(errno) << "\n";
	}

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = ::htons(static_cast<uint16_t>(port_));

	if (bind_address_.empty() || bind_address_ == "0.0.0.0")
		addr.sin_addr.s_addr = INADDR_ANY;
	else if (::inet_pton(AF_INET, bind_address_.c_str(), &addr.sin_addr) !=
		 1) {
		/* Close here as well as in stop(): a caller that catches this
		 * and retries with a good address must not leak the socket. */
		::close(listen_fd_);
		listen_fd_ = -1;
		throw std::runtime_error("invalid IPv4 bind_address: " +
					 bind_address_);
	}

	if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
		   sizeof(addr)) < 0) {
		std::string msg = std::strerror(errno);
		::close(listen_fd_);
		listen_fd_ = -1;
		throw std::runtime_error("bind(" + bind_address_ + ":" +
					 std::to_string(port_) +
					 ") failed: " + msg);
	}

	if (::listen(listen_fd_, 4) < 0) {
		std::string msg = std::strerror(errno);
		::close(listen_fd_);
		listen_fd_ = -1;
		throw std::runtime_error("listen() failed: " + msg);
	}

	running_.store(true);
	thread_ = std::thread(&TCPStatusServer::accept_loop, this);

	std::cout << "[tcp] status server listening on " << bind_address_ << ":"
		  << port_ << "\n";
}

void TCPStatusServer::publish(int distance_cm, ParkingState state)
{
	std::lock_guard<std::mutex> lock(mtx_);
	latest_distance_cm_ = distance_cm;
	latest_state_ = state;
	latest_message_ = format_status_message(distance_cm, state);
}

void TCPStatusServer::serve_client(int client_fd)
{
	std::string message;
	{
		std::lock_guard<std::mutex> lock(mtx_);
		message = latest_message_;
	}

	/* MSG_NOSIGNAL: if the client disappears we get EPIPE from send()
	 * instead of a SIGPIPE that would kill the whole monitor. */
	size_t sent = 0;
	while (sent < message.size()) {
		ssize_t n = ::send(client_fd, message.data() + sent,
				   message.size() - sent, MSG_NOSIGNAL);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		sent += static_cast<size_t>(n);
	}

	::shutdown(client_fd, SHUT_WR);
	::close(client_fd);
}

void TCPStatusServer::accept_loop()
{
	while (running_.load()) {
		struct sockaddr_in peer{};
		socklen_t peer_len = sizeof(peer);

		int client_fd = ::accept(listen_fd_,
					 reinterpret_cast<struct sockaddr*>(&peer),
					 &peer_len);
		if (client_fd < 0) {
			if (errno == EINTR)
				continue;
			/* accept() also fails with EBADF once the socket is
			 * closed by stop(); that is a normal shutdown. */
			if (!running_.load() || errno == EBADF ||
			    errno == EINVAL)
				break;
			std::cerr << "[tcp] accept() failed: "
				  << std::strerror(errno) << "\n";
			continue;
		}

		char ip[INET_ADDRSTRLEN] = {0};
		if (::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip)))
			std::cout << "[tcp] client connected from " << ip << "\n";

		/* One request/response per connection. A short-lived
		 * blocking exchange is enough for the demo and keeps the
		 * accept loop simple. */
		serve_client(client_fd);
	}
}

void TCPStatusServer::stop()
{
	if (!running_.exchange(false)) {
		/* Not running, but a failed start() may still own a socket. */
		if (listen_fd_ >= 0) {
			::close(listen_fd_);
			listen_fd_ = -1;
		}
		return;
	}

	/* Shutting down the listening socket wakes accept() with EINVAL so
	 * the thread does not sit in a blocking call. */
	if (listen_fd_ >= 0)
		::shutdown(listen_fd_, SHUT_RDWR);

	if (thread_.joinable())
		thread_.join();

	if (listen_fd_ >= 0) {
		::close(listen_fd_);
		listen_fd_ = -1;
	}
}
