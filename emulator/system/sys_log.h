/* =============================================================================
 * File: sys_sys_log.h
 * Component: Asynchronous logging (public API)
 *
 * Overview
 * --------
 * Public interface for the engine logger. Provides:
 *   - Severity enum (Debug/Info/Error/Off)
 *   - Initialization/teardown (LogOpen/LogClose)
 *   - Level control and optional console mirroring
 *   - Source-aware printf-style macros: LOG_DEBUG / LOG_INFO / LOG_ERROR
 *
 * Key Types & Macros
 * ------------------
 *   enum class Log::Level { Debug, Info, Error, Off };
 *     Minimum severity filter for emitted messages.
 *
 *   bool Log::open(const std::string& filename);
 *     Opens/creates the log file and starts the background writer thread.
 *
 *   void Log::close();
 *     Flushes and shuts down the logging system.
 *
 *   void Log::write(Level lvl, const char* file, const char* func, int line,
 *                   const char* fmt, ...);
 *     Low-level entry point; prefer the macros for convenience and source tags.
 *
 *   void Log::setLevel(Level level);
 *     Adjusts the minimum level to emit.
 *
 *   void Log::setConsoleOutputEnabled(bool enabled);
 *     Mirrors output to a Windows console (allocates one if needed).
 *
 *   // Convenience macros (source-tagged)
 *   LogOpen(f)   -> Log::open(f)
 *   LogClose()   -> Log::close()
 *   LOG_DEBUG()  -> Debug-level message
 *   LOG_INFO()   -> Info-level message
 *   LOG_ERROR()  -> Error-level message
 *
 * Timestamps
 * ----------
 *   Define LOG_WITH_TIMESTAMP before including this header to prepend
 *   ISO-like local timestamps to each message.
 *
 * Thread Safety
 * -------------
 *   All public functions/macros are safe to call from multiple threads. Messages
 *   are formatted at the call site and written by a dedicated worker thread.
 *
 * Usage (minimal)
 * ---------------
 *   #include "sys_log.h"
 *   LogOpen("game.log");
 *   LOG_INFO("Build %s ready", GIT_SHA1);
 *   LogClose();
 *
 * ---------------------------------------------------------------------------
 * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2012-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */

#pragma once

#include <string>

//#define LOG_WITH_TIMESTAMP

namespace Log {

    enum class Level {
        Debug = 0,
        Info = 1,
        Warn = 2,  
        Error = 3,
        Off = 4
    };

    // Initializes the logging system and starts the background thread.
    // Returns false if the log file could not be opened.
    bool open(const std::string& filename);

    // Flushes and shuts down logging system.
    void close();

    // Writes a formatted log message at the specified level with source info.
    void write(Level level, const char* file, const char* function, int line, const char* format, ...);

    // Change the minimum level of messages to log.
    void setLevel(Level level);

    // Enable or disable also writing messages to the console.
    void setConsoleOutputEnabled(bool enabled);
}

// Logging macros - safe, fast, and non-blocking.
#define LogOpen(f) Log::open(f)
#define LogClose() Log::close()
#define LOG_DEBUG(fmt, ...) Log::write(Log::Level::Debug, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Log::write(Log::Level::Info,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Log::write(Log::Level::Error, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Log::write(Log::Level::Warn,  __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)