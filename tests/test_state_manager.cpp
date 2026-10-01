/*
 * test_state_manager.cpp - unit tests for FR-07 (distance -> state) and
 * FR-08 (change detection). These are the tests that matter most: the
 * threshold logic is the business rule of the whole project.
 */
#include "state_manager.h"

#include <stdexcept>

#include "test_harness.h"


/* SRS section 5.3: SAFE > 50, WARNING 20 < d <= 50, DANGER <= 20. */
void test_threshold_boundaries()
{
	ThresholdPolicy policy(50, 20);

	CHECK(policy.evaluate(150) == ParkingState::Safe);
	CHECK(policy.evaluate(51) == ParkingState::Safe);
	CHECK(policy.evaluate(50) == ParkingState::Warning); /* boundary belongs to WARNING */
	CHECK(policy.evaluate(49) == ParkingState::Warning);
	CHECK(policy.evaluate(21) == ParkingState::Warning);
	CHECK(policy.evaluate(20) == ParkingState::Danger); /* boundary belongs to DANGER */
	CHECK(policy.evaluate(19) == ParkingState::Danger);
	CHECK(policy.evaluate(10) == ParkingState::Danger);
	CHECK(policy.evaluate(0) == ParkingState::Danger);
}

void test_threshold_rejects_nonsense_arguments()
{
	/* A policy where danger >= warning could never be satisfied. */
	bool threw = false;
	try {
		ThresholdPolicy bad(20, 50);
		(void)bad;
	} catch (const std::invalid_argument&) {
		threw = true;
	}
	CHECK(threw);
}

void test_custom_thresholds_are_honoured()
{
	ThresholdPolicy policy(100, 60);
	CHECK(policy.evaluate(120) == ParkingState::Safe);
	CHECK(policy.evaluate(100) == ParkingState::Warning);
	CHECK(policy.evaluate(60) == ParkingState::Danger);
	CHECK_CONTAINS(policy.describe(), "100");
}

/* OCP: a policy is just another class implementing StatePolicy. */
class AlwaysSafePolicy : public StatePolicy {
public:
	ParkingState evaluate(int) const override { return ParkingState::Safe; }
	int warning_threshold_cm() const override { return 0; }
	int danger_threshold_cm() const override { return 0; }
	std::string describe() const override { return "AlwaysSafePolicy"; }
};

void test_policy_is_substitutable()
{
	AlwaysSafePolicy policy;
	StateManager sm(policy);
	CHECK(sm.evaluate(1) == ParkingState::Safe);
	CHECK(sm.evaluate(999) == ParkingState::Safe);
}

void test_first_reading_is_reported_as_an_event()
{
	ThresholdPolicy policy(50, 20);
	StateManager sm(policy);
	std::string event;

	CHECK(sm.initialised() == false);
	CHECK(sm.update(120, &event) == true);
	CHECK(event == "state_change");
	CHECK(sm.current() == ParkingState::Safe);
	CHECK(sm.initialised() == true);
}

void test_steady_state_is_a_periodic_read()
{
	ThresholdPolicy policy(50, 20);
	StateManager sm(policy);
	std::string event;

	sm.update(120, &event);
	CHECK(sm.update(110, &event) == false);
	CHECK(event == "periodic_read");
	CHECK(sm.transition_count() == 0);
}

/* The full SAFE -> WARNING -> DANGER -> WARNING -> SAFE walk. */
void test_full_transition_sequence()
{
	ThresholdPolicy policy(50, 20);
	StateManager sm(policy);
	std::string event;

	const int sequence[] = {150, 120, 90,  60,  50,  40,  30, 20,
				10,  20,  30,  40,  50,  60,  90, 150};
	const int n = static_cast<int>(sizeof(sequence) / sizeof(sequence[0]));

	/* The first sample only initialises the machine. */
	sm.update(sequence[0], &event);
	CHECK(event == "state_change");

	int changes = 0;
	ParkingState prev = sm.current();

	for (int i = 1; i < n; ++i) {
		sm.update(sequence[i], &event);
		if (sm.current() != prev) {
			++changes;
			CHECK(event == "state_change");
		} else {
			CHECK(event == "periodic_read");
		}
		prev = sm.current();
	}

	/* 150 S | 60 W | 20 D | 30 W | 90 S  -> four state changes. */
	CHECK(changes == 4);
	CHECK(sm.transition_count() == changes);
	CHECK(sm.current() == ParkingState::Safe);
}

void test_reset_clears_the_machine()
{
	ThresholdPolicy policy(50, 20);
	StateManager sm(policy);
	std::string event;

	sm.update(150, &event);
	sm.update(10, &event);
	CHECK(sm.transition_count() > 0);

	sm.reset();
	CHECK(sm.initialised() == false);
	CHECK(sm.transition_count() == 0);
	CHECK(sm.current() == ParkingState::Safe);
}

void test_negative_distance_is_danger()
{
	/* Defensive: a corrupt reading must not fall through the policy. */
	ThresholdPolicy policy(50, 20);
	CHECK(policy.evaluate(-5) == ParkingState::Danger);
}

TEST_LIST(TEST_ENTRY(test_threshold_boundaries)
	  TEST_ENTRY(test_threshold_rejects_nonsense_arguments)
	  TEST_ENTRY(test_custom_thresholds_are_honoured)
	  TEST_ENTRY(test_policy_is_substitutable)
	  TEST_ENTRY(test_first_reading_is_reported_as_an_event)
	  TEST_ENTRY(test_steady_state_is_a_periodic_read)
	  TEST_ENTRY(test_full_transition_sequence)
	  TEST_ENTRY(test_reset_clears_the_machine)
	  TEST_ENTRY(test_negative_distance_is_danger))
