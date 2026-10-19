#include "platform/psp_save.h"

#include <psputility_sysparam.h>

#include <cstdio>
#include <cstring>

namespace platform {

namespace {
constexpr const char* kFile = "profile.dat";
constexpr const char* kTemp = "profile.tmp";

bool readAll(const char* path, uint8_t* buf, size_t cap, size_t& size) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    size = std::fread(buf, 1, cap, f);
    std::fclose(f);
    return size > 0;
}
}  // namespace

bool saveProfile(const meta::Profile& p) {
    uint8_t buf[meta::kProfileSaveBytes];
    const size_t n = meta::serialize(p, buf, sizeof buf);
    if (n == 0) return false;
    FILE* f = std::fopen(kTemp, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, n, f) == n;
    if (std::fclose(f) != 0 || !ok) return false;
    std::remove(kFile);
    return std::rename(kTemp, kFile) == 0;
}

bool loadProfile(meta::Profile& p) {
    uint8_t buf[meta::kProfileSaveBytes];
    size_t n = 0;
    if (readAll(kFile, buf, sizeof buf, n) && meta::deserialize(p, buf, n)) return true;
    // Power lost between remove and rename: the new save is still in the temp file.
    return readAll(kTemp, buf, sizeof buf, n) && meta::deserialize(p, buf, n);
}

void systemNickname(char* out, int size) {
    if (size <= 0) return;
    out[0] = 0;
    char name[128] = {};
    if (sceUtilityGetSystemParamString(PSP_SYSTEMPARAM_ID_STRING_NICKNAME, name, sizeof name) == 0) {
        std::strncpy(out, name, size - 1);
        out[size - 1] = 0;
    }
}

}  // namespace platform
