#pragma once

namespace core {

// "<git short hash>[+] <HH:MM>", generated at build time by cmake/build_info.cmake.
// "+" means the build had uncommitted changes.
extern const char* const kBuildId;

}  // namespace core
