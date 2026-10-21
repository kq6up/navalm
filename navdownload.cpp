/* SPDX-License-Identifier: MIT */
#include "navdownload.h"
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
void download_https(const std::string &url, const std::filesystem::path &output,
                    std::atomic<bool> &cancel, std::atomic<uint64_t> &bytes) {
    if (url.rfind("https://", 0) != 0)
        throw std::runtime_error("Map download requires HTTPS.");
#ifdef _WIN32
    auto wide = [](const std::string &s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L' ');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    };
    struct Handle {
        HINTERNET h = nullptr;
        ~Handle() {
            if (h)
                WinHttpCloseHandle(h);
        }
    };
    auto w = wide(url);
    URL_COMPONENTS u{};
    u.dwStructSize = sizeof u;
    u.dwHostNameLength = (DWORD)-1;
    u.dwUrlPathLength = (DWORD)-1;
    u.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &u))
        throw std::runtime_error("Invalid download URL.");
    std::wstring host(u.lpszHostName, u.dwHostNameLength), path(u.lpszUrlPath, u.dwUrlPathLength);
    path.append(u.lpszExtraInfo, u.dwExtraInfoLength);
    Handle session{WinHttpOpen(L"NAVALM/5.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h)
        throw std::runtime_error("Cannot start HTTPS download.");
    WinHttpSetTimeouts(session.h, 15000, 15000, 15000, 15000);
    Handle connection{WinHttpConnect(session.h, host.c_str(), u.nPort, 0)};
    Handle request{connection.h
                       ? WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE)
                       : nullptr};
    if (!request.h ||
        !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                            0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        throw std::runtime_error("HTTPS download failed.");
    DWORD status = 0, length = sizeof status;
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
                             WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
        throw std::runtime_error("Map server returned an HTTP error.");
    std::ofstream out(output, std::ios::binary);
    char buffer[65536];
    DWORD n = 0;
    while (!cancel) {
        if (!WinHttpReadData(request.h, buffer, sizeof buffer, &n))
            throw std::runtime_error("Download interrupted.");
        if (!n)
            break;
        if (!(out.write(buffer, n)))
            throw std::runtime_error("Cannot write map download.");
        bytes += n;
        if (bytes > 256 * 1024 * 1024)
            throw std::runtime_error("Map download exceeds 256 MB.");
    }
    if (cancel)
        throw std::runtime_error("Download cancelled.");
    if (!out.flush())
        throw std::runtime_error("Cannot finish map download.");
#else
    // macOS ships curl. Pass arguments directly; no shell or interpolation.
    std::string target = output.string();
    const char *args[] = {"/usr/bin/curl",
                          "--fail",
                          "--location",
                          "--silent",
                          "--show-error",
                          "--proto",
                          "=https",
                          "--proto-redir",
                          "=https",
                          "--connect-timeout",
                          "15",
                          "--max-time",
                          "180",
                          "--max-filesize",
                          "268435456",
                          "--output",
                          target.c_str(),
                          url.c_str(),
                          nullptr};
    pid_t pid;
    int error =
        posix_spawn(&pid, args[0], nullptr, nullptr, const_cast<char *const *>(args), environ);
    if (error)
        throw std::runtime_error("Cannot start the system HTTPS downloader.");
    int status = 0;
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid)
            break;
        if (done < 0)
            throw std::runtime_error("Cannot wait for download.");
        std::error_code ec;
        auto n = std::filesystem::file_size(output, ec);
        if (!ec)
            bytes = n;
        if (cancel) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            throw std::runtime_error("Download cancelled.");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("Map download failed. Check the connection and try again.");
#endif
}
