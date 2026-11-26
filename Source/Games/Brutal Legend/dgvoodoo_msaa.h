#pragma once

// Forced MSAA through dgVoodoo: reads and writes the [DirectX] "Antialiasing" key of dgVoodoo.conf, next to the game exe.
// The conf file is the source of truth (nothing is stored in the ReShade config). Takes effect on the next game start.
// Edits are made byte-for-byte: every other line keeps its content and line ending.

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace BLDgVoodooMSAA
{
   // Index of the UI list, same order as kLabels and kValues.
   enum Mode : int { Appdriven = 0, Off, X2, X4, X8, Count };

   inline const char* const kLabels[Count] = { "Game Default", "Off", "2x", "4x", "8x" };
   inline const char* const kValues[Count] = { "appdriven", "off", "2x", "4x", "8x" };

   struct Status
   {
      bool available = false;
      std::string reason; // Why the option is unavailable (shown as a tooltip)
      int mode = Appdriven; // Value read from the conf (Appdriven when the key is absent)
   };

   struct Line
   {
      std::string body; // Without the line ending
      std::string eol;  // "\r\n", "\n" or "" (last line without a line ending)
   };

   inline std::wstring GetConfPath()
   {
      std::vector<wchar_t> buf(32768);
      DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
      if (n == 0 || n >= buf.size())
         return {};
      std::wstring exe(buf.data(), n);
      size_t sep = exe.find_last_of(L"\\/");
      if (sep == std::wstring::npos)
         return {};
      return exe.substr(0, sep + 1) + L"dgVoodoo.conf";
   }

   inline bool ReadFileBytes(const std::wstring& path, std::string& out)
   {
      HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE)
         return false;
      LARGE_INTEGER size = {};
      if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > (LONGLONG)(16 * 1024 * 1024))
      {
         CloseHandle(h);
         return false;
      }
      out.resize((size_t)size.QuadPart);
      size_t total = 0;
      bool ok = true;
      while (total < out.size())
      {
         DWORD read = 0;
         if (!ReadFile(h, out.data() + total, (DWORD)(out.size() - total), &read, nullptr) || read == 0)
         {
            ok = false;
            break;
         }
         total += read;
      }
      CloseHandle(h);
      return ok;
   }

   inline std::vector<Line> SplitLines(const std::string& bytes)
   {
      std::vector<Line> lines;
      size_t start = 0;
      while (start < bytes.size())
      {
         size_t nl = bytes.find('\n', start);
         Line line;
         if (nl == std::string::npos)
         {
            line.body = bytes.substr(start);
            line.eol = "";
            start = bytes.size();
         }
         else
         {
            line.body = bytes.substr(start, nl - start);
            line.eol = "\n";
            start = nl + 1;
         }
         if (!line.eol.empty() && !line.body.empty() && line.body.back() == '\r')
         {
            line.body.pop_back();
            line.eol = "\r\n";
         }
         lines.push_back(std::move(line));
      }
      return lines;
   }

   inline std::string Trim(const std::string& s)
   {
      size_t a = s.find_first_not_of(" \t");
      if (a == std::string::npos)
         return {};
      size_t b = s.find_last_not_of(" \t");
      return s.substr(a, b - a + 1);
   }

   inline std::string Lower(std::string s)
   {
      for (char& c : s)
         c = (char)std::tolower((unsigned char)c);
      return s;
   }

   // Returns true for a "[Section]" line and sets section to its lowercase name.
   inline bool ParseHeader(const std::string& body, std::string& section)
   {
      std::string t = Trim(body);
      if (t.size() < 2 || t.front() != '[' || t.back() != ']')
         return false;
      section = Lower(Trim(t.substr(1, t.size() - 2)));
      return true;
   }

   // Returns true for a "Key = Value" line. key is lowercase, value is lowercase and trimmed, eq is the position of '='.
   inline bool ParseKey(const std::string& body, std::string& key, std::string& value, size_t& eq)
   {
      eq = body.find('=');
      if (eq == std::string::npos)
         return false;
      key = Lower(Trim(body.substr(0, eq)));
      value = Lower(Trim(body.substr(eq + 1)));
      return !key.empty();
   }

   inline int ModeFromValue(const std::string& value)
   {
      for (int i = 0; i < Count; i++)
      {
         if (value == kValues[i])
            return i;
      }
      return Appdriven;
   }

   inline bool HasGeneralSection(const std::vector<Line>& lines)
   {
      std::string section;
      for (const Line& l : lines)
      {
         if (ParseHeader(l.body, section) && section == "general")
            return true;
      }
      return false;
   }

   // Reads the current Antialiasing value from dgVoodoo.conf. Missing file or binary conf: unavailable, with the reason.
   inline Status ReadStatus()
   {
      Status st;
      std::wstring path = GetConfPath();
      if (path.empty())
      {
         st.reason = "Could not locate the game folder.";
         return st;
      }
      std::string bytes;
      if (!ReadFileBytes(path, bytes))
      {
         st.reason = "dgVoodoo.conf was not found next to the game exe.";
         return st;
      }
      std::vector<Line> lines = SplitLines(bytes);
      if (!HasGeneralSection(lines))
      {
         st.reason = "dgVoodoo.conf is a binary config, not a text file. Set MSAA in dgVoodoo's own settings.";
         return st;
      }

      std::string section, key, value;
      size_t eq = 0;
      for (const Line& l : lines)
      {
         if (ParseHeader(l.body, section))
            continue;
         if (section == "directx" && ParseKey(l.body, key, value, eq) && key == "antialiasing")
         {
            st.mode = ModeFromValue(value);
            break;
         }
      }
      st.available = true;
      return st;
   }

   // Writes the Antialiasing value to dgVoodoo.conf. Returns false with error set on failure (the file is left untouched).
   inline bool WriteMode(int mode, std::string& error)
   {
      if (mode < 0 || mode >= Count)
         mode = Appdriven;
      const std::string value = kValues[mode];

      std::wstring path = GetConfPath();
      std::string bytes;
      if (path.empty() || !ReadFileBytes(path, bytes))
      {
         error = "dgVoodoo.conf was not found next to the game exe.";
         return false;
      }
      std::vector<Line> lines = SplitLines(bytes);
      if (!HasGeneralSection(lines))
      {
         error = "dgVoodoo.conf is a binary config, not a text file.";
         return false;
      }

      std::string nl = "\r\n";
      for (const Line& l : lines)
      {
         if (!l.eol.empty())
         {
            nl = l.eol;
            break;
         }
      }

      // Find the [DirectX] header and an existing Antialiasing key in that section.
      int header_index = -1;
      int key_index = -1;
      size_t key_eq = 0;
      std::string section, key, existing;
      bool in_directx = false;
      for (size_t i = 0; i < lines.size(); i++)
      {
         if (ParseHeader(lines[i].body, section))
         {
            in_directx = (section == "directx");
            if (in_directx && header_index < 0)
               header_index = (int)i;
            continue;
         }
         if (in_directx && key_index < 0 && ParseKey(lines[i].body, key, existing, key_eq) && key == "antialiasing")
            key_index = (int)i;
      }

      std::string out;
      if (key_index >= 0)
      {
         // Keep the key's own spacing and the whitespace after '=', replace only the value.
         Line& l = lines[key_index];
         std::string prefix = l.body.substr(0, key_eq + 1); // up to and including '='
         std::string rest = l.body.substr(key_eq + 1);
         size_t ws = rest.find_first_not_of(" \t");
         std::string gap = (ws == std::string::npos) ? std::string(" ") : rest.substr(0, ws);
         l.body = prefix + gap + value;
      }
      else if (header_index >= 0)
      {
         // Insert right after the [DirectX] header.
         Line& header = lines[header_index];
         if (header.eol.empty())
            header.eol = nl;
         Line added;
         added.body = "Antialiasing = " + value;
         added.eol = nl;
         lines.insert(lines.begin() + header_index + 1, added);
      }

      if (key_index >= 0 || header_index >= 0)
      {
         for (const Line& l : lines)
            out += l.body + l.eol;
      }
      else
      {
         out = bytes;
         if (!out.empty() && out.back() != '\n')
            out += "\r\n";
         out += "[DirectX]\r\nAntialiasing = " + value + "\r\n";
      }

      // Write a temp file in the same folder, then swap it in.
      std::wstring tmp = path + L".luma.tmp";
      HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE)
      {
         error = "could not create a temp file (error " + std::to_string(GetLastError()) + ").";
         return false;
      }
      DWORD written = 0;
      bool ok = WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size();
      ok = FlushFileBuffers(h) && ok;
      CloseHandle(h);
      if (!ok)
      {
         DeleteFileW(tmp.c_str());
         error = "could not write the temp file (error " + std::to_string(GetLastError()) + ").";
         return false;
      }
      if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      {
         DWORD err = GetLastError();
         DeleteFileW(tmp.c_str());
         error = "could not replace dgVoodoo.conf (error " + std::to_string(err) + ").";
         return false;
      }
      return true;
   }
}
