// ============================================================================
//  Daedalus :: core/Version.hpp
//  Library identity and compile-time feature reporting.
// ============================================================================
#ifndef DAEDALUS_CORE_VERSION_HPP
#define DAEDALUS_CORE_VERSION_HPP

#include <string>

#define DAEDALUS_VERSION_MAJOR 1
#define DAEDALUS_VERSION_MINOR 0
#define DAEDALUS_VERSION_PATCH 0

namespace daedalus {

/// Semantic version of the library, e.g. "1.0.0".
[[nodiscard]] inline std::string version() {
    return std::to_string(DAEDALUS_VERSION_MAJOR) + '.' +
           std::to_string(DAEDALUS_VERSION_MINOR) + '.' +
           std::to_string(DAEDALUS_VERSION_PATCH);
}

/// Human-readable banner used by the CLI and by `--version` output.
[[nodiscard]] inline std::string banner() {
    return "Daedalus " + version() + " - C++20 data structures & algorithms library";
}

}  // namespace daedalus

#endif  // DAEDALUS_CORE_VERSION_HPP
