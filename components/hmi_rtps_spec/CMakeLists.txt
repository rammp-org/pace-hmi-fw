# hmi_rtps_spec: what only this HMI adds to the shared RTPS spec (external/rammp-rtps): names and
# helpers, timing, display limits, the bench self-test topics and the warning texts it raises
# itself (CS-LAY-02). README.md says what is in it.
#
# Header-only (constexpr data and helpers), so an INTERFACE library: it takes no PRIVATE options,
# and fw_component_options() does not apply. Its includers build it with their own flags (main,
# hmi_ui, and the host app tests/host/rtps_spec with the full fw-standards warning set).
idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES rammp_rtps_messages)
