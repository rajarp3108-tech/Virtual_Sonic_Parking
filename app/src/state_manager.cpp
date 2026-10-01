#include "state_manager.h"

#include <stdexcept>

ThresholdPolicy::ThresholdPolicy(int warning_cm, int danger_cm)
    : warning_cm_(warning_cm), danger_cm_(danger_cm)
{
	if (danger_cm >= warning_cm) {
		throw std::invalid_argument(
			"ThresholdPolicy: danger threshold must be below the "
			"warning threshold");
	}
}

ParkingState ThresholdPolicy::evaluate(int distance_cm) const
{
	/* Boundary handling follows the SRS exactly:
	 *   distance >  warning  -> SAFE
	 *   distance <= danger   -> DANGER
	 *   otherwise            -> WARNING                                            */
	if (distance_cm <= danger_cm_)
		return ParkingState::Danger;
	if (distance_cm > warning_cm_)
		return ParkingState::Safe;
	return ParkingState::Warning;
}

std::string ThresholdPolicy::describe() const
{
	return "ThresholdPolicy(SAFE if d > " + std::to_string(warning_cm_) +
	       " cm, DANGER if d <= " + std::to_string(danger_cm_) +
	       " cm, otherwise WARNING)";
}

/* ------------------------------------------------------------------ */

StateManager::StateManager(const StatePolicy& policy)
    : policy_(policy),
      current_(ParkingState::Safe),
      initialised_(false),
      transitions_(0)
{
}

ParkingState StateManager::evaluate(int distance_cm) const
{
	return policy_.evaluate(distance_cm);
}

bool StateManager::update(int distance_cm, std::string* out_event)
{
	ParkingState next = policy_.evaluate(distance_cm);

	/* The very first reading is reported as an event so the log always
	 * shows the state the monitor started in. */
	if (!initialised_) {
		initialised_ = true;
		current_ = next;
		if (out_event)
			*out_event = "state_change";
		return true;
	}

	if (next != current_) {
		current_ = next;
		++transitions_;
		if (out_event)
			*out_event = "state_change";
		return true;
	}

	if (out_event)
		*out_event = "periodic_read";
	return false;
}

void StateManager::reset()
{
	initialised_ = false;
	current_ = ParkingState::Safe;
	transitions_ = 0;
}
