#pragma once
#include <switch.h>
#include <cstdarg>
#include <cstdio>

inline bool DebugLog(const char* format, ...) {
    FILE* file = std::fopen("sdmc:/joyoff-debug.txt", "a");
    if (!file) return false;
    const auto milliseconds = static_cast<unsigned long long>(armTicksToNs(armGetSystemTick()) / 1000000);
    bool written = std::fprintf(file, "[%llu ms] ", milliseconds) >= 0;
    va_list arguments;
    va_start(arguments, format);
    written = (std::vfprintf(file, format, arguments) >= 0) && written;
    va_end(arguments);
    written = (std::fputc('\n', file) != EOF) && written;
    return std::fclose(file) == 0 && written;
}

inline void DebugBytes(const char* label, const void* buffer, size_t size) {
    const auto* data = static_cast<const u8*>(buffer);
    char hex[3 * 64 + 1]{};
    const size_t count = size < 64 ? size : 64;
    for (size_t i = 0; i < count; ++i) std::snprintf(hex + i * 3, 4, "%02X ", data[i]);
    DebugLog("%s (%zu bytes, first %zu): %s", label, size, count, hex);
}

inline void DebugDevice(const char* source, const BtmConnectedDeviceV13& device) {
    const auto* address = reinterpret_cast<const u8*>(&device.address);
    DebugLog("%s address=%02X:%02X:%02X:%02X:%02X:%02X profile=%u name=\"%.*s\"",
        source, address[0], address[1], address[2], address[3], address[4], address[5],
        static_cast<unsigned>(device.profile), static_cast<int>(sizeof(device.name)), device.name);
    DebugBytes("Bluetooth name raw", device.name, sizeof(device.name));
}
