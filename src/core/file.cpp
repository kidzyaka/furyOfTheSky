#include "core/file.h"

#include <malloc.h>
#include <pspkernel.h>

#include <cstdio>
#include <cstdlib>

namespace core {

Blob& Blob::operator=(Blob&& o) noexcept {
    if (this != &o) {
        free(data);
        data = o.data;
        size = o.size;
        o.data = nullptr;
        o.size = 0;
    }
    return *this;
}

Blob::~Blob() { free(data); }

Blob loadFile(const char* path) {
    Blob blob;
    FILE* f = std::fopen(path, "rb");
    if (!f) return blob;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0) {
        blob.data = static_cast<uint8_t*>(memalign(16, static_cast<size_t>(size)));
        if (blob.data && std::fread(blob.data, 1, size, f) == static_cast<size_t>(size)) {
            blob.size = static_cast<size_t>(size);
            // The GE fetches through the bus, bypassing the CPU cache: anything
            // still sitting dirty in the D-cache would be read as garbage.
            sceKernelDcacheWritebackRange(blob.data, blob.size);
        } else {
            free(blob.data);
            blob.data = nullptr;
        }
    }
    std::fclose(f);
    return blob;
}

}  // namespace core
