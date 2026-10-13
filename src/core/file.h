#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

// Whole-file blob in 16-byte aligned memory, already written back from the
// CPU data cache so the GE can read it directly.
struct Blob {
    uint8_t* data = nullptr;
    size_t size = 0;

    Blob() = default;
    Blob(const Blob&) = delete;
    Blob& operator=(const Blob&) = delete;
    Blob(Blob&& o) noexcept : data(o.data), size(o.size) { o.data = nullptr; o.size = 0; }
    Blob& operator=(Blob&& o) noexcept;
    ~Blob();

    explicit operator bool() const { return data != nullptr; }
};

// Path is relative to the EBOOT's folder. Returns an empty blob on failure.
Blob loadFile(const char* path);

}  // namespace core
