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
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <stdexcept>
// Keep unexpected Windows faults diagnosable without writing navigation data.
#ifdef _WIN32
inline wchar_t nav_error_log_path[32768]{};
inline char nav_desktop_action[128] = "Starting NAVALM";
inline void nav_set_desktop_action(const char *action) {
    std::snprintf(nav_desktop_action, sizeof nav_desktop_action, "%s", action ? action : "Action");
}
inline void nav_append_diagnostic(const char *message) {
    if (!nav_error_log_path[0])
        return;
    HANDLE file =
        CreateFileW(nav_error_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written;
    WriteFile(file, message, (DWORD)std::strlen(message), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}
inline LONG WINAPI nav_windows_fault(EXCEPTION_POINTERS *fault) {
    char text[1024];
    auto address = reinterpret_cast<std::uintptr_t>(fault->ExceptionRecord->ExceptionAddress);
    auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::snprintf(text, sizeof text,
                  "Unexpected Windows fault; build input-guards-1 (%s %s)\r\n"
                  "Action: %s\r\nCode: 0x%08lX; module offset: 0x%llX\r\n\r\n",
                  __DATE__, __TIME__, nav_desktop_action,
                  (unsigned long)fault->ExceptionRecord->ExceptionCode,
                  (unsigned long long)(address - base));
    nav_append_diagnostic(text);
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void nav_enable_diagnostics(const std::filesystem::path &directory) {
    auto path = (directory / "desktop-errors.log").wstring();
    if (path.size() < sizeof nav_error_log_path / sizeof *nav_error_log_path)
        std::wmemcpy(nav_error_log_path, path.c_str(), path.size() + 1);
    SetUnhandledExceptionFilter(nav_windows_fault);
}
inline void nav_windows_error(const char *message) {
    char text[2048];
    std::snprintf(text, sizeof text, "Handled error; action: %s\r\n%s\r\n\r\n", nav_desktop_action,
                  message);
    nav_append_diagnostic(text);
    int count = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    std::wstring wide(count ? count : 1, L'\0');
    if (count)
        MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), count);
    MessageBoxW(GetActiveWindow(), wide.c_str(), L"NAVALM — check your inputs",
                MB_OK | MB_ICONINFORMATION);
}
#else
inline void nav_set_desktop_action(const char *) {}
inline void nav_enable_diagnostics(const std::filesystem::path &) {}
#endif
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
inline void nav_unpack_resource(int id, const std::filesystem::path &target,
                                const std::function<void(size_t, size_t)> &progress = {}) {
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
        if (progress)
            progress(0, size);
        for (size_t done = 0; done < size;) {
            size_t chunk = std::min(size_t(1024 * 1024), size_t(size) - done);
            if (!out.write(static_cast<const char *>(bytes) + done, chunk))
                throw std::runtime_error(
                    "Cannot unpack offline data beside NAVALM.exe. Use a writable folder.");
            done += chunk;
            if (progress)
                progress(done, size);
        }
        if (!out.flush())
            throw std::runtime_error("Cannot finish writing the bundled offline data.");
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
