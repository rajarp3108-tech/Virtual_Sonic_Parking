#include "telemetry.h"

#include <cstdio>
#include <ctime>

std::string format_timestamp(long epoch_seconds)
{
	std::time_t t = static_cast<std::time_t>(epoch_seconds);
	std::tm tm_buf{};

	/* localtime_r is the thread-safe version of localtime. */
	if (localtime_r(&t, &tm_buf) == nullptr)
		return "0000-00-00 00:00:00";

	char buf[32];
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
		      tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
		      tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
	return std::string(buf);
}

std::string format_record(const TelemetryRecord& rec)
{
	std::string out;
	out.reserve(96);
	out += format_timestamp(rec.timestamp);
	out += " | distance=";
	out += std::to_string(rec.distance_cm);
	out += " | state=";
	out += to_string(rec.state);
	out += " | event=";
	out += rec.event.empty() ? "none" : rec.event;
	return out;
}
