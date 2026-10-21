#pragma once

namespace Shader
{
   constexpr uint32_t MAX_SHADER_DEFINES = // Avoid setting this too big as it bloats the ReShade config whether used or not. Don't go beyond 100 (max array index 99) without changing code related to this.
#ifdef MAX_SHADER_DEFINES_OVERRIDE
      MAX_SHADER_DEFINES_OVERRIDE;
#elif DEVELOPMENT || TEST
      50;
#else
      30;
#endif
   constexpr uint32_t SHADER_DEFINES_MAX_NAME_LENGTH = 50 + 1; // Increase if necessary (+ 1 is for to null terminate the string)
   constexpr uint32_t SHADER_DEFINES_MAX_VALUE_LENGTH = 1 + 1; // 1 character (+ 1 is for to null terminate the string)

   // Hacky list to make words nicer. Add any here if needed.
	// A few cases would be better handled with a redirection, like "SRGB" to "sRGB", but whatever.
   static const std::unordered_set<std::string> upper_case_names = {
		"HDR", "HDR10","SDR", "AA", "SSAO", "LUT", "UI", "HUD", "FXAA", "SMAA", "DLSS", "FSR", "TAA", "SRGB", "SCRGB", "RGB", "RGBA"
   };

   std::string NameToTitleCase(const std::string& input)
   {
      std::string spaced = input;
      std::replace(spaced.begin(), spaced.end(), '_', ' ');

      std::istringstream iss(spaced);
      std::string token, result;

      while (iss >> token)
      {
         if (upper_case_names.count(token))
         {
            // leave it as is
         }
         else
         {
            // lowercase the whole token
            std::transform(token.begin(), token.end(), token.begin(), ::tolower);
            // uppercase first character
            token[0] = static_cast<char>(::toupper(token[0]));
         }

         if (!result.empty())
            result += ' ';
         result += token;
      }

      return result;
   }

   struct ShaderDefine
   {
      ShaderDefine(const char* _name = "", char _value = '\0')
      {
         assert(std::strlen(_name) < SHADER_DEFINES_MAX_NAME_LENGTH);
         strncpy(&name[0], _name, std::max<size_t>(std::size(name), std::strlen(_name)));
         value[0] = _value;
         value[1] = '\0';
      }

      const char* GetName() const { return &name[0]; }
      const char* GetValue() const { return &value[0]; }

      char* GetName() { return &name[0]; }
      char* GetValue() { return &value[0]; }

      // This assumes the value was numerical to begin with (it usually is), and within 0-9
      uint8_t GetNumericalValue() const
      {
         // Avoid edge characters (that ImGui allows to write)
         if (value[0] == '\0' || value[0] == ' ' || value[0] == '-')
         {
            // Default to 0 if we have no default value
            if (value[0] == '\0')
            {
               return 0;
            }
            return value[0] - '0';
         }
         return value[0] - '0';
      }

      char name[SHADER_DEFINES_MAX_NAME_LENGTH];
      // Char and not int because defines are taken as text by the compiler, and we are fine with the 255 limit.
      // In fact, we only ideally want one positive numerical character,
      // so the range is 0-9 in char (the values don't match integers),
      // and also '\0' to simply define a define without a value, but these are advised against.
      // Only the first character is used, the second is to null terminate the string and should never have any other value.
      char value[SHADER_DEFINES_MAX_VALUE_LENGTH];
   };

   uint32_t defines_count = 0;
   // Extra flag to specify we need a recompilation
   bool defines_need_recompilation = false;
   bool auto_recompile_defines
#if DEVELOPMENT
      = false;
#else
      = true;
#endif

   struct SimplerShaderDefine
   {
      std::string name;
      std::string value;

      bool operator==(const SimplerShaderDefine& other) const noexcept
      {
         return name == other.name && value == other.value;
      }
   };

   struct ShaderDefineData
   {
      ShaderDefineData(const char* name = "", char value = '\0', bool _fixed_name = false, bool _fixed_value = false, const char* _tooltip = nullptr, uint8_t _max_value = 0) :
         name_hint("Define " + std::to_string(defines_count) + " Name"),
         value_hint("Define " + std::to_string(defines_count) + " Value"),
         fixed_name(_fixed_name),
         fixed_value(_fixed_value),
         default_data(name, value),
         tooltip(_tooltip)
      {
         defines_count++;
         editable_data = default_data;
         // Ignore the max value if this define is editable/custom
         if (fixed_name)
         {
            max_value = _max_value;
         }
      }

      // The default label/hint (auto generated)
      const std::string name_hint;
      const std::string value_hint;

      bool editable = true;

   private:
      // Set to true if you want the name of this define to be fixed
      const bool fixed_name;
      bool fixed_value;

      const char* tooltip;

   public:
      // The current (possibly editable) name and value
      ShaderDefine editable_data;
      // The default name and value
      ShaderDefine default_data;
      // The last name and value that got compiled into shaders
      ShaderDefine compiled_data;

      // Ignored if 0
      uint8_t max_value = 0;

      bool IsNameEditable() const
      {
   #if DEVELOPMENT
         return !fixed_name && editable;
   #elif !TEST
         return false;
   #endif // DEVELOPMENT
         return !fixed_name && editable && IsCustom();
      }
      bool IsValueEditable() const
      {
   #if DEVELOPMENT || TEST
         return !fixed_value && editable;
   #endif // DEVELOPMENT
         return !fixed_value && editable && !IsCustom();
      }

      void SetTooltip(const char* _tooltip)
      {
         tooltip = _tooltip;
      }
      void SetValueFixed(bool _fixed_value)
      {
         fixed_value = _fixed_value;
      }

      // If true, this a "custom" shader define created at runtime
      bool IsCustom() const
      {
         return strcmp(default_data.GetName(), "") == 0;
      }
      bool IsNameEmpty() const
      {
         return strcmp(editable_data.GetName(), "") == 0;
      }
      bool IsValueEmpty() const
      {
         return editable_data.value[0] == '\0'; // The second character (SHADER_DEFINES_MAX_VALUE_LENGTH) doesn't matter
      }
      // Whether it's fully null/empty
      bool IsEmpty() const
      {
         return IsNameEmpty() && IsValueEmpty();
      }
      bool IsNameDefault() const
      {
         return strcmp(editable_data.GetName(), default_data.GetName()) == 0;
      }
      bool IsValueDefault() const
      {
         return editable_data.value[0] == default_data.value[0];
      }
      // Whether is has the default name/value
      bool IsDefault() const
      {
         return IsNameDefault() && IsValueDefault();
      }
      // Whether it needs to be compiled for the values to apply (it's "dirty")
      bool NeedsCompilation() const
      {
         return strcmp(editable_data.GetName(), compiled_data.GetName()) != 0 || (editable_data.value[0] != compiled_data.value[0]);
      }

      void SetValue(char value)
      {
         editable_data.value[0] = value;
      }
      // To be called on startup only, to change the defaults per project (it changes the current value too, but then we should go through loading anyway)
      void SetDefaultValue(char value)
      {
         editable_data.value[0] = value;
         default_data.value[0] = value;
      }
      void Reset()
      {
         strncpy(editable_data.GetName(), default_data.GetName(), SHADER_DEFINES_MAX_NAME_LENGTH);
         editable_data.value[0] = default_data.value[0];
      }
      void Restore()
      {
         strncpy(editable_data.GetName(), compiled_data.GetName(), SHADER_DEFINES_MAX_NAME_LENGTH);
         editable_data.value[0] = compiled_data.value[0];
      }
      void Clear()
      {
         // No need to clear the remaining characters after the null value (nor to set the value to the value of 0)
         editable_data.name[0] = '\0';
         editable_data.value[0] = '\0';
      }
      void OnCompilation()
      {
         strncpy(compiled_data.GetName(), editable_data.GetName(), SHADER_DEFINES_MAX_NAME_LENGTH);
         compiled_data.value[0] = editable_data.value[0];
      }

      // This assumes the value was numerical to begin with (it usually is), and within 0-9
      uint8_t GetCompiledNumericalValue() const
      {
         return compiled_data.GetNumericalValue();
      }

      bool HasTooltip() const { return tooltip != nullptr && tooltip[0] != '\0'; }
      const char* GetTooltip() const { return tooltip; }

      static void Reset(std::vector<ShaderDefineData>& shader_defines_data)
      {
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            shader_defines_data[i].Reset();
         }
      }
      static void Restore(std::vector<ShaderDefineData>& shader_defines_data)
      {
         // TODO: also call something like "RemoveCustomData()" to remove all the custom defines that have been added to the list but never been compiled yet
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            shader_defines_data[i].Restore();
         }
      }
      static void OnCompilation(std::vector<ShaderDefineData>& shader_defines_data)
      {
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            shader_defines_data[i].OnCompilation();
         }
         defines_need_recompilation = false;
      }

      static bool ContainsName(const std::vector<ShaderDefineData>& shader_defines_data, const char* name, uint32_t excluded_index = UINT_MAX /*MAX_SHADER_DEFINES*/)
      {
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            if (i != excluded_index && strcmp(shader_defines_data[i].editable_data.GetName(), name) == 0)
            {
               return true;
            }
         }
         return false;
      }

      static void RemoveCustomData(std::vector<ShaderDefineData>& shader_defines_data, bool only_if_empty = false)
      {
   #if 0
         for (auto it = shader_defines_data.begin(); it != shader_defines_data.end(); )
         {
            if (it->IsCustom() && (!only_if_empty || it->IsEmpty()))
            {
               // We can't keep track of changes in the defines we removed so mark them as dirty before removing them
               defines_need_recompilation |= it->NeedsCompilation() || !it->IsEmpty();
               it = shader_defines_data.erase(it);
            }
            else
            {
               ++it;
            }
         }
   #else // The above implementation fails to compile due to using "const" on member variables of the vector struct, due to some deleted constructor. We work around it by re-creating the vector from scratch
         const auto shader_defines_data_copy = shader_defines_data;
         shader_defines_data.clear();
         for (auto it = shader_defines_data_copy.begin(); it != shader_defines_data_copy.end(); )
         {
            if (!it->IsCustom() || (only_if_empty && !it->IsEmpty()))
               shader_defines_data.emplace_back(*it);
            else // We can't keep track of changes in the defines we removed so mark them as dirty before removing them
               defines_need_recompilation |= it->NeedsCompilation() || !it->IsEmpty();
            ++it;
         }
   #endif

         // Reset the global counter of how many defines had been created
         defines_count = shader_defines_data.size();
      }

      static void Load(std::vector<ShaderDefineData>& shader_defines_data, const std::string& config_name, reshade::api::effect_runtime* runtime = nullptr)
      {
         char char_buffer[std::string_view("Define99Value ").size()]; // Hardcoded max length (see "MAX_SHADER_DEFINES")
   #if DEVELOPMENT || TEST
         for (uint32_t i = 0; i < MAX_SHADER_DEFINES; i++)
   #else
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
   #endif
         {
            const bool is_new_define = i >= shader_defines_data.size();
            bool is_editable = is_new_define || shader_defines_data[i].IsNameEditable();
            bool should_load_value = true;
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            // If the current name (index) is "editable" (writable), load it directly
            if (is_editable)
            {
               if (is_new_define)
               {
                  shader_defines_data.emplace_back();
               }
               size_t size = std::size(shader_defines_data[i].editable_data.name); // SHADER_DEFINES_MAX_NAME_LENGTH
               reshade::get_config_value(runtime, config_name.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetName(), &size);
               // If this name was already in the list (from another index), or is empty, ignore it and revert to default
               if ((!shader_defines_data[i].IsCustom() && shader_defines_data[i].IsNameEmpty()) || ContainsName(shader_defines_data, shader_defines_data[i].editable_data.GetName(), i))
               {
                  shader_defines_data[i].Reset();
                  // If the defauled value was also already present (due to another define having loaded it), clear it completely
                  if (ContainsName(shader_defines_data, shader_defines_data[i].editable_data.GetName(), i))
                  {
                     shader_defines_data[i].Clear();
                  }
               }
            }
            // In this case, we should only load the define actual value if the define name serialized in the config matched with our default one
            else
            {
               char name[SHADER_DEFINES_MAX_NAME_LENGTH] = "";
               size_t size = std::size(name); // SHADER_DEFINES_MAX_NAME_LENGTH
               reshade::get_config_value(runtime, config_name.c_str(), &char_buffer[0], &name[0], &size);
               should_load_value = strcmp(shader_defines_data[i].editable_data.GetName(), &name[0]) == 0 && (&name[0] != ""); // See "ShaderDefineData::IsCustom()"
            }

            is_editable = is_new_define || shader_defines_data[i].IsValueEditable();
            if (is_editable && should_load_value)
            {
               assert(i < shader_defines_data.size());
               sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
               size_t size = std::size(shader_defines_data[i].editable_data.value); // SHADER_DEFINES_MAX_VALUE_LENGTH
               reshade::get_config_value(runtime, config_name.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetValue(), &size);
               if (!shader_defines_data[i].IsCustom() && shader_defines_data[i].IsValueEmpty())
               {
                  shader_defines_data[i].Reset();
               }
            }
         }

         // Clean up the ones that loaded up with empty values
         RemoveCustomData(shader_defines_data, true);
      }
      static void Save(const std::vector<ShaderDefineData>& shader_defines_data, const std::string& config_name, reshade::api::effect_runtime* runtime = nullptr)
      {
         char char_buffer[std::string_view("Define99Value ").size()]; // Hardcoded max length (see "MAX_SHADER_DEFINES")
         constexpr bool always_save_shader_defines = false;
         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            if (!shader_defines_data[i].IsDefault() || always_save_shader_defines)
            {
               reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/);
            }
            // Don't save default values, they would pollute the config file and cause issues with versioning.
            // If the shaders code change, we have other ways of detecting that we need to re-compile after launch, and if the addon code change, it would also automatically trigger a recompile.
            else
            {
               reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], (const char*)nullptr);
            }
            sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
            if (!shader_defines_data[i].IsDefault() || always_save_shader_defines)
            {
               reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/);
            }
            else
            {
               reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], (const char*)nullptr);
            }
         }
   #if DEVELOPMENT || TEST // These are never read or set outside of these configurations, so there's no need to clear them either for now
         // Clean the remaining non set defines if they were in the config (setting them to "null" will remove them)
         // Note: if we decrease "MAX_SHADER_DEFINES", this won't clear them all, in that case, we could run it until index 99.
         for (uint32_t i = shader_defines_data.size(); i < MAX_SHADER_DEFINES; i++)
         {
            sprintf(&char_buffer[0], i < 10 ? "Define#%iName" : "Define%iName", i);
            reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], (const char*)nullptr);
            sprintf(&char_buffer[0], i < 10 ? "Define#%iValue" : "Define%iValue", i);
            reshade::set_config_value(runtime, config_name.c_str(), &char_buffer[0], (const char*)nullptr);
         }
   #endif
      }
   };
}