# E-stop plan (branch `dev-estop`)

Status: implemented on `dev-estop` (firmware, shared message, bench sim); not yet bench-tested on
the Tab5. Section 9 lists where the build differs from this plan.

Scope: an e-stop switch on the HMI (GPIO2 or GPIO47, NO or NC, chosen in Settings)
that engages a robot-wide stop over RTPS. Only the MCB can clear it. The MCB also
runs a deadman: if a node goes silent, that engages the stop too.

## 1. Message

The ROS EStop format (`std_msgs/Header`, `engaged`, `source`, `reason`) with three
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
  bool engaged;           // see the table below
  std::string source;     // who published it ("hmi", "mib", ...)
  std::string reason;     // free text, for the log
  // appended to the ROS layout
  uint32_t seq;           // +1 per message from this source: loss detection and the clear handshake
  std::string ack_source; // clears only: the source whose engagement this clears; "" otherwise
  uint32_t ack_seq;       // clears only: newest seq the clearer had seen from ack_source
};
inline constexpr Topic<EStop> kEStop{RAMMP_TOPIC_ESTOP, RAMMP_TYPE_ESTOP};
inline constexpr char kEStopClearer[] = "mib"; // the only source whose clears count
```

| engaged | ack_source | Meaning | Who sends it |
|---|---|---|---|
| true | (any) | Engage. Always acted on, with no age or seq check. | Any node |
| false | `""` | Heartbeat: "alive, not engaging". It clears nothing. | Any node, every period |
| false | `"<src>"` | Clears `<src>`'s engagement | `kEStopClearer` only |

This differs from the ROS comment, where `false` means "clear it". Here a plain `false`
is only a heartbeat, so one node can't release another node's stop by sending its
normal heartbeat. A node that only knows the 4-field layout decodes `seq = 0` and
`ack_source = ""`, so its messages read as heartbeats or engages, never as clears.

The topic name is 11 characters. espp builds a `std::string(topic)` on every publish,
and a name of 15 characters or fewer stays in libstdc++'s short-string buffer, which
uses no heap.

## 2. Timing policy (configurable in code)

```cpp
struct EStopPolicy {
  milliseconds period{100};        // every node sends at least this often
  milliseconds timeout{300};       // receiver: a source silent this long counts as engaged (if deadman)
  bool deadman{true};              // false: only engaged=true stops; silence is just logged
  milliseconds clear_window{1000}; // MCB: a clear not taken up within this is dropped; still latched
};
inline constexpr EStopPolicy kEStopPolicy{};
```

It lives in the shared spec, so the MCB, the HMI and the bench sim read the same
values. The sim takes flags to override them (`--estop-timeout`, `--no-deadman`).

## 3. Staleness: receive time on the steady clock, never the stamp

- **Engaging is never age-checked.** Any `true` engages, however late it arrives.
  RTPS readers already drop samples older than the last one delivered from the same
  writer, so a late `true` can't follow a newer `false` from that writer. If one
  somehow did, it would cause an extra stop, which is safe.
- **Clearing is age-checked, by receive time:**
  - MCB: a release needs the source's `false` with `seq > ack_seq`, received within
    `timeout` of the moment it releases.
  - HMI: a clear counts only if the e-stop task takes it from the queue within one
    `period` of receiving it. A task held up by a flash write can't act on an old clear.
- **Deadman:** the MCB records when it last heard from each source. Silence longer
  than `timeout` latches the stop.
- **The HMI watches the MCB:** no MCB message for longer than `timeout` shows
  "E-STOP LINK LOST" and refuses driving.

## 4. Clear handshake

Roles: each node publishes its own demand. The MCB owns the latch and publishes it
every period: `engaged=true` with the cause in `reason` while latched, a clear
(`false` plus ack fields) while clearing, and a plain `false` after releasing.

HMI states:

- **NORMAL:** sends `false` every period.
- **ENGAGED:** sends `true` every period, including after the switch is released.

HMI rules:

- A press edge enters ENGAGED and sends at once, without waiting for the tick.
- `press_seq` is the seq of the first `true` sent after the latest press. Pressing
  again while engaged sets a new `press_seq`.
- The HMI accepts a clear only if all of these hold:
  - `source == kEStopClearer`
  - `ack_source == "hmi"`
  - `ack_seq >= press_seq`
  - the switch isn't held (the pin is read once, at that moment)
  - the clear is fresh, by the rule in section 3
- Then it goes to NORMAL and sends `false` at once.

MCB rules (MCB code; the bench sim does the same):

- Latched while any source's newest message is `true`, or (with `deadman`) any
  watched source has been silent longer than `timeout`.
- To clear a source: fix `ack_seq` to the newest seq seen from it, and send the clear
  every period. Never update `ack_seq` during a clear attempt.
- Release once that source's newest message is a `false` with `seq > ack_seq`.
  If that hasn't happened within `clear_window`, drop the attempt and stay latched.
- Several engaged sources: one clear per source, taking turns each period.
- After a silence longer than `timeout`, forget that source's seq, because a node
  that rebooted starts again from 0.

```
HMI                                           MCB
press -> true  seq=40  (press_seq=40) ------> latch
         true  seq=41, 42 ------------------> latched
                       <--------------------- false ack_source=hmi ack_seq=42  (MCB decides)
         true  seq=43 (already in flight) --> still latched: waiting for an hmi false > 42
clear: 42 >= 40, switch up, fresh -> NORMAL
         false seq=44 ----------------------> 44 > 42, fresh -> RELEASE
```

| Race | Why it can't release a stop |
|---|---|
| A `true` already in flight arrives after the MCB's clear | The MCB is still latched until it gets a `false` newer than `ack_seq`. |
| Pressed again while the clear is in flight | `press_seq` (43) > `ack_seq` (42): the HMI rejects the clear and keeps sending `true`, and the MCB's attempt times out. |
| An old or duplicated clear arrives later | Its `ack_seq` is below `press_seq`, or it fails the freshness check. |
| Another node's heartbeat `false` | It has no ack fields, so it is not a clear. |
| A lost clear or a lost `false` | Both are resent every period; the attempt only ends at `clear_window`. |
| HMI reboots and seq restarts | Boot takes much longer than `timeout`: the deadman latches and the MCB forgets the old seq. |
| A link drop or crash | No messages arrive, so the deadman latches. Nothing can be released until messages flow again. |
| The MCB crashes while latched | The HMI stays ENGAGED (it only leaves on an accepted clear) and shows "E-STOP LINK LOST". |
| `seq` wraps | At 10 Hz that takes 13.6 years. Comparisons use wrap-safe serial arithmetic anyway. |

## 5. HMI firmware

| Part | Design |
|---|---|
| Interrupt | Our own interrupt routine (`IRAM_ATTR`; see section 9 on the shared ISR service), on the active edge only: falling for NO, rising for NC. It doesn't read the pin level, so a press released before the task runs is still counted. It increments an atomic press counter and wakes the e-stop task. No polling. |
| Why not `espp::Button` | It reads the pin level in its own task, so a short press can come and go before it looks. |
| E-stop task | The only owner of the state, `seq` and `press_seq`. Its stack and queue are created at start-up. It wakes on a press, an incoming message, or the period deadline. It runs above the UI and RTPS receive task priorities. |
| Send | `cdr::serialize_into` from one message built at start-up into a static `std::array`. Only the plain number fields change between sends. All of the HMI's strings are 15 characters or fewer (`static_assert`). It uses the participant's raw `publish(topic, span)`. The writer is BEST_EFFORT (section 9) and doesn't wait for a matched peer. |
| Receive | A raw reader callback on the RTPS task, BEST_EFFORT. A hand-written decoder walks the received bytes and creates no `std::string`. It posts `{rx_time, seq, engaged, is_clearer, ack_for_hmi, ack_seq}` to a static FreeRTOS queue (`xQueueCreateStatic`) and wakes the e-stop task. |
| Outputs | One atomic snapshot: state, MCB link, and who engaged it. The UI timer reads it to show the ErrorBanner and leave the Drive and Seat screens. `rtps_comms` reads it to send DriveCommand DISABLE, zero the stick, and refuse to enable driving. |
| Logging | Nothing is logged inside the e-stop path; it counts events and keeps the last cause. The UI task logs changes from the snapshot. |
| Boot | Seq starts at 0. The pin is read once after the interrupt is set up: if it is active, the stop engages; otherwise the HMI starts NORMAL. |
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
   gets the MCB side (latch, deadman, clear with acks, policy flags).

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
- Open question for the MIB: what decides to clear? The MIB has no operator input
  today. Options: a button on the MIB, an automatic clear once every source is
  `false` and fresh, or a bench command. Until that is decided, `rtps_mcb_sim.py`
  provides a "clear" action.

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
- Not run yet: cppcheck (not installed here), and the bench checks in section 7.
