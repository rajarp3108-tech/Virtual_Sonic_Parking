/*
 * state_manager.h - FR-07 / FR-08: distance -> SAFE | WARNING | DANGER,
 * plus change detection so the application knows when to raise an event.
 *
 * The threshold rule lives behind the StatePolicy interface, so the policy
 * can be replaced (Open/Closed Principle) without touching the monitor,
 * the logger or the socket code.
 */
#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <string>

#include "telemetry.h"

/* Abstract policy: "given a distance, what is the state?" */
class StatePolicy {
public:
	virtual ~StatePolicy() = default;
	virtual ParkingState evaluate(int distance_cm) const = 0;
	virtual int warning_threshold_cm() const = 0;
	virtual int danger_threshold_cm() const = 0;
	virtual std::string describe() const = 0;
};

/* The default policy from the SRS:
 *   distance >  50 cm            -> SAFE
 *   20 cm < distance <= 50 cm    -> WARNING
 *   distance <= 20 cm            -> DANGER
 */
class ThresholdPolicy : public StatePolicy {
public:
	ThresholdPolicy(int warning_cm, int danger_cm);

	ParkingState evaluate(int distance_cm) const override;
	int warning_threshold_cm() const override { return warning_cm_; }
	int danger_threshold_cm() const override { return danger_cm_; }
	std::string describe() const override;

private:
	int warning_cm_;
	int danger_cm_;
};

/* Stateful wrapper: remembers the last state and counts transitions. */
class StateManager {
public:
	explicit StateManager(const StatePolicy& policy);

	ParkingState evaluate(int distance_cm) const;

	/*
	 * Update the machine with a new distance.
	 * Returns true when the state changed (an event worth logging loudly).
	 * *out_event is set to "state_change" or "periodic_read".
	 */
	bool update(int distance_cm, std::string* out_event);

	ParkingState current() const { return current_; }
	bool initialised() const { return initialised_; }
	long transition_count() const { return transitions_; }
	const StatePolicy& policy() const { return policy_; }

	void reset();

private:
	const StatePolicy& policy_;
	ParkingState current_;
	bool initialised_;
	long transitions_;
};

#endif /* STATE_MANAGER_H */
