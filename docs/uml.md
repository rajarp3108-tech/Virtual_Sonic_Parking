# UML Diagrams (ASCII version)

Renderable PlantUML sources are in [`uml/`](../uml/README.md). This file is the
tool-free version for the printed report. Both describe the same design.

## 1. Use Case Diagram

```
                          ┌──────────────────────────────────────────┐
                          │   Virtual Ultrasonic Parking Sensor      │
                          │                                          │
   ┌──────────┐           │  (UC1) Load / unload driver              │
   │          │───────────┼─▶      (insmod / rmmod, root)            │
   │          │           │                                          │
   │          │───────────┼─▶  (UC3) Send control command             │
   │ Operator │           │          reset / pause / resume          │
   │          │           │                                          │
   │          │───────────┼─▶  (UC4) Start monitor                    │
   │          │           │          foreground or daemon             │
   │          │           │                                          │
   │          │───────────┼─▶  (UC5) Stop monitor                     │
   │          │           │          Ctrl-C / SIGTERM                 │
   │          │           │                                          │
   │          │           │                                          │
   │          │───────────┼─▶  (UC6) Observe parking state            │
   │          │           │          SAFE / WARNING / DANGER          │
   │          │           │                                          │
   │          │───────────┼─▶  (UC7) Read the log file                │
   │          │           │                                          │
   │          │───────────┼─▶  (UC8) Query status over TCP             │
   │          │           │                                          │
   │          │───────────┼─▶  (UC9) Configure thresholds             │
   │          │           │          and polling interval             │
   │          │           │                                          │
   │          │           │                                          │
   │          │───────────┼─▶  (UC10) Run the automated tests         │
   └──────────┘           │                                          │
                          │                                          │
   ┌──────────┐           │  ┌────────────────────────────────────┐  │
   │  TCP     │───────────┼─▶│ (UC8) Query current status over TCP │  │
   │  Client  │           │  └────────────────────────────────────┘  │
   └──────────┘           └──────────────────────────────────────────┘

   ┌──────────┐
   │  Demo    │  watches the operator run the whole flow
   │ Audience │  «extends» nothing - a passive observer
   └──────────┘
```

Relationships that are easy to forget when drawing this:

- **(UC2) read the device** is a use case of the *monitor*, not of the
  operator. The operator never reads `/dev/parking_sensor` directly - that is
  the point of the driver interface.
- **(UC4) start monitor** `<<includes>>` **(UC2) read**, because the monitor
  cannot run without polling the device.
- **(UC6) observe state** is realised by the terminal output *and* by the log,
  so it is connected to (UC7) as well.

## 2. Class Diagram

```
  +-------------------+        implements          +--------------------------+
  | <<interface>>     |<--------------------------| DeviceSensorReader        |
  | ISensorSource     |                            |--------------------------|
  |-------------------|                            | - path_ : string         |
  | +read_distance_cm |                            | - fd_    : int           |
  | +send_command     |                            +--------------------------+
  | +name             |                            | +open()  +~dtor()        |
  +-------------------+                            | +read()  +write()        |
          ^                                          +--------------------------+
          |  implements
          |                                          +--------------------------+
          +-------------------------------------| SimulatedSensorReader     |
                                                     |--------------------------|
                                                     | - min_cm_ .. direction_  |
                                                     +--------------------------+

  +-------------------+        implements          +--------------------------+
  | <<interface>>     |<--------------------------| ThresholdPolicy          |
  | StatePolicy       |                            |--------------------------|
  |-------------------|                            | - warning_cm_ : int       |
  | +evaluate(d)      |                            | - danger_cm_  : int       |
  | +warning_thresh() |                            +--------------------------+
  | +danger_thresh()  |                            | +evaluate(d) : ParkingSt.|
  | +describe()       |                            +--------------------------+
  +-------------------+                                     ^
          ^                                                 | uses (reference)
          |                                                 |
          |                                    +--------------------------------+
          |                                    | StateManager                   |
          |                                    |--------------------------------|
          |                                    | - const StatePolicy& policy_  |
          |                                    | - current_      : ParkingState |
          |                                    | - initialised_  : bool         |
          |                                    | - transitions_  : long         |
          |                                    +--------------------------------+
          |                                    | +update(d, event) : bool      |
          |                                    | +current() +reset()           |
          |                                    +--------------------------------+

  +----------------------------------+     +--------------------------------+
  | TelemetryRecord                  |     | Logger                         |
  |----------------------------------|     |--------------------------------|
  | + timestamp   : long             |     | - path_   : string             |
  | + distance_cm : int              |<----| - out_    : ofstream           |
  | + state       : ParkingState     |     | - pending_: queue<string>      |
  | + event       : string           |     | - mtx_    : mutex              |
  +----------------------------------+     | - cv_     : condition_variable |
                                          | - worker_ : thread              |
  +----------------------------------+     | - running_: atomic<bool>        |
  | enum class ParkingState          |     +--------------------------------+
  |----------------------------------|     | +start() +log() +stop()        |
  | Safe  = 0                        |     +--------------------------------+
  | Warning = 1                      |
  | Danger = 2                      |     +--------------------------------+
  +----------------------------------+     | TCPStatusServer                |
                                          |--------------------------------|
  +----------------------------------+     | - bind_address_ : string        |
  | MonitorConfig                    |     | - port_ / listen_fd_ : int     |
  |----------------------------------|     | - latest_message_ : string     |
  | + device_path        : string    |     | - mtx_   : mutex               |
  | + log_path           : string    |     | - thread_ : thread             |
  | + poll_interval_ms   : int       |     +--------------------------------+
  | + warning_threshold_cm : int    |     | +start() +publish() +stop()    |
  | + danger_threshold_cm  : int    |     +--------------------------------+
  | + tcp_enabled / bind_address          ^
  | + tcp_port                     : int | owns (unique_ptr)
  | + static load(path)            |     |
  +----------------------------------+     |
                                    +---------------------------------------+
                                    | ParkingMonitor  «MainController»      |
                                    |---------------------------------------|
                                    | - config_ : MonitorConfig            |
                                    | - source_ : unique_ptr<ISensorSource>|
                                    | - state_  : unique_ptr<StateManager> |
                                    | - logger_ : unique_ptr<Logger>       |
                                    | - server_ : unique_ptr<TCPStatus..>  |
                                    | - stop_requested_ : atomic<bool>      |
                                    | - max_iterations_ / iterations_      |
                                    |---------------------------------------|
                                    | +run() +request_stop()               |
                                    | +set_max_iterations(n)               |
                                    | +iterations() +read_errors()          |
                                    +---------------------------------------+
                                                    ^
                                                    | one request/response per connection
                                       +--------------------------------------------+
                                       | status_client   (separate process)            |
                                       | connect() -> "STATUS distance=37 state=..."  |
                                       +--------------------------------------------+
```

Two design points to be able to explain:

1. **`ParkingMonitor` composes its services (`*--`), it does not inherit
   from them.** The monitor *has-a* reader, *has-a* state manager.
2. **`StateManager` holds a `const StatePolicy&`, not a copy of a rule.** That
   single design choice is what makes the Open/Closed Principle real: a new
   rule is a new class implementing `StatePolicy`, and no existing line of
   code changes. `tests/test_state_manager.cpp` proves it with
   `AlwaysSafePolicy`.

## 3. Sequence Diagram

```
 Client            main()        ParkingMonitor     DeviceSensorReader    Driver
   |                  |               |                   |                |
   |                  | MonitorConfig::load()             |                |
   |                  | build object graph                |                |
   |                  |-------------->|                   |                |
   |                  |               | Logger::start()   |                |
   |                  |               | TCPStatusServer::start()          |
   |                  |               |                   |                |
   |                  |               |== poll loop, every poll_interval_ms ==|
   |                  |               |                   |                |
   |                  |               |---read_distance_cm()------------->|
   |                  |               |                   |--- read(fd) -->|
   |                  |               |                   |<-- copy_to_user -|
   |                  |               |<-- distance_cm ---|                |
   |                  |               |                   |                |
   |                  |               |-- update(d, &event) -> StateManager |
   |                  |               |      evaluate(): d > 50 ? SAFE     |
   |                  |               |<-- changed?, state                 |
   |                  |               |                   |                |
   |                  |               |-- log(record) ---> Logger thread    |
   |                  |               |                   |--> append to log file
   |                  |               |-- publish(d,state) -> TCPStatusServer|
   |                  |               |                   |                |
   |                  |               |  sleep 50 ms slices (signal safe)    |
   |                  |               |                   |                |
   |  connect(9000) ------------------------------------------------------------>|
   |                  |               |<-- latest_message_ ----------------- |
   |<---------------- STATUS distance=37 state=WARNING --------------------------|
   |  close           |               |                                        |
   |                  |               |                                        |
   |                  | SIGINT ------>|  handler sets g_stop_requested       |
   |                  |               |  loop exits -> TCPStatusServer::stop |
   |                  |               |            -> Logger::stop (drain+join)
   |                  |<-- summary ---|                                        |
   |                  | exit 0        |                                        |
```

Order matters in the shutdown sequence: stop accepting clients **first**, then
drain the log, so the "monitor_stop" line is guaranteed to be the last line in
the file and is actually written.

## 4. State Machine Diagram

```
                    distance > 50 cm
        ┌───────────────────────────────────┐
        │                                   ▼
   ┌─────────┐   distance <= 50 cm    ┌───────────┐
   │  SAFE   │ ──────────────────────▶ │ WARNING   │
   │ > 50 cm │ ◀────────────────────── │ 20..50 cm │
   └─────────┘   distance > 50 cm     └───────────┘
        ▲                                   │
        │                                   │ distance <= 20 cm
        │                                   ▼
        │      distance > 20 cm     ┌───────────┐
        └────────────────────────────│  DANGER   │
                                     │  <= 20 cm │
                                     └───────────┘
                                            │
                                            ▼ monitor stopped
                                          [*]

   first reading ──────────────────▶  any state directly
   (the machine initialises to whatever the first distance means)
```

Threshold table, and the exact source line that implements it:

| State | Condition | Code |
|---|---|---|
| `DANGER` | `distance <= 20` | `if (distance_cm <= danger_cm_) return ParkingState::Danger;` |
| `SAFE` | `distance > 50` | `if (distance_cm > warning_cm_) return ParkingState::Safe;` |
| `WARNING` | everything else | `return ParkingState::Warning;` |

Boundary decisions, which examiners ask about:

- exactly `50` cm is **WARNING** (`>` is strict, so 50 is not SAFE);
- exactly `20` cm is **DANGER** (`<=`, so 20 is DANGER and not WARNING);
- therefore the WARNING band is the half-open interval `(20, 50]`;
- a negative distance is **DANGER** by construction - there is no branch that
  lets a corrupt reading fall through into SAFE.
