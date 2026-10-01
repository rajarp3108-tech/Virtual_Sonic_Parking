/*
 * parking_sensor.h - Shared contract between the kernel driver and user space.
 *
 * This is the ONLY header shared by both worlds. It contains no kernel-only
 * and no user-space-only symbols, so it can be included from
 * driver/parking_sensor.c and from the C++ application safely.
 *
 * Data path:
 *   kernel timer  ->  driver state  ->  copy_to_user()  ->  read(fd)
 *   write(fd)     ->  copy_from_user()  ->  driver control state
 */
#ifndef PARKING_SENSOR_H
#define PARKING_SENSOR_H

#define PARKING_SENSOR_NAME    "parking_sensor"
#define PARKING_SENSOR_DEVNAME "/dev/parking_sensor"
#define PARKING_SENSOR_CLASS   "parking_sensor"

/* Commands accepted by write() on the device file. Keep them short. */
#define PARKING_SENSOR_CMD_RESET  "reset" /* restart the sequence at max_cm  */
#define PARKING_SENSOR_CMD_PAUSE  "pause" /* stop the timer from advancing   */
#define PARKING_SENSOR_CMD_RESUME "resume"

/*
 * Payload returned by one read() call.
 *
 * A fixed-size struct is used on purpose: it makes the user/kernel transfer
 * a single copy_to_user() and keeps the userspace parser trivial. A real
 * product would add a version field and use ioctl() for configuration.
 */
struct parking_sensor_data {
	int distance_cm; /* current simulated distance, centimetres        */
	int min_cm;      /* lower bound of the simulated sequence           */
	int max_cm;      /* upper bound of the simulated sequence           */
	int step_cm;     /* distance change applied on every timer tick     */
	int ticks;       /* timer ticks since module load or last reset     */
	int paused;      /* 1 = simulation frozen, 0 = simulation running   */
};

#endif /* PARKING_SENSOR_H */
