# Vendored dependencies

## toml.hpp — toml++ v3.4.0

Single-header TOML parser used by `src/config/Config.cpp`.

- Upstream: https://github.com/marzer/tomlplusplus
- License: MIT (full text embedded at the top of `toml.hpp`)

Vendored directly (rather than pulled in via CMake FetchContent or a
system package) so the build has no network dependency and doesn't
require a `libtomlplusplus-dev`-style package that most distributions
don't ship. To upgrade, replace `toml.hpp` with a newer release's
single-header amalgamation from the upstream repository.
