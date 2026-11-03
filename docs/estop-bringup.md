# E-stop bring-up

How to test the robot-wide e-stop on the HMI and the MIB. The design and the protocol are in
[plans/estop.md](plans/estop.md). Everything is merged:

- **rammp-rtps** `main` (rammp-rtps#21): `rammp::EStop` on `rt/estop`, ROS 2 facing.
- **MIB:** RAMMP-mib-fw `main` (RAMMP-mib-fw#64) has estop-core `v0.3.0` and publishes `rt/estop`.
- **HMI:** pace-hmi-fw `main` (pace-hmi-fw#26), espp 1.4.0.

Both firmwares must be at that rammp-rtps commit or newer, or they do not hear each other
(the MIB's deadman then latches `hmi: silent`).

## 1. Flash the HMI (Tab5, ESP-IDF 6.0)

```sh
git clone --recurse-submodules https://github.com/rammp-org/pace-hmi-fw.git
cd pace-hmi-fw
idf.py -p <PORT> flash monitor
```

E-stop switch: GPIO2, normally open to GND. You can change the pin and the switch type in
**Settings > E-stop**.

## 2. Flash the MIB

RAMMP-mib-fw `main`: `idf.py -p <PORT> flash monitor`. The latch is `estop::Latch` in
`MibSystem`, watching `hmi`; the clear button is `mib::config::estop_clear_gpio`, normally
open to GND, held 2 s. How a firmware takes estop-core in is in its README and
[`examples/latch/main/main.cpp`](https://github.com/rammp-org/rammp-estop-core/blob/main/examples/latch/main/main.cpp).

## 3. Test

- [ ] **Boot:** the MIB log says `LATCHED: boot`. Hold the clear button for 2 s: the log says
  `RELEASED`.
- [ ] **Press the HMI e-stop:** the MIB logs `LATCHED: hmi: switch gpio2`. Hold the clear button
  for 2 s to release.
- [ ] **Clear while held:** with the HMI switch still pressed, a clear is refused.
- [ ] **Deadman:** unplug the HMI's Ethernet. The MIB logs `LATCHED: hmi: silent (deadman)`.
- [ ] **No false deadman:** clear the stop, then open Bench (PIN 1234) on the HMI. The MIB stays
  `RELEASED`, and the HMI logs no `History overflow on topic 'rt/estop'`.

## 4. See it from ROS 2 (or a laptop)

The e-stop is ROS 2 facing since rammp-rtps#21: the HMI and the MIB publish `rt/estop` with
type `rammp_common_interfaces::msg::dds_::EStop_`, which is what a ROS 2 subscriber to `/estop`
of `rammp_common_interfaces/msg/EStop` matches on. No bridge: the Jetson reads the joystick's
own packets. Both firmwares must be at that rammp-rtps commit or newer, or the MIB and the HMI
no longer hear each other (the deadman then latches `hmi: silent`).

On a machine with ROS 2 Humble and Cyclone DDS (the `rammp-base` image), build
`rammp-interfaces-ros2` at the commit that mirrors `rammp::EStop` (rammp-interfaces-ros2#30) and:

```sh
ros2 topic echo --qos-reliability best_effort /estop
```

- [ ] With the chair idle: a message every 100 ms from `source: hmi` and from `source: mib`,
  `state: 0`, `pub_seq` counting up.
- [ ] **Press the HMI e-stop:** `state: 1`, `source: hmi`, `reason: switch gpio2`, at 10 Hz
  until the MIB clears.
- [ ] **Hold the MIB clear button 2 s:** one burst of `source: mib`, `clear_target: hmi`,
  `clear_pub_seq: <the hmi's latest pub_seq>`, then the HMI goes back to `state: 0`.

Without ROS 2, a plain Cyclone DDS subscriber sees the same thing; the MIB bench simulator
in `scripts/rtps_mcb_sim.py` already speaks these names.

## Notes

- Needs ESP-IDF ≥ 5.5 and espp ≥ 1.4.0. On 1.3.10 a Cyclone DDS peer never receives a
  sample (esp-cpp/espp#862, in 1.3.11), and 1.4.0 adds the locator and lease fixes
  (esp-cpp/espp#866, #867, #871) the bench needs with a laptop on several interfaces.
- Bench-tested 2026-11-03 (Tab5 and MIB on the chair LAN, a Cyclone DDS listener on a laptop).
  The chair test with the Jetson is pending; see pace-hmi-fw#27 for the RTPS proxy limits it
  may need.
