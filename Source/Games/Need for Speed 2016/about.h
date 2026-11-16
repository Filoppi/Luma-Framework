#pragma once

// Luma's "About" tab
inline void DrawAbout()
{
   ImGui::PushTextWrapPos(0.f);
   ImGui::Text(
      "Luma for \"Need for Speed (2016)\" is developed by PetarBadzhakov and is open source and free.\n"
      "It replaces the game's TAA with super resolution anti-aliasing: DLSS, FSR 4, XeSS or FSR 3.\n"
      "Set Anti-Aliasing in the game's video settings to TAA. Luma's \"Auto\" Super Resolution picks the best one your GPU supports.\n"
      "The Resolution Scale setting renders the game above the screen's resolution (supersampling), for GPUs with performance to spare.\n"
      "Thanks to the Luma team and contributors, and to DristoforColumb's Mass Effect: Andromeda Luma mod, which this is based on.");
   ImGui::PopTextWrapPos();

   ImGui::NewLine();
   static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
   if (ImGui::Button(social_link.c_str()))
   {
      // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
      static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
      ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
   }
   static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
   if (ImGui::Button(contributing_link.c_str()))
      ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);

   ImGui::NewLine();
   ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

   ImGui::NewLine();
   ImGui::Text("Credits:"
               "\n\nMain:"
               "\nPetarBadzhakov"
               "\n\nBased on:"
               "\nMass Effect: Andromeda Luma mod (DristoforColumb)"
               "\n\nThird Party:"
               "\nReShade"
               "\nImGui"
               "\nAMD FidelityFX (FSR 3, FSR 4)"
               "\nIntel XeSS"
               "\nNVIDIA NGX (DLSS)",
      "");
}
