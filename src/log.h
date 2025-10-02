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
#define CORE_TRACE(...)    ::acceleratorcss::Log::GetLogger()->trace(__VA_ARGS__)
#define CORE_DEBUG(...)    ::acceleratorcss::Log::GetLogger()->debug(__VA_ARGS__)
#define CORE_INFO(...)     ::acceleratorcss::Log::GetLogger()->info(__VA_ARGS__)
#define CORE_WARN(...)     ::acceleratorcss::Log::GetLogger()->warn(__VA_ARGS__)
#define CORE_ERROR(...)    ::acceleratorcss::Log::GetLogger()->error(__VA_ARGS__)
#define CORE_CRITICAL(...) ::acceleratorcss::Log::GetLogger()->critical(__VA_ARGS__)
}
