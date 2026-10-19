#pragma once

#include "meta/profile.h"

namespace platform {

// The profile lives in `profile.dat` next to EBOOT.PBP (docs/formats/profile.md).
// Written as profile.tmp and renamed over the old file, so a power-off mid-write
// leaves the previous save intact.
bool saveProfile(const meta::Profile& p);
// False when there is no valid save (first run, corrupt file): `p` is untouched.
bool loadProfile(meta::Profile& p);

// The console's nickname from the system settings (UTF-8), or "" if unavailable.
void systemNickname(char* out, int size);

}  // namespace platform
