/* SPDX-License-Identifier: MIT */
#ifndef NAVDOWNLOAD_H
#define NAVDOWNLOAD_H
#include <atomic>
#include <filesystem>
#include <string>
// HTTPS streaming into a private temporary file; caller validates then installs.
void download_https(const std::string &url, const std::filesystem::path &output,
                    std::atomic<bool> &cancel, std::atomic<uint64_t> &bytes);
#endif
