/**
EnigmaFix Copyright (c) 2026 Bryce Q.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**/

// Internal Functionality
#include "LogManager.h"
#include "../Settings/PlayerSettings.h"
#include "../Utilities/CrashHandler.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <iostream>
#include <windows.h>

#include "spdlog/async.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
// System Libraries


auto& PlayerSettingsLM = EnigmaFix::PlayerSettings::Get();

// Singleton Instance
EnigmaFix::LogManager EnigmaFix::LogManager::lm_Instance;


// TODO: Figure out how to close up everything when the game exits. For some reason, the console window stays, and it doesn't save the full log files.
namespace EnigmaFix
{
    namespace {
        // An async logger queues messages for a worker thread and the file sink buffers them on top of that, so with no
        // flush policy at all nothing reaches disk until the logger is destroyed on exit. If the game crashes, or is
        // still running while the log is being read, the file just sits there empty.
        //
        // Warnings and errors flush immediately because those are the ones worth having when something goes wrong.
        // Everything else rides a one second timer, which keeps the per-draw logging in RenderManager from paying for a
        // file flush on every message.
        void ApplyFlushPolicy(const std::shared_ptr<spdlog::async_logger>& logger)
        {
            logger->flush_on(spdlog::level::warn);
            spdlog::flush_every(std::chrono::seconds(1));
        }
    }

    void LogManager::Init()
    {
        // Initialize async logging thread pool (if not already initialized)
        spdlog::init_thread_pool(8192, 1);  // Queue size: 8192, Threads: 1

        std::shared_ptr<spdlog::sinks::basic_file_sink_mt> file_sink = nullptr; // Declare file_sink outside the try block

        try {
            // Delete any existing EnigmaFix.log file
            std::filesystem::path logFilePath = "EnigmaFix.log";
            if (std::filesystem::exists(logFilePath)) {
                std::filesystem::remove(logFilePath);  // Delete the file
            }

            // Create the file sink
            file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("EnigmaFix.log", true);
            file_sink->set_level(spdlog::level::debug);
        }
        catch (const std::exception& ex) {
            std::cout << "Log initialization failed: " << ex.what() << std::endl;
        }

        // Show console window if EnableConsoleLog is enabled
        if (PlayerSettingsLM.MS.EnableConsoleLog) {
            AllocConsole();
            freopen_s((FILE**)stdout, "CONOUT$", "w", stdout); // Allows us to add outputs to the ASI Loader Console Window.

            // Initialize spdlog with a colored console sink
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::debug);  // Set log level to debug

            // Combine sinks into an async logger (ensure file_sink is valid before combining)
            if (file_sink) {
                auto async_logger = std::make_shared<spdlog::async_logger>(
                    "console",
                    spdlog::sinks_init_list{console_sink, file_sink}, // Combine both sinks (console and file)
                    spdlog::thread_pool(),
                    spdlog::async_overflow_policy::block
                );

                // Set the global logger
                spdlog::set_default_logger(async_logger);
                ApplyFlushPolicy(async_logger);

                // Optional: Set a global log level and pattern
                spdlog::set_level(spdlog::level::debug);  // Set global log level

                // Optional: Customize the log pattern (timestamp, log level, message)
                spdlog::set_pattern("[%m/%d/%Y - %I:%M:%S%p] [%^%l%$] %v");

                // Log initial message
                spdlog::info("Console logging initialized.");

                // Log initial message indicating successful initialization
                spdlog::info("Logger initialized successfully.");

                // Installed as soon as there is somewhere to write a report to.
                CrashHandler::Init();
            }
            else {
                std::cout << "File sink not initialized, logging will only be available in the console." << std::endl;
            }
        }
        else {
            // If EnableConsoleLog is disabled, initialize logging only to the file
            if (file_sink) {
                auto async_logger = std::make_shared<spdlog::async_logger>(
                    "file_logger",
                    spdlog::sinks_init_list{file_sink}, // Only use the file sink
                    spdlog::thread_pool(),
                    spdlog::async_overflow_policy::block
                );

                 // Set the global logger
                spdlog::set_default_logger(async_logger);
                ApplyFlushPolicy(async_logger);

                // Optional: Set a global log level and pattern
                spdlog::set_level(spdlog::level::debug);  // Set global log level

                // Optional: Customize the log pattern (timestamp, log level, message)
                spdlog::set_pattern("[%m/%d/%Y - %I:%M:%S%p] [%^%l%$] %v");

                // Log initial message
                spdlog::info("File logging initialized.");

                // Log initial message indicating successful initialization
                spdlog::info("Logger initialized successfully.");

                // Installed as soon as there is somewhere to write a report to.
                CrashHandler::Init();
            } else {
                std::cout << "File sink not initialized, no logging available." << std::endl;
            }
        }
    }
}
