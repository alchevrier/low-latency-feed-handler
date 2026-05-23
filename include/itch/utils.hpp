#pragma once
#include <cstring>

namespace itch {
    template<typename T>
    T read(const uint8_t* ptr) {
        T value;
        std::memcpy(&value, ptr, sizeof(T));
        return value;
    }
}