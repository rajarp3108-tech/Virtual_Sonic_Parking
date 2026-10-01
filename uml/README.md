# UML Diagrams

The four required diagrams live here as [PlantUML](https://plantuml.com) text files.
PlantUML source is plain text, so the diagrams are reviewable in Git and can be
rendered in three ways.

| File | Diagram | SRS section |
|---|---|---|
| `use_case.puml` | Use case | 15 |
| `class_diagram.puml` | Class | 15 |
| `sequence_diagram.puml` | Sequence | 15 |
| `state_machine.puml` | State machine | 15 |

## How to render

**Option 1 - online, nothing to install.** Paste the file content into
<https://www.plantuml.com/plantuml/uml/> and download the PNG.

**Option 2 - local jar.**

```bash
curl -o plantuml.jar https://repo1.maven.org/maven2/net/sourceforge/plantuml/plantuml/1.2024.6/plantuml-1.2024.6.jar
for f in uml/*.puml; do
    java -jar plantuml.jar -tpng "$f"
done
```

**Option 3 - no tool at all.** `docs/uml.md` contains an ASCII version of all
four diagrams. That file is the primary version for the printed report, so the
submission never depends on rendering anything.

## Keep the diagram and the code in sync

The class diagram is not a drawing, it is a promise about the real classes.
These are the exact files it describes.

| Class in the diagram | Implemented in |
|---|---|
| `ParkingMonitor` (MainController) | `app/include/parking_monitor.h`, `app/src/parking_monitor.cpp` |
| `ISensorSource`, `DeviceSensorReader`, `SimulatedSensorReader` | `app/include/sensor_reader.h`, `app/src/sensor_reader.cpp` |
| `StatePolicy`, `ThresholdPolicy`, `StateManager` | `app/include/state_manager.h`, `app/src/state_manager.cpp` |
| `Logger` | `app/include/logger.h`, `app/src/logger.cpp` |
| `TCPStatusServer` | `app/include/tcp_status_server.h`, `app/src/tcp_status_server.cpp` |
| `TelemetryRecord`, `ParkingState` | `app/include/telemetry.h`, `app/src/telemetry.cpp` |
| `MonitorConfig` | `app/include/config.h`, `app/src/config.cpp` |
| `status_client` | `client/status_client.cpp` |
| driver side of the sequence diagram | `driver/parking_sensor.c` |

If you add a method, add it to `class_diagram.puml` too. Examiners do notice.
