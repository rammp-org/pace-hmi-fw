# alloc_guard: the on-chip allocation guard (TS-DET-08), bench builds only. README.md has the
# concern and requirements. Without CONFIG_HMI_BENCH_ALLOC_GUARD no source is compiled: the
# component is its headers, and callers reach src/alloc_guard.cpp only inside
# `if constexpr (hmi::alloc_guard::ENABLED)`, so a release image has none of it.
if(CONFIG_HMI_BENCH_ALLOC_GUARD)
  set(alloc_guard_srcs "src/alloc_guard.cpp")
  set(alloc_guard_priv_requires esp_hw_support esp_timer freertos heap)
else()
  set(alloc_guard_srcs "")
  set(alloc_guard_priv_requires "")
endif()

idf_component_register(SRCS ${alloc_guard_srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ${alloc_guard_priv_requires})

if(alloc_guard_srcs)
  fw_component_options(${COMPONENT_LIB})
endif()
