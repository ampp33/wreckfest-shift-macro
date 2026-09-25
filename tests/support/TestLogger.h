#pragma once

#include <string>
#include <vector>

namespace testlog {

/// Every line the plugin logged since the last clear(), each prefixed with
/// milliseconds since clear(). The plugin's Logger is replaced by an
/// in-memory one for tests (TestLogger.cpp) so a failing test can print
/// the plugin's own debug trace alongside the recorded controller events.
std::vector<std::string> lines();
void clear();

} // namespace testlog
