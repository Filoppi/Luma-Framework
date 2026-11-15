#pragma once

// Logs in Development and Test builds. Publishing builds neither format nor keep the messages.
template <typename... Args>
void LogDiagnostic(reshade::log::level level, std::format_string<Args...> format, Args&&... args)
{
#if DEVELOPMENT || TEST
   reshade::log::message(level, ("NFS: " + std::format(format, std::forward<Args>(args)...)).c_str());
#endif
}
