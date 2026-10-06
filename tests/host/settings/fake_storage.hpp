#pragma once
// Fake of main/storage.hpp for the settings host app (TS-UNIT-03): storage_path() points into a
// temporary folder made on first use, and storage_write() records each call, writes the file
// there (so a later settings_load() reads it back) and can be scripted to fail.

#include <string>

namespace fake_storage {

/// What storage_write() has seen since the last reset().
struct State {
  int write_calls = 0;       ///< storage_write() calls, failed ones included
  std::string last_name;     ///< the name of the last call
  std::string last_contents; ///< the contents of the last call
  bool fail_writes = false;  ///< scripted: storage_write() returns false and writes nothing
};

/// The recorded state; a case may set fail_writes.
State &state();

/// Forget the recorded calls and stop failing.
void reset();

/// The temporary folder standing in for /storage (made on first use).
const std::string &root();

} // namespace fake_storage
