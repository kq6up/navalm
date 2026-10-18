/* SPDX-License-Identifier: MIT */
#ifndef NAVPLATFORM_H
#define NAVPLATFORM_H
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
inline void nav_setenv(const char *key, const char *value, int overwrite) {
#ifdef _WIN32
    if (overwrite || !std::getenv(key))
        if (_putenv_s(key, value))
            throw std::runtime_error("Cannot set application environment.");
#else
    if (setenv(key, value, overwrite))
        throw std::runtime_error("Cannot set application environment.");
#endif
}
inline std::filesystem::path nav_temp_dir() {
    std::random_device random;
    for (int i = 0; i < 64; ++i) {
        auto p = std::filesystem::temp_directory_path() /
                 ("navalm-test-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code e;
        if (std::filesystem::create_directory(p, e))
            return p;
    }
    throw std::runtime_error("Cannot create private test directory.");
}
#ifdef _WIN32
inline void nav_unpack_resource(int id, const std::filesystem::path &target) {
    if (std::filesystem::exists(target))
        return;
    auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    if (!resource)
        return;
    DWORD size = SizeofResource(nullptr, resource);
    auto data = LoadResource(nullptr, resource);
    const void *bytes = data ? LockResource(data) : nullptr;
    if (!bytes || !size)
        throw std::runtime_error("Cannot read bundled map or ephemeris.");
    std::filesystem::create_directories(target.parent_path());
    auto part = target;
    part += "." + std::to_string(GetCurrentProcessId()) + ".part";
    {
        std::ofstream out(part, std::ios::binary);
        if (!out.write(static_cast<const char *>(bytes), size) || !out.flush())
            throw std::runtime_error(
                "Cannot unpack offline data beside NAVALM.exe. Use a writable folder.");
    }
    if (!MoveFileExW(part.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(part, ignored);
        if (!std::filesystem::exists(target))
            throw std::runtime_error("Cannot install bundled offline data.");
    }
}
#endif
#endif
