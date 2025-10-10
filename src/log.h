//
// Created by Michal Přikryl on 12.06.2025.
// Copyright (c) 2025 slynxcz. All rights reserved.
//
#include <memory>
#include <spdlog/spdlog.h>

namespace acceleratorcss {
    class Log {
    public:
        static void Init();

        static void Close();

        static std::shared_ptr<spdlog::logger> &GetLogger() { return m_core_logger; }

    private:
        static std::shared_ptr<spdlog::logger> m_core_logger;
    };

    // Shortcuts
#define CORE_TRACE(fmt, ...)    ::acceleratorcss::Log::GetLogger()->trace("- [ " fmt " ] -", ##__VA_ARGS__)
#define CORE_DEBUG(fmt, ...)    ::acceleratorcss::Log::GetLogger()->debug("- [ " fmt " ] -", ##__VA_ARGS__)
#define CORE_INFO(fmt, ...)     ::acceleratorcss::Log::GetLogger()->info("- [ " fmt " ] -", ##__VA_ARGS__)
#define CORE_WARN(fmt, ...)     ::acceleratorcss::Log::GetLogger()->warn("- [ " fmt " ] -", ##__VA_ARGS__)
#define CORE_ERROR(fmt, ...)    ::acceleratorcss::Log::GetLogger()->error("- [ " fmt " ] -", ##__VA_ARGS__)
#define CORE_CRITICAL(fmt, ...) ::acceleratorcss::Log::GetLogger()->critical("- [ " fmt " ] -", ##__VA_ARGS__)
}
