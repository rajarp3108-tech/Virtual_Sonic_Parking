# Stage Evidence and Git Plan

This is the record an evaluator checks. Six stages, each with a focus, a
deliverable and a commit. Do the work in this order and commit at the end of
each stage - the history *is* the evidence of incremental development.

## 1. Repository setup (once)

```bash
cd Wipro-Virtual_Ultrasonic_Parking
git init
git branch -M main
git add .
git commit -m "chore: initial project structure, SRS, docs skeleton"
```

Branch strategy from the SRS:

```
main
 ├── feature/driver        -> driver/parking_sensor.c
 ├── feature/cpp-monitor   -> app/
 ├── feature/tcp           -> app/src/tcp_status_server.cpp, client/
 ├── feature/testing       -> tests/, scripts/test_all.sh
 └── docs/stage-evidence   -> docs/
```

Workflow per stage:

```bash
git switch -c feature/driver      # or main for a docs-only stage
git add <files>
git commit -m "feat(driver): character device with kernel timer simulation"
git switch main
git merge --no-ff feature/driver
```

Use `--no-ff` so `main` shows a merge commit per stage; the graph is then
readable evidence of a staged process.

## 2. The six stages

### Stage 1 - Project introduction (days 1-2)

| Item | Content |
|---|---|
| Focus | problem statement, objectives, scope, use case, expected outcome |
| Do | read the SRS; write a half-page proposal; draw the use case; set up the repository |
| Commit | `docs: project proposal, problem statement and use case` |
| Evidence | this file, `docs/requirements.md` section 1, `uml/use_case.puml` |
| Done when | you can say the problem and the scope in 60 seconds without notes |

### Stage 2 - Requirements and plan (days 3-5)

| Item | Content |
|---|---|
| Focus | functional and non-functional requirements, PRD/SRS, modules, timeline, test plan |
| Do | finalise `docs/requirements.md`; write the test plan; create the backlog |
| Commit | `docs: requirements, traceability matrix and test plan` |
| Evidence | `docs/requirements.md`, `docs/test-report.md` part 1, `docs/syllabus-traceability.md` |
| Done when | every FR id maps to a file and a test |

### Stage 3 - Design and architecture (days 6-9)

| Item | Content |
|---|---|
| Focus | layered architecture, UML, data structures, Linux/device interface, branch plan |
| Do | write `docs/architecture.md`; draw all four UML diagrams; decide the struct in `include/parking_sensor.h` |
| Commit | `feat(design): layered architecture, UML diagrams and shared device struct` |
| Evidence | `docs/architecture.md`, `uml/*.puml`, `docs/uml.md`, `include/parking_sensor.h` |
| Done when | you can draw the data flow on a whiteboard without looking |

### Stage 4 - Prototype (days 10-13)

| Item | Content |
|---|---|
| Focus | driver skeleton, `/dev/parking_sensor`, C++ reader, state machine, first log output |
| Do | the driver, then `DeviceSensorReader`, then `ThresholdPolicy`/`StateManager`, then `Logger` |
| Branches | `feature/driver`, then `feature/cpp-monitor` |
| Commits | `feat(driver): char device + kernel timer` / `feat(app): sensor reader and three-state machine` / `feat(app): threaded file logger` |
| Evidence | `scripts/load_driver.sh` output, a screenshot of the first log lines |
| Done when | `./build/parking_monitor --no-tcp` prints states and writes the log |
| Note | if the college machine will not load a module, prove the user-space half with `--simulate` and note it honestly in the report |

### Stage 5 - Testing and integration (days 14-17)

| Item | Content |
|---|---|
| Focus | TCP server/client, process and signal handling, tests, debugging, fixes |
| Do | the TCP server and client, `tests/`, `scripts/test_all.sh`, `scripts/debug_helper.sh` |
| Branches | `feature/tcp`, `feature/testing` |
| Commits | `feat(net): IPv4 TCP status server and client` / `test: 41 unit and integration cases with a dependency-free harness` / `fix: shutdown the listening socket before joining the accept thread` |
| Evidence | `docs/test-report.md` part 2 filled in, the test output in the report |
| Done when | `sudo ./scripts/test_all.sh all` passes and the report has real numbers |

### Stage 6 - Final implementation (days 18-20)

| Item | Content |
|---|---|
| Focus | polish, documentation, final diagrams, Git history, presentation |
| Do | fill the report, add UML renders, practise the demo, write the challenges table |
| Commit | `docs: final report, test results, demo script and interview preparation` |
| Evidence | the complete repository, `docs/demo-script.md` rehearsed |
| Done when | the demo runs top to bottom in under 10 minutes without a hiccup |

## 3. Suggested commit list

Small, meaningful commits in this order:

```
chore: initial project structure, SRS and docs skeleton
docs: project proposal, problem statement and use case
docs: requirements, traceability matrix and test plan
docs: layered architecture and UML diagrams
build: makefiles for driver, application, client and tests
feat(driver): register character device parking_sensor
feat(driver): kernel timer moves the simulated distance
feat(driver): read and write with copy_to_user / copy_from_user
feat(app): DeviceSensorReader with RAII on the file descriptor
feat(app): ThresholdPolicy and three-state machine
feat(app): config file for thresholds and poll interval
feat(app): threaded logger writing the log file
feat(net): IPv4 TCP status server
feat(net): status client with reconnect support
feat(app): control loop, signal handling and daemon mode
test: dependency-free harness plus state machine tests
test: config, logging, sensor and TCP tests
test: end-to-end monitor integration tests
feat(scripts): build, load, demo, test and cleanup scripts
docs: test report, syllabus traceability, demo and interview notes
```

Do not squash. The number of commits and their messages are part of what is
being assessed.

## 4. Agile framing

The project was run as a hybrid of Scrum and Kanban, which fits a solo
six-stage plan better than either in pure form.

| Ceremony | Duration | Output |
|---|---|---|
| Sprint planning | 30 min per stage | the commit list above becomes the stage backlog |
| Daily stand-up (self) | 5 min | "done / next / blocked" in the commit message or a scratch note |
| Sprint review | 20 min per stage | the stage evidence table is filled in |
| Retrospective | 15 min per stage | one line in the challenges table, `docs/test-report.md` 2.6 |
| Backlog refinement | before each stage | the test cases for that stage are written before the code |

Kanban side: the work is pulled stage by stage, and a stage is not started
until the previous one is merged and its evidence recorded.

**Definition of Done**, applied to every stage:

1. the code compiles with no new warnings,
2. the tests for that stage pass,
3. the documentation for that stage is updated,
4. the commit message explains *why*, not just *what*,
5. the evidence row in this file is filled in.

## 5. Evidence to collect as you go

| Evidence | Where to put it | When |
|---|---|---|
| `dmesg` driver banner | screenshot + text in this file | stage 4 |
| `/dev/parking_sensor` listing | screenshot | stage 4 |
| first log lines | screenshot | stage 4 |
| test output | `docs/test-report.md` 2.3 | stage 5 |
| client output | `logs/client_output.log` | stage 5 |
| gdb / readelf evidence | `docs/debugging-notes.md` | stage 5 |
| final demo screenshots | `docs/stage-evidence/` | stage 6 |
| `git log --graph` | screenshot | stage 6 |

Create `docs/stage-evidence/` and drop the screenshots in as you go:

```bash
mkdir -p docs/stage-evidence
cp /path/to/screenshot.png docs/stage-evidence/stage4-driver-load.png
git add docs/stage-evidence
```

## 6. Stage completion table

Fill in as you finish each stage. This is the table an evaluator reads first.

| Stage | Days | Branch | Merged | Evidence attached | Reviewer note |
|---|---|---|---|---|---|
| 1 Introduction | 1-2 | main | _(date)_ | proposal, use case | |
| 2 Requirements | 3-5 | main | _(date)_ | FR list, test plan | |
| 3 Design | 6-9 | main | _(date)_ | architecture, UML x4 | |
| 4 Prototype | 10-13 | feature/driver, feature/cpp-monitor | _(date)_ | dmesg, device node, first log | |
| 5 Testing | 14-17 | feature/tcp, feature/testing | _(date)_ | test report with numbers | |
| 6 Final | 18-20 | docs/stage-evidence | _(date)_ | report, demo, screenshots | |
