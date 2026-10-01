/*
 * telemetry.h - core data types shared by every part of the application.
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <ctime>
#include <string>
#include <utility>

/* The three parking states of the state machine. */
enum class ParkingState { Safe = 0, Warning = 1, Danger = 2 };

/* Uppercase name used in the log file and on the TCP wire: SAFE/WARNING/DANGER. */
inline const char* to_string(ParkingState s)
{
	switch (s) {
	case ParkingState::Safe:
		return "SAFE";
	case ParkingState::Warning:
		return "WARNING";
	case ParkingState::Danger:
		return "DANGER";
	}
	return "UNKNOWN";
}

/* One observation of the sensor, plus the derived state and event text.
 * This is the single structure that travels from the reader, through the
 * state manager, into the logger and the TCP server. */
struct TelemetryRecord {
	long timestamp = 0; /* seconds since epoch, filled with time(nullptr) */
	int distance_cm = 0; /* current simulated distance                    */
	ParkingState state = ParkingState::Safe;
	std::string event; /* "periodic_read" or "state_change"              */

	TelemetryRecord() = default;
	TelemetryRecord(int d, ParkingState s, std::string e)
	    : timestamp(static_cast<long>(::time(nullptr))),
	      distance_cm(d),
	      state(s),
	      event(std::move(e))
	{
	}
};

/* Log line format, exactly as required by the SRS (Appendix B):
 *   2026-09-28 19:02:11 | distance=80 | state=SAFE | event=periodic_read
 * Kept as a free function so the unit test can verify it without a file. */
std::string format_timestamp(long epoch_seconds);
std::string format_record(const TelemetryRecord& rec);

#endif /* TELEMETRY_H */
