#include "sensor_reader.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* DeviceSensorReader                                                  */
/* ------------------------------------------------------------------ */

DeviceSensorReader::DeviceSensorReader(const std::string& path)
    : path_(path), fd_(-1)
{
	/* O_RDWR because the application also sends reset/pause commands
	 * through the same descriptor. */
	fd_ = ::open(path_.c_str(), O_RDWR);
	if (fd_ < 0) {
		throw std::runtime_error("cannot open " + path_ + ": " +
					 std::strerror(errno) +
					 " (is the driver loaded? try: "
					 "sudo insmod "
					 "driver/parking_sensor.ko)");
	}
}

DeviceSensorReader::~DeviceSensorReader()
{
	/* RAII: whoever owns the object does not have to remember close(). */
	if (fd_ >= 0)
		::close(fd_);
}

int DeviceSensorReader::read_distance_cm()
{
	if (fd_ < 0)
		throw std::runtime_error("sensor: device is closed");

	struct parking_sensor_data data{};

	/* One read() == one complete telemetry record. EINTR means a signal
	 * arrived, so the call is retried in a loop - a recursive retry
	 * would grow the stack if signals kept coming. */
	for (;;) {
		const ssize_t n = ::read(fd_, &data, sizeof(data));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			throw std::runtime_error("read(" + path_ + ") failed: " +
						 std::strerror(errno));
		}

		if (n == 0)
			throw std::runtime_error("read(" + path_ +
						 ") returned 0 bytes, driver unloaded?");

		if (static_cast<size_t>(n) != sizeof(data)) {
			throw std::runtime_error(
				"short read from " + path_ + ": got " +
				std::to_string(n) + " of " +
				std::to_string(sizeof(data)) + " bytes");
		}

		return data.distance_cm;
	}
}

bool DeviceSensorReader::send_command(const std::string& cmd)
{
	if (fd_ < 0)
		return false;

	ssize_t n = ::write(fd_, cmd.c_str(), cmd.size());
	if (n < 0) {
		/* FR-16: report, do not crash. The caller logs the failure. */
		return false;
	}
	return static_cast<size_t>(n) == cmd.size();
}

/* ------------------------------------------------------------------ */
/* SimulatedSensorReader                                               */
/* ------------------------------------------------------------------ */

SimulatedSensorReader::SimulatedSensorReader(int min_cm, int max_cm, int step_cm)
    : min_cm_(min_cm),
      max_cm_(max_cm),
      step_cm_(step_cm),
      distance_cm_(max_cm),
      direction_(-1),
      ticks_(0)
{
}

int SimulatedSensorReader::read_distance_cm()
{
	/* Mirror the driver: walk down, then back up, bouncing at the ends. */
	++ticks_;
	distance_cm_ += direction_ * step_cm_;

	if (distance_cm_ <= min_cm_) {
		distance_cm_ = min_cm_;
		direction_ = 1;
	} else if (distance_cm_ >= max_cm_) {
		distance_cm_ = max_cm_;
		direction_ = -1;
	}

	return distance_cm_;
}

void SimulatedSensorReader::reset()
{
	distance_cm_ = max_cm_;
	direction_ = -1;
	ticks_ = 0;
}
