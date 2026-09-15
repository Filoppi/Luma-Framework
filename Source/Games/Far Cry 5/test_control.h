#pragma once
#include <string_view>
#include <filesystem>
#include <fstream>
namespace FC5
{
#ifdef GAME_FAR_CRY_5
   inline std::filesystem::path ControlDirectory()
   {
#if DEVELOPMENT
      return std::filesystem::path(SOLUTION_DIR) / "Captures/Far Cry 5";
#else
      return System::GetModulePath().parent_path() / "Luma/Far Cry 5";
#endif
   }
#endif
   enum class TestCommand { Invalid, Native, DLAA, JitteredMV, FlippedJitter, CaptureNative, CaptureDLAA, ProbeScaling, DLSS, CaptureDLSS, ReporterPause, ReporterResume };
   inline TestCommand ParseTestCommand(std::string_view command)
   {
      if (!command.empty() && command.back() == '\r') command.remove_suffix(1);
      if (command == "native") return TestCommand::Native;
      if (command == "dlaa") return TestCommand::DLAA;
      if (command == "dlaa-jittered-mv") return TestCommand::JitteredMV;
      if (command == "dlaa-flipped-jitter") return TestCommand::FlippedJitter;
      if (command == "capture-native") return TestCommand::CaptureNative;
      if (command == "capture-dlaa") return TestCommand::CaptureDLAA;
      if (command == "probe-scaling") return TestCommand::ProbeScaling;
      if (command == "dlss") return TestCommand::DLSS;
      if (command == "capture-dlss") return TestCommand::CaptureDLSS;
      if (command == "reporter-pause") return TestCommand::ReporterPause;
      if (command == "reporter-resume") return TestCommand::ReporterResume;
      return TestCommand::Invalid;
   }
   struct TestControl
   {
      std::filesystem::file_time_type timestamp{};
      TestCommand Poll(const std::filesystem::path& path)
      {
         std::error_code error;
         const auto current = std::filesystem::last_write_time(path, error);
         if (error || current == timestamp) return TestCommand::Invalid;
         std::ifstream input(path);
         char line[64]{};
         if (!input.getline(line, sizeof(line))) return TestCommand::Invalid;
         timestamp = current;
         return ParseTestCommand(line);
      }
   };
}
