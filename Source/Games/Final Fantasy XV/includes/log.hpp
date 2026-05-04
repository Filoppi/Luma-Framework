#pragma once

#include <string>

inline void Log_Debug(reshade::log::level level, const char* message)
{
#if DEBUG_LOG
    reshade::log::message(level, message);
#endif
}

inline void Log_Debug(reshade::log::level level, const std::string& message)
{
    Log_Debug(level, message.c_str());
}
