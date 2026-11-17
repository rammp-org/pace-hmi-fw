#pragma once
// RTPS stress test, CONFIG_HMI_RTPS_STRESS only (scripts/rtps_stress.py drives it).
//
// One extra reader, rt/rammp/stress/to_hmi (std_msgs/UInt8MultiArray, so a stock ROS 2
// node can publish it), counts what each remote participant delivers. Once a second the
// console gets a STRESS line: heap, how many remote endpoints discovery matched on the
// topics that matter, and the delivery counters. It goes over USB, so watching costs the
// network nothing.

namespace espp {
class RtpsParticipant;
}

/// Adds the stress reader and starts the stats task. False = no room in the pools
/// (the stress overlay raises them by one).
bool rtps_stress_attach(espp::RtpsParticipant &participant);
