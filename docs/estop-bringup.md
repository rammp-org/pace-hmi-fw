# E-stop bring-up

How to test the robot-wide e-stop on the HMI and the MIB before the PRs merge. The design
and the protocol are in [plans/estop.md](plans/estop.md).

- **HMI:** `pace-hmi-fw`, branch `dev-estop`
- **MIB:** add `estop-core` `v0.2.0`

Both use rammp-rtps branch `feat/estop`. `dev-estop` and `feat/estop` aren't merged into
`main` yet: we test this combination on the HMI and the MIB first, and merge the PRs once it
works.
The HMI and estop-core already point at `feat/estop`, so there is nothing to set up for it.

## 1. Flash the HMI (Tab5, ESP-IDF 6.0.0)

```sh
git clone --recurse-submodules -b dev-estop https://github.com/rammp-org/pace-hmi-fw.git
cd pace-hmi-fw
idf.py -p <PORT> flash monitor
```

E-stop switch: GPIO2, normally open to GND. You can change the pin and the switch type in
**Settings > E-stop**.

## 2. MIB: add estop-core

**a)** In `main/idf_component.yml`, under `dependencies:`:

```yaml
  estop_core:
    git: https://github.com/Alaining/estop-core.git
    version: v0.2.0
```

**b)** In code:

```cpp
#include "estop/estop.hpp"

// Watches "hmi". Clear button: normally open to GND, held 2 s.
// Replace GPIO_NUM_35 with your clear-button pin.
static estop::Latch latch({.clear_input = {.gpio_num = GPIO_NUM_35}});

// In app_main:
latch.start();              // first, before the network and the motors
latch.attach(participant);  // after your espp::RtpsParticipant has started

// In the motor loop:
if (latch.active()) { /* stop the motors */ }
```

There's a full commented example in
[`examples/latch/main/main.cpp`](https://github.com/Alaining/estop-core/blob/main/examples/latch/main/main.cpp).

## 3. Test

- [ ] **Boot:** the MIB log says `LATCHED: boot`. Hold the clear button for 2 s: the log says
  `RELEASED`.
- [ ] **Press the HMI e-stop:** the MIB logs `LATCHED: hmi: switch gpio2`. Hold the clear button
  for 2 s to release.
- [ ] **Clear while held:** with the HMI switch still pressed, a clear is refused.
- [ ] **Deadman:** unplug the HMI's Ethernet. The MIB logs `LATCHED: hmi: silent (deadman)`.

## Notes

- Needs ESP-IDF ≥ 5.5 and espp ≥ 1.3.2.
- Test on a MIB branch whose rammp-rtps submodule is at `c7db102`, the same as MIB main. If yours
  has a newer rammp-rtps, ask before building.
- This is the first run on real MIB hardware. If anything looks off, please send the log.
