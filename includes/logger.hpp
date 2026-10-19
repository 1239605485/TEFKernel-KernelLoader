/*******************************************************************************
 * KernelLoader - logger
 * Copyright (C) 2026 eternalfuture-e38299
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Author: eternalfuture-e38299
 * GitHub: https://github.com/eternalfuture-e38299
 * Created: 2026/5/1
 *******************************************************************************/

#pragma once

#include <spdlog/spdlog.h>
#include "../mod-api/mod_logger.h"

#include <cstdio>
#include <mutex>
#include <string>
namespace kernelloader {
inline std::string diagnosticLogPath;
inline std::mutex diagnosticLogMutex;
inline void writeRuntimeLog(int level, const std::string &message) {
    {
        std::lock_guard<std::mutex> guard(diagnosticLogMutex);
        if (!diagnosticLogPath.empty()) {
            if (auto file = std::fopen(diagnosticLogPath.c_str(), "a")) {
                std::fprintf(file, "[%d] %s\n", level, message.c_str());
                std::fclose(file);
            }
        }
    }
}
template<class... Args> inline void persistentLog(int level, const char *pattern, const Args &...args) {
    const auto message = fmt::format(fmt::runtime(pattern), args...);
    writeRuntimeLog(level, message);
    if (auto logger = spdlog::get("KernelLoader"))
        logger->log(static_cast<spdlog::level::level_enum>(level), "{}", message);
}
}
#define LOG_TRACE(...) kernelloader::persistentLog(0, __VA_ARGS__)
#define LOG_DEBUG(...) kernelloader::persistentLog(1, __VA_ARGS__)
#define LOG_INFO(...) kernelloader::persistentLog(2, __VA_ARGS__)
#define LOG_WARN(...) kernelloader::persistentLog(3, __VA_ARGS__)
#define LOG_ERROR(...) kernelloader::persistentLog(4, __VA_ARGS__)
#define LOG_CRITICAL(...) kernelloader::persistentLog(5, __VA_ARGS__)


namespace kernelloader::logger {
    inline std::shared_ptr<spdlog::logger> g_logger{};
    inline std::shared_ptr<spdlog::logger> g_mod_logger{};

    void init();
    void shutdown();

    void init_mod();
    void shutdown_mod();

    extern "C" void mod_logger_write_imp(mod_log_level_t level, const char* tag, const char* fmt, ...);
}
