# RTPS stress test

How many participants, and how much traffic from each, the HMI's RTPS handles before
peers stop matching or streams stop arriving.

| File | What |
|---|---|
| `network.toml` | The network: MIB, PACE RACER and Jetson stand-ins (Guo's orchestrator mocks), then filler ROS 2 nodes |
| `sdkconfig.stress` | Today's limits plus the HMI's stress reader (`CONFIG_HMI_RTPS_STRESS`) |
| `sdkconfig.tuned` | The MIB's discovery limits on top: the "after" run |
| `../scripts/rtps_stress.py` | Runs the stand-ins, reads the HMI's `STRESS` lines on USB serial, writes the table |

## Run

```console
# build (delete sdkconfig first); add sdkconfig.tuned for the "after" run
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;stress/sdkconfig.stress;<wifi overlay>" build flash

python scripts/rtps_stress.py sweep --peer <HMI IP> --advertised-address <PC IP> \
    --serial COM9 --label before --rtt
```

Results land in `stress/results/<time>-<label>/`: `summary.md` (the table), `steps.csv`
and the HMI's `console.log`.

## What it measures

Each stand-in is a real RTPS participant with its role's discovery footprint, and also:

- publishes `rt/rammp/stress/to_hmi` (`std_msgs/UInt8MultiArray`) at the rate under test.
  The HMI counts each sender's samples and sequence gaps.
- subscribes to the joystick (`joystick = true`) and counts what arrives.

Data between stand-ins never leaves the PC, and on a switch the HMI never sees traffic for
topics it doesn't use. So the bandwidth axis is only traffic the HMI must handle.

A step passes when all of these hold:

- Every stand-in's stress stream is received at 99% or better.
- Every joystick subscriber gets at least 95% of what the MIB stand-in gets.
- MibStatus loss is 5% or less (it runs at 2 Hz).
- The HMI did not reset.

For each participant count the rate goes up until a step fails.

**Over WiFi** all traffic shares the air, so the bandwidth numbers only hold for WiFi. The
participant and matching limits do carry over to Ethernet.
