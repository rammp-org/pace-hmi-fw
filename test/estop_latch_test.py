#!/usr/bin/env python3
"""The e-stop protocol, end to end without a network: scripts/estop_latch.py (the MIB's
side) against a model of the HMI's rules in main/estop.cpp, through every race in
docs/plans/estop.md section 4. Messages go through the real codec (pack/unpack), and
the test controls delivery order, so in-flight and reordered messages can be forced.

    python3 test/estop_latch_test.py
"""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))

import rammp_rtps as spec  # noqa: E402
from estop_latch import EStopLatch  # noqa: E402

P = spec.ESTOP_PERIOD_MS / 1000


class HmiModel:
    """main/estop.cpp's rules: send every period; a press engages and sets a new
    engaged_pub_seq at its first send; a clear attempt is accepted only if it is fresh,
    from the clearer, addressed to us, covers engaged_pub_seq, and the switch is up."""

    def __init__(self, name: str = spec.ESTOP_SELF, pub_seq: int = 0) -> None:
        self.name = name
        self.pub_seq = pub_seq
        self.engaged = False
        self.engaged_pub_seq = 0
        self.press_unsent = False
        self.held = False

    def press(self) -> None:
        self.engaged = True
        self.press_unsent = True

    def send(self) -> bytes:
        self.pub_seq = (self.pub_seq + 1) & 0xFFFFFFFF
        if self.engaged and self.press_unsent:
            self.engaged_pub_seq = self.pub_seq
            self.press_unsent = False
        return spec.pack_estop(self.engaged, self.name, "switch gpio2" if self.engaged else "",
                               self.pub_seq)

    def receive(self, payload: bytes, age: float = 0.0) -> bool:
        """True if this took a clear."""
        m = spec.unpack_estop(payload)
        if (m is None or m.source != spec.ESTOP_CLEARER or not m.is_clear_attempt
                or m.clear_target != self.name or not self.engaged):
            return False
        covers = not self.press_unsent and not spec.estop_seq_newer(self.engaged_pub_seq, m.clear_pub_seq)
        if age > P or not covers or self.held:
            return False
        self.engaged = False
        return True


def mib_send(latch: EStopLatch, now: float) -> bytes:
    (msg,) = latch.outgoing(now)
    return spec.pack_estop(*msg)


def deliver(latch: EStopLatch, payload: bytes, now: float) -> None:
    latch.on_message(spec.unpack_estop(payload), now)


class Bench:
    """One HMI and the MIB, stepping a period at a time with everything delivered."""

    def __init__(self, deadman: bool = True) -> None:
        self.now = 0.0
        self.latch = EStopLatch(deadman=deadman)
        self.hmi = HmiModel()

    def step(self, hmi_to_mib: bool = True, mib_to_hmi: bool = True) -> None:
        self.now += P
        out = self.hmi.send()
        if hmi_to_mib:
            deliver(self.latch, out, self.now)
        self.latch.tick(self.now)
        back = mib_send(self.latch, self.now)
        if mib_to_hmi:
            self.hmi.receive(back)

    def run(self, periods: int, **kw) -> None:
        for _ in range(periods):
            self.step(**kw)

    def clear(self) -> None:
        assert self.latch.start_clear_attempt(self.now)

    def boot(self) -> None:
        """Heard from, cleared, released: the starting point of most tests."""
        self.run(1)
        self.clear()
        self.run(3)
        assert not self.latch.latched, self.latch.events


class EStopProtocolTest(unittest.TestCase):

    def test_boot_is_latched_until_cleared(self):
        b = Bench()
        self.assertFalse(b.latch.start_clear_attempt(b.now))  # never heard from the HMI
        b.run(5)
        self.assertTrue(b.latch.latched)  # nothing releases by itself
        b.clear()
        b.run(3)
        self.assertFalse(b.latch.latched)

    def test_press_latches_and_holds_after_release_of_the_switch(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(1)
        self.assertTrue(b.latch.latched)
        b.run(50)  # a momentary switch: up long ago, the HMI keeps sending true
        self.assertTrue(b.hmi.engaged and b.latch.latched)

    def test_clear_releases_through_the_handshake(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(3)
        b.clear()
        b.run(3)
        self.assertFalse(b.hmi.engaged)
        self.assertFalse(b.latch.latched)
        self.assertIn("RELEASED", b.latch.events)

    def test_true_in_flight_after_the_clear_does_not_release_or_confuse(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        b.clear()
        clear = mib_send(b.latch, b.now)          # the clear, held back
        late_true = b.hmi.send()                  # already in flight, newer than ack
        deliver(b.latch, late_true, b.now + 0.01)
        b.latch.tick(b.now + 0.01)
        self.assertTrue(b.latch.latched)          # still waiting for a false > ack
        self.assertTrue(b.hmi.receive(clear))     # the HMI takes the clear
        deliver(b.latch, b.hmi.send(), b.now + 0.02)
        b.latch.tick(b.now + 0.02)
        self.assertFalse(b.latch.latched)
        # The late true arriving again (a retransmit or duplicate) after release:
        # engaging is never seq-checked, so it latches. Safe, if a nuisance.
        deliver(b.latch, late_true, b.now + 0.03)
        self.assertTrue(b.latch.latched)

    def test_press_again_while_the_clear_is_in_flight_is_refused(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        b.clear()
        clear = mib_send(b.latch, b.now)
        b.hmi.press()                       # pressed again before the clear arrives
        deliver(b.latch, b.hmi.send(), b.now)
        self.assertFalse(b.hmi.receive(clear))
        # The MIB keeps resending its attempt (old clear_pub_seq); the HMI keeps refusing; it expires.
        b.run(int(b.latch.clear_window / P) + 3)
        self.assertTrue(b.hmi.engaged and b.latch.latched)
        self.assertIn("clear attempt expired: not completed within the clear window", b.latch.events)
        b.clear()                           # a fresh decision covers the new press
        b.run(3)
        self.assertFalse(b.latch.latched)

    def test_old_clear_arriving_later_is_refused(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        b.clear()
        old_clear = mib_send(b.latch, b.now)
        b.run(3)                            # cleared and released normally
        self.assertFalse(b.latch.latched)
        b.hmi.press()
        b.run(2)
        self.assertFalse(b.hmi.receive(old_clear))   # clear_pub_seq below the new engaged_pub_seq
        self.assertFalse(b.hmi.receive(old_clear, age=0.0))
        self.assertTrue(b.hmi.engaged)

    def test_stale_clear_is_refused_by_receive_age(self):
        hmi = HmiModel()
        hmi.press()
        hmi.send()
        clear = spec.pack_estop(False, spec.ESTOP_CLEARER, "clear", 9, spec.ESTOP_SELF, hmi.pub_seq)
        self.assertFalse(hmi.receive(clear, age=2 * P))
        self.assertTrue(hmi.receive(clear, age=0.0))

    def test_held_switch_refuses_the_clear(self):
        hmi = HmiModel()
        hmi.press()
        hmi.send()
        hmi.held = True
        clear = spec.pack_estop(False, spec.ESTOP_CLEARER, "clear", 9, spec.ESTOP_SELF, hmi.pub_seq)
        self.assertFalse(hmi.receive(clear))

    def test_another_nodes_heartbeat_is_not_a_clear(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        other = HmiModel(name="pendant")
        for _ in range(5):
            deliver(b.latch, other.send(), b.now)
            self.assertFalse(b.hmi.receive(other.send()))  # not from the clearer
        b.latch.tick(b.now)
        self.assertTrue(b.latch.latched and b.hmi.engaged)

    def test_clear_from_a_non_clearer_is_ignored(self):
        hmi = HmiModel()
        hmi.press()
        hmi.send()
        rogue = spec.pack_estop(False, "rogue", "clear", 1, spec.ESTOP_SELF, hmi.pub_seq)
        self.assertFalse(hmi.receive(rogue))

    def test_lost_clears_and_falses_are_resent(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        b.clear()
        b.step(mib_to_hmi=False)   # clear lost
        b.step(hmi_to_mib=False)   # clear arrives; the HMI's false is lost
        self.assertTrue(b.latch.latched)
        b.run(2)                   # resent false gets through
        self.assertFalse(b.latch.latched)

    def test_link_drop_trips_the_deadman_and_blocks_release(self):
        b = Bench()
        b.boot()
        b.run(int(b.latch.timeout / P) + 2, hmi_to_mib=False)
        self.assertTrue(b.latch.latched)
        self.assertIn("silent", b.latch.cause)
        b.latch.start_clear_attempt(b.now)  # may start...
        b.run(int(b.latch.clear_window / P) + 2, hmi_to_mib=False)
        self.assertTrue(b.latch.latched)  # ...but cannot finish while silent

    def test_hmi_reboot_restarts_seq_and_still_clears(self):
        b = Bench()
        b.hmi.pub_seq = 5000
        b.boot()
        b.now += 3.0                 # rebooting: silence far beyond the timeout
        b.latch.tick(b.now)
        self.assertTrue(b.latch.latched)
        b.hmi = HmiModel()           # seq from 0 again
        b.run(3)
        self.assertEqual(b.latch.sources[spec.ESTOP_SELF].last_pub_seq, b.hmi.pub_seq)  # forgot 5000
        b.clear()
        b.run(3)
        self.assertFalse(b.latch.latched)

    def test_deadman_off_ignores_silence(self):
        b = Bench(deadman=False)
        b.boot()
        b.run(20, hmi_to_mib=False)
        self.assertFalse(b.latch.latched)
        b.hmi.press()
        b.run(1)
        self.assertTrue(b.latch.latched)

    def test_ros_layout_engages_but_never_clears(self):
        b = Bench()
        b.boot()
        ros_true = spec.encode(spec.MSG_ESTOP_ROS, 0, 0, "", 1, "ros_node", "panic")
        deliver(b.latch, ros_true, b.now)
        self.assertTrue(b.latch.latched)
        self.assertEqual(spec.unpack_estop(ros_true).pub_seq, 0)
        ros_false = spec.encode(spec.MSG_ESTOP_ROS, 0, 0, "", 0, spec.ESTOP_CLEARER, "x")
        self.assertFalse(spec.unpack_estop(ros_false).is_clear_attempt)

    def test_release_needs_a_false_sent_after_the_decision(self):
        # The HMI was never engaged (the MIB latched on its own), so its newest word is
        # already engaged=false -- but at clear_pub_seq, from before the attempt. That must
        # not release: only an engaged=false newer than clear_pub_seq proves the HMI answered.
        b = Bench()
        b.boot()
        b.latch.engage("bench")
        b.clear()
        b.latch.tick(b.now)
        self.assertTrue(b.latch.latched)
        b.run(1)
        self.assertFalse(b.latch.latched)

    def test_reordered_older_message_does_not_overwrite_the_newest(self):
        b = Bench()
        b.boot()
        b.hmi.press()
        b.run(2)
        b.clear()
        clear = mib_send(b.latch, b.now)
        old_true = b.hmi.send()                 # sent before the clear arrived...
        self.assertTrue(b.hmi.receive(clear))
        new_false = b.hmi.send()
        deliver(b.latch, new_false, b.now)      # ...but delivered after the false
        deliver(b.latch, old_true, b.now)
        self.assertFalse(b.latch.sources[spec.ESTOP_SELF].engaged)  # newest word stands
        b.latch.tick(b.now)
        self.assertFalse(b.latch.latched)

    def test_seq_wraps(self):
        b = Bench()
        b.hmi.pub_seq = 0xFFFFFFFF - 3
        b.boot()
        b.hmi.press()
        b.run(4)                     # crosses the wrap while engaged
        b.clear()
        b.run(3)
        self.assertFalse(b.latch.latched)
        self.assertTrue(spec.estop_seq_newer(1, 0xFFFFFFFF))


if __name__ == "__main__":
    unittest.main(verbosity=1)
