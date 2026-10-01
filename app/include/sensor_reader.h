/*
 * sensor_reader.h - the user-space side of the /dev/parking_sensor interface.
 *
 * ISensorSource is the abstraction the rest of the application depends on
 * (Dependency Inversion). The monitor never knows whether the number came
 * from the kernel driver or from the in-process simulator, so the same
 * code path is used for the real demo and for the unit tests.
 */
#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include <string>

#include "parking_sensor.h"

class ISensorSource {
public:
	virtual ~ISensorSource() = default;

	/* Return the current distance in cm.
	 * Throws std::runtime_error on a recoverable device failure. */
	virtual int read_distance_cm() = 0;

	/* Optional control channel; returns false if unsupported. */
	virtual bool send_command(const std::string& /*cmd*/)
	{
		return false;
	}

	virtual const char* name() const = 0;
};

/*
 * Real reader: one open file descriptor on /dev/parking_sensor.
 * RAII - the destructor closes the descriptor, so no early return can leak it.
 */
class DeviceSensorReader : public ISensorSource {
public:
	explicit DeviceSensorReader(const std::string& path);
	~DeviceSensorReader() override;

	DeviceSensorReader(const DeviceSensorReader&) = delete;
	DeviceSensorReader& operator=(const DeviceSensorReader&) = delete;

	int read_distance_cm() override;
	bool send_command(const std::string& cmd) override;
	const char* name() const override { return "device"; }

	int fd() const { return fd_; }
	const std::string& path() const { return path_; }

private:
	std::string path_;
	int fd_;
};

/*
 * In-process generator with the same triangle-wave behaviour as the driver
 * (start high, walk down to the minimum, walk back up). Used for:
 *   - `--simulate` demo mode, so the user-space half can be demonstrated
 *     without root, a kernel build or QEMU;
 *   - unit tests of the state machine.
 * It is NOT a replacement for the driver and is not used in the live path.
 */
class SimulatedSensorReader : public ISensorSource {
public:
	SimulatedSensorReader(int min_cm = 10, int max_cm = 150, int step_cm = 10);

	int read_distance_cm() override;
	const char* name() const override { return "simulated"; }

	void reset();
	void set_step(int step_cm) { step_cm_ = step_cm; }
	int ticks() const { return ticks_; }

private:
	int min_cm_;
	int max_cm_;
	int step_cm_;
	int distance_cm_;
	int direction_;
	int ticks_;
};

#endif /* SENSOR_READER_H */
