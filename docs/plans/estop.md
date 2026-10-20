# E-stop plan (branch `dev-estop`)

Status: implemented on `dev-estop` (firmware, shared message, bench sim) and bench-tested on
the Tab5 (section 10). Section 9 lists where the build differs from this plan.

Scope: an e-stop switch on the HMI (GPIO2 or GPIO47, NO or NC, chosen in Settings)
that engages a robot-wide stop over RTPS. Only the MCB can end it. The MCB also
runs a deadman: if a node goes silent, that engages the stop too.

## 1. Message

The ROS EStop format (`std_msgs/Header`, `engaged`, `source`, `reason`; the bool `engaged` is
the `EStopState state` enum here, same octet values) with three
fields appended. Because they are appended, a node that only knows the 4-field ROS
layout still decodes the start of the message correctly. Being ROS 2 compatible is
not a goal: the topic and type names follow the rammp convention.

```cpp
// messages/estop_message.hpp (rammp-rtps)
#define RAMMP_TOPIC_ESTOP "rammp/estop"     // broadcast: no publisher segment, any node publishes
#define RAMMP_TYPE_ESTOP "rammp/msg/EStop"

struct Time { int32_t sec; uint32_t nanosec; };      // builtin_interfaces/Time
struct Header { Time stamp; std::string frame_id; }; // std_msgs/Header

struct EStop {
  Header header;          // stamp: for the log only, never for decisions; all-zero = unstamped
  EStopState state;       // ENGAGED or NOT_ENGAGED (one octet, the ROS bool's values)
  std::string source;     // who published it ("hmi", "mib", ...)
  std::string reason;     // free text, for the log
  // appended to the ROS layout
  uint32_t pub_seq;         // this publisher's message number: +1 per message, wraps
  std::string clear_target; // clear attempts only: whose engagement; "" otherwise
  uint32_t clear_pub_seq;   // clear attempts only: newest pub_seq the clearer had received
                            // from clear_target when the attempt started
};
inline constexpr Topic<EStop> kEStop{RAMMP_TOPIC_ESTOP, RAMMP_TYPE_ESTOP};
inline constexpr char kEStopClearer[] = "mib"; // the only source whose clear attempts count
```

| state | clear_target | Meaning | Who sends it |
|---|---|---|---|
| ENGAGED | (any) | Engage. Always acted on, with no age or `pub_seq` check. | Any node |
| NOT_ENGAGED | `""` | Heartbeat: "alive, not engaging". It never ends the stop. | Any node, every period |
| NOT_ENGAGED | `"<src>"` | Clear attempt for `<src>`'s engagement | `kEStopClearer` only |

Terms: a **clear attempt** is the clearer's request to end the stop; the stop is
**released** only when the attempt succeeds. Between the two the stop stays in effect.

This differs from the ROS comment, where `false` means "clear it". Here a plain `NOT_ENGAGED`
is only a heartbeat, so one node can't release another node's stop by sending its
normal heartbeat. A node that only knows the 4-field layout decodes `pub_seq = 0` and
`clear_target = ""`, so its messages read as heartbeats or engages, never as clear attempts.

The topic name is 11 characters. espp builds a `std::string(topic)` on every publish,
and a name of 15 characters or fewer stays in libstdc++'s short-string buffer, which
uses no heap.

## 2. Timing policy (configurable in code)

```cpp
struct EStopPolicy {
  milliseconds period{100};        // every node sends at least this often
  milliseconds timeout{300};       // receiver: a source silent this long counts as engaged (if deadman)
  bool deadman{true};              // false: only ENGAGED stops; silence is just logged
  milliseconds clear_window{1000}; // MCB: a clear attempt not completed within this expires; still latched
};
inline constexpr EStopPolicy kEStopPolicy{};
```

It lives in the shared spec, so the MCB, the HMI and the bench sim read the same
values. The sim takes flags to override them (`--estop-timeout`, `--no-deadman`).

## 3. Staleness: receive time on the steady clock, never the stamp

- **Engaging is never age-checked.** Any `ENGAGED` engages, however late it arrives.
  RTPS readers already drop samples older than the last one delivered from the same
  writer, so a late `ENGAGED` can't follow a newer `NOT_ENGAGED` from that writer. If one
  somehow did, it would cause an extra stop, which is safe.
- **Clearing is age-checked, by receive time:**
  - MCB: a release needs the source's `NOT_ENGAGED` with `pub_seq > clear_pub_seq`, received within
    `timeout` of the moment it releases.
  - HMI: a clear attempt counts only if the e-stop task takes it from the queue within
    one `period` of receiving it. A task held up by a flash write can't act on an old one.
- **Deadman:** the MCB records when it last heard from each source. Silence longer
  than `timeout` latches the stop.
- **The HMI watches the MCB:** no MCB message for longer than `timeout` shows
  "E-STOP LINK LOST" and refuses driving.

## 4. Clear attempt

Roles: each node publishes its own demand. The MCB owns the latch and publishes it
every period: `ENGAGED` with the cause in `reason` while latched, a clear-attempt message
(`NOT_ENGAGED` plus `clear_target` and `clear_pub_seq`) during a clear attempt, and a plain `NOT_ENGAGED` after
releasing.

HMI states:

- **NOT_ENGAGED:** sends `NOT_ENGAGED` every period (the heartbeat).
- **ENGAGED:** sends `ENGAGED` every period, including after the switch is released.

HMI rules:

- A press edge enters ENGAGED and sends at once, without waiting for the tick.
- `engaged_pub_seq` is the `pub_seq` of the first `ENGAGED` sent after the latest press.
  Pressing again while engaged sets a new `engaged_pub_seq`.
- The HMI accepts a clear attempt only if all of these hold:
  - `source == kEStopClearer`
  - `clear_target == "hmi"`
  - `clear_pub_seq >= engaged_pub_seq`
  - the switch isn't held (the pin is read once, at that moment)
  - the clear attempt is fresh, by the rule in section 3
- Then it goes to NOT_ENGAGED and sends `NOT_ENGAGED` at once.

MCB rules (MCB code; the bench sim does the same):

- Latched while any source's newest message is `ENGAGED`, or (with `deadman`) any
  watched source has been silent longer than `timeout`.
- To start a clear attempt: fix `clear_pub_seq` to the newest `pub_seq` received from
  each source (`last_pub_seq`), and send the clear attempt every period. Never update
  `clear_pub_seq` during the attempt.
- Release once that source's newest message is a `NOT_ENGAGED` with `pub_seq > clear_pub_seq`.
  If that hasn't happened within `clear_window`, drop the attempt and stay latched.
- Several engaged sources: one clear-attempt message per source, taking turns each period.
- After a silence longer than `timeout`, forget that source's `last_pub_seq`, because a node
  that rebooted starts again from 0.

```
HMI                                           MCB
press -> ENGAGED  pub_seq=40 (engaged_pub_seq=40) --> latch
         ENGAGED  pub_seq=41, 42 ------------------> latched
                       <------------------------- clear attempt: NOT_ENGAGED clear_target=hmi clear_pub_seq=42
         ENGAGED  pub_seq=43 (already in flight) --> still latched: waiting for an hmi NOT_ENGAGED > 42
accepted: 42 >= 40, switch up, fresh -> NOT_ENGAGED
         NOT_ENGAGED pub_seq=44 ----------------------> 44 > 42, fresh -> RELEASED
```

| Race | Why it can't release a stop |
|---|---|
| A `ENGAGED` already in flight arrives after the MCB's clear attempt | The MCB is still latched until it gets a `NOT_ENGAGED` newer than `clear_pub_seq`. |
| Pressed again while the clear attempt is in flight | `engaged_pub_seq` (43) > `clear_pub_seq` (42): the HMI refuses the attempt and keeps sending `ENGAGED`, and the attempt expires. |
| An old or duplicated clear attempt arrives later | Its `clear_pub_seq` is below `engaged_pub_seq`, or it fails the freshness check. |
| Another node's heartbeat `NOT_ENGAGED` | It has no `clear_target`, so it is not a clear attempt. |
| A lost clear-attempt message or a lost `NOT_ENGAGED` | Both are resent every period; the attempt only expires at `clear_window`. |
| HMI reboots and `pub_seq` restarts | Boot takes much longer than `timeout`: the deadman latches and the MCB forgets the old `last_pub_seq`. |
| A link drop or crash | No messages arrive, so the deadman latches. Nothing can be released until messages flow again. |
| The MCB crashes while latched | The HMI stays ENGAGED (it only leaves on an accepted clear attempt) and shows "E-STOP LINK LOST". |
| `pub_seq` wraps | At 10 Hz that takes 13.6 years. Comparisons use wrap-safe serial arithmetic anyway. |

## 5. HMI firmware

| Part | Design |
|---|---|
| Interrupt | Our own interrupt routine (`IRAM_ATTR`; see section 9 on the shared ISR service), on the active edge only: falling for NO, rising for NC. It doesn't read the pin level, so a press released before the task runs is still counted. It increments an atomic press counter and wakes the e-stop task. No polling. |
| Why not `espp::Button` | It reads the pin level in its own task, so a short press can come and go before it looks. |
| E-stop task | The only owner of the state, `pub_seq` and `engaged_pub_seq`. Its stack and queue are created at start-up. It wakes on a press, an incoming message, or the period deadline. It runs above the UI and RTPS receive task priorities. |
| Send | `cdr::serialize_into` from one message built at start-up into a static `std::array`. Only the plain number fields change between sends. All of the HMI's strings are 15 characters or fewer (`static_assert`). It uses the participant's raw `publish(topic, span)`. The writer is BEST_EFFORT (section 9) and doesn't wait for a matched peer. |
| Receive | A raw reader callback on the RTPS task, BEST_EFFORT. A hand-written decoder walks the received bytes and creates no `std::string`. It posts `{rx_time, pub_seq, engaged, is_clearer, clear_for_hmi, clear_pub_seq}` to a static FreeRTOS queue (`xQueueCreateStatic`) and wakes the e-stop task. |
| Outputs | One atomic snapshot: state, MCB link, and who engaged it. The UI timer reads it to show the ErrorBanner and leave the Drive and Seat screens. `rtps_comms` reads it to send DriveCommand DISABLE, zero the stick, and refuse to enable driving. |
| Logging | Nothing is logged inside the e-stop path; it counts events and keeps the last cause. The UI task logs changes from the snapshot. |
| Boot | Seq starts at 0. The pin is read once after the interrupt is set up: if it is active, the stop engages; otherwise the HMI starts NOT_ENGAGED. |
| Known delay | During a flash write (saving settings, OTA) the press is still caught, but the send waits until the flash operation finishes. |

Settings (`settings_spec.h`, so the rows are generated with no SquareLine change):

- `ESTOP_PIN`: GPIO2 / GPIO47, default GPIO2.
- `ESTOP_TYPE`: NO / NC, default NO.
- Both can only be changed while locked.
- On a change, the UI posts the new configuration to the e-stop task, which re-arms
  the interrupt and reads the pin once.

Heap: nothing on our path allocates after start-up, logging included. A heap trace
over 1,000 presses checks it. Known exceptions, below our code: lwIP allocates a
buffer on every send, and espp's receive transport builds a `std::vector` per packet.

## 6. Commits

1. **rammp-rtps `dev-estop`:** `messages/estop_message.hpp`, added to `messages.hpp`.
2. **pace-hmi-fw `dev-estop`:** the submodule bump, `main/estop.{hpp,cpp}`, the
   `rtps_comms` hooks, the settings rows, the banner text in `hmi_rtps_spec.hpp`,
   and the drive cutoff.
3. **pace-hmi-fw scripts:** `rammp_rtps.py` gets the EStop codec; `rtps_mcb_sim.py`
   gets the MCB side (latch, deadman, clear attempts, policy flags).

Pushing the rammp-rtps branch and opening its PR needs a separate OK.

## 7. Verification

- IDF 6.0.0 build; GCC, cppcheck and clang-tidy with zero findings.
- Host test: the hand-written decoder against `cdr::serialize_into`, both ways,
  including a 4-field ROS-layout message.
- Bench, against `rtps_mcb_sim.py`:
  - time from the edge to the packet on the wire
  - a 1 ms pulse is caught
  - heap trace over 1,000 presses
  - every row of the race table, forced from the sim
  - Ethernet pulled: the deadman latches
  - MCB killed: the HMI shows LINK LOST and stays engaged
- Not covered: the real MCB.

## 8. Open items

The MCB is the MIB, [rammp-org/RAMMP-mib-fw](https://github.com/rammp-org/RAMMP-mib-fw).
We don't own that repo, so its side ships as a PR proposal or pseudocode at the end.

- The MIB has to implement the rules in section 4.
- The MIB publishes its latch state every period on `rammp/estop`. The HMI's banner
  and LINK LOST detection depend on that.
- Where it fits in the MIB (commit fc70b7b):
  - `MibSystem` already runs a 20 ms state task: the deadman check and the latch
    belong there, as a new `ESTOP` state or a flag that overrides
    `DRIVE_ENABLED` / `IDLE`.
  - The 50 ms motor task then sends `RequestedState::DISARMED` (or the motor spec's
    `ESTOP`) with zero velocity.
  - Its readers are BEST_EFFORT, like the HMI's e-stop writer.
- Open question for the MIB: what starts a clear attempt? The MIB has no operator input
  today. Options: a button on the MIB, an automatic clear attempt once every source is
  `NOT_ENGAGED` and fresh, or a bench command. Until that is decided, `rtps_mcb_sim.py`
  provides a "clear attempt" action.

## 9. As built: where it differs from the plan

| Plan | Built | Why |
|---|---|---|
| RELIABLE writer | BEST_EFFORT | ROS 2 compatibility was dropped, every RAMMP topic and the MIB's readers are best-effort, and the 100 ms resend is the reliability. |
| Interrupt in IRAM, `ESP_INTR_FLAG_IRAM` | The handler is `IRAM_ATTR`, but the shared GPIO ISR service is installed without the IRAM flag (espp's `Interrupt`, the W5500) | With the flag, every GPIO handler on the board would have to live in IRAM. During a flash write the edge is held in the GPIO status register and handled right after: delayed, never lost. |
| Clearer `"mcb"` | `"mib"` (`rammp::kEStopClearer`) | The MCB is the MIB. |
| HMI watches the MIB and refuses driving without its e-stop messages | Built, but off: `rammp::kEStopNeedsMib = false` | The MIB doesn't publish `rammp/estop` yet. With the check on, nothing could drive. |
| Drive DISABLE "at once" on engage | Sent from the stick task, at the next stick period after the press | That publish goes through espp's per-publish `std::string` for a long topic name, so it stays out of the e-stop task. The stick goes out centred from the same period. |
| (not planned) | A press inside the 2 ms settle after re-arming isn't edge-counted | The level is read once after arming, so a switch still held is caught. |
| (not planned) | Arming is retried every period until it works; until then `estop_active()` is true and the banner says "E-STOP INPUT NOT ARMED" | Fail-safe, rather than giving up after one failed attempt. |

Verification so far:

- IDF 6.0.0 build is clean: no new warnings.
- `test/estop_codec_test.cpp` (ASan/UBSan, 200k fuzzed inputs) and
  `test/estop_latch_test.py` (18 protocol cases, including every race in section 4)
  pass. Mutation checks show both catch the bugs they target.
- The Python encoder is byte-identical to espp/cdr.
- Not run yet: cppcheck (not installed here).

## 10. Bench results (2026-10-19, board 2, `e52381d`)

Setup:
- Tab5 board 2, Ethernet on the lab LAN (10.111.20.0/22).
- The switch: bare wires, then a jumper, from GPIO2 to GND, set as NO.
- The MIB: `rtps_mcb_sim.py`'s latch, driven by a bench script on the PC. No real MIB or
  chair was on the network.
- Logs, screenshots and the full flash backup are in `~/bench/estop/` on the bench laptop.
- The on-target harness is on `dev_ai_estop_bench` and is never merged.

| Check | How | Result |
|---|---|---|
| Press is caught | Real presses | Every touch. A jumper gives 1 edge; bare wires bounce (11–171 edges), which is harmless because each one re-engages. |
| Banner | Remote-UI screenshot | "E-STOP ENGAGED / HMI E-STOP SWITCH PRESSED / Only the MIB can clear it" on Locked and Drive. |
| Drive cutoff | Bench MIB in ENABLED, then a press | The HMI's DriveCommand DISABLE reached the MIB 4–31 ms after the latch. The screen goes Drive → Locked. |
| Keeps sending after release | Harness | ~10 `ENGAGED` per second after a 1 µs press, until it is cleared. |
| Clear attempt while held | Jumper held 22 s | Refused 20 times. Both clear attempts expired; the stop held. |
| Clear attempt after release | Real presses | Accepted; the MIB released the stop 120–200 ms after starting the attempt. |
| Old `clear_pub_seq` / foreign clear attempt / plain false / ROS 4-field / garbage | Harness injection | Refused, ignored, ignored, never a clear attempt, `rx_err` +1. |
| MIB latch shown, then stale | Harness injection | Remote banner up; stale after exactly the 300 ms timeout (receive time). |
| Re-arm GPIO47; NC with nothing wired | Harness | Moves pins with no spurious press. NC with nothing wired reads as pressed (fail-safe). |
| MIB deadman | Ethernet unplugged for ~3 s | MIB latched "hmi silent" 0.55 s *before* the W5500 even reported link down. It released after the replug, once the HMI was heard again. |
| No heap in the e-stop task | Heap hook, RTPS down, 5 real presses | **0 allocations**: 77 edges, 5 accepted clear attempts, 565 sends. With RTPS up, espp's publish path allocates about 5 times per send (the known exception). |
| Endpoint pools | Boot log | All 11 RTPS endpoints added (7 writers, 6 readers). |

Found while testing:

- **Fixed (`e52381d`):** `estop_start` installed the GPIO ISR service again and logged an
  E-level line on every boot.
- **Bench-only, not chased:** the harness's open-drain pulses register on the first pulse
  and then read back HIGH on both pins. Real presses don't do this (the level log read
  GPIO2 = 0 for all 22 s the jumper was in), so it is a harness artifact. Still, don't
  trust that harness's GPIO results without a real press.
- **UI wording:** the Locked screen still says "Hold the joystick button to drive" under
  the e-stop banner. The hold is refused, but the prompt shouldn't offer it.
- **Lab network:** the LAN proxy-ARPs for hosts, so an old neighbour entry can point at the
  gateway. The board also gets a new DHCP address on every reconnect. The bench script
  re-targets the MIB on each new IP.

## 11. Moved to estop-core

The e-stop now lives in [estop-core](https://github.com/Alaining/estop-core), one ESP-IDF
component that every RAMMP firmware uses: the HMI runs `estop::Node`, the MIB
`estop::Latch`. pace-hmi-fw takes it through the component manager at tag `v0.1.0`, and
overrides its `rammp_rtps_messages` with the `external/rammp-rtps` submodule
(`override_path` in `main/idf_component.yml`), so the submodule stays the one source of
the spec. The rules, codec and task are the ones described above, ported unchanged:

| Was (this repo) | Now (estop-core) |
|---|---|
| `main/estop.cpp` rules | `estop::core::NodeLogic` (pure; no I/O, no clock) |
| `main/estop.cpp` task, ISR, queues | `estop::Node` (`src/esp/node.cpp`, `edge_input.cpp`) |
| `main/estop_codec.hpp` | `estop/core/codec.hpp` |
| `rtps_comms` EStop writer and reader | `estop::Node::attach(participant)` |
| `test/estop_codec_test.cpp` | `test/test_codec.cpp`, plus `test_node`, `test_latch` and `test_race` (the C++ race suite: the real node against the real latch through the codec) |

What stays here: `main/hmi_estop.hpp` (the node, `"hmi"`, and the GPIO2/GPIO47 setting
map), the banners and the drive gates in `main.cpp` and `rtps_comms`, and the Python bench
tools (`scripts/estop_latch.py` for the MIB simulator, `test/estop_latch_test.py`).

One behaviour change: before the task has run once, the banner says "E-STOP INPUT NOT
ARMED" (it said "E-STOP ENGAGED"). Driving is refused either way.
