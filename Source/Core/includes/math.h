#pragma once

// Needs to be defined before anybody else includes "cmath"
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <cmath>
#include <array>
#include <limits>
#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <cctype>
#include "assert.h"

// Not everything in here is exactly math, there's also util and logic stuff, but it's close enough!
namespace Math
{
	std::array<unsigned int, 2> FindClosestIntegerResolutionForAspectRatio(double width, double height, double aspectRatio)
	{
		// We can't round both the x and y resolution as that might generate an aspect ratio
		// further away from the target one, we also can't either ceil or floor both sides,
		// so we find the combination or flooring and ceiling that is closest to the target ar.
		const unsigned int ceiledWidth = static_cast<unsigned int>(std::ceil(width));
		const unsigned int ceiledHeight = static_cast<unsigned int>(std::ceil(height));
		const unsigned int flooredWidth = static_cast<unsigned int>(std::floor(width));
		const unsigned int flooredHeight = static_cast<unsigned int>(std::floor(height));

		unsigned int intWidth = flooredWidth;
		unsigned int intHeight = flooredHeight;

		double minAspectRatioDistance = (std::numeric_limits<double>::max)(); // Wrap it around () because "max" might already be defined as macro
		for (const unsigned int newWidth : std::array<unsigned int, 2>{ ceiledWidth, flooredWidth })
		{
			for (const unsigned int newHeight : std::array<unsigned int, 2>{ ceiledHeight, flooredHeight })
			{
				const double newAspectRatio = static_cast<double>(newWidth) / newHeight;
				const double aspectRatioDistance = std::abs((newAspectRatio / aspectRatio) - 1.f);
				if (aspectRatioDistance < minAspectRatioDistance)
				{
					minAspectRatioDistance = aspectRatioDistance;
					intWidth = newWidth;
					intHeight = newHeight;
				}
			}
		}

		return std::array<unsigned int, 2>{ intWidth, intHeight };
	}

	template<typename T>
	bool AlmostEqual(T a, T b, T tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	// Emulates hlsl "asfloat(int x)"
	template<typename T>
	float AsFloat(T value)
	{
		static_assert(sizeof(T) <= sizeof(uint32_t) && sizeof(uint32_t) == sizeof(float));
		uint32_t value_uint32 = value;
		return *reinterpret_cast<float*>(&value_uint32);
	}

   // Emulates hlsl "asint(float x)"
   template<typename T>
   int32_t AsInt(T value)
   {
      static_assert(sizeof(T) <= sizeof(float) && sizeof(float) == sizeof(int32_t));
      float value_float = value;
      return *reinterpret_cast<int32_t*>(&value_float);
   }

   // Emulates hlsl "asuint(float x)"
   template<typename T>
   uint32_t AsUInt(T value)
   {
      static_assert(sizeof(T) <= sizeof(float) && sizeof(float) == sizeof(uint32_t));
      float value_float = value;
      return *reinterpret_cast<uint32_t*>(&value_float);
   }

   template<typename T>
   constexpr T SignOf(T val)
   {
      return (T(0) < val) - (val < T(0));
   }

   bool IsMemoryAllZero(const char* begin, std::size_t bytes)
   {
      return std::all_of(begin, begin + bytes, [](char byte) { return byte == 0; });
   }

   template <class Map>
   typename Map::mapped_type MapFindOrDefaultValue(const Map& m, const typename Map::key_type& k, const typename Map::mapped_type& default_value)
   {
      if (auto it = m.find(k); it != m.end())
         return it->second;
      return default_value;
   }

   static constexpr unsigned int crc_table[256] = {
       0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
       0xe963a535, 0x9e6495a3,    0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
       0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
       0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
       0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
       0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
       0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
       0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
       0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
       0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
       0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
       0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
       0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
       0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
       0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
       0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
       0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
       0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
       0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
       0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
       0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
       0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
       0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
       0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
       0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
       0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
       0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
       0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
       0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
       0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
       0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
       0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
       0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
       0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
       0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
       0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
       0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
       0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
       0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
       0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
       0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
       0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
       0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
   };

   constexpr uint32_t string_view_crc32(std::string_view str)
   {
#if 0 //TODOFT: delete
      if (!std::is_constant_evaluated())
         assert(false);
#endif
      uint32_t crc = 0xffffffff;
      for (char c : str)
         crc = (crc >> 8) ^ crc_table[(crc ^ c) & 0xff];
      return crc ^ 0xffffffff;
   }
   constexpr uint32_t char_ptr_crc32(const char* char_ptr)
   {
      uint32_t crc = 0xffffffff;
      size_t i = 0;
      while (char_ptr[i] != '\0')
      {
         crc = (crc >> 8) ^ crc_table[(crc ^ char_ptr[i]) & 0xff];
         i++;
      }
      return crc ^ 0xffffffff;
   }

   // "fnv1a_32" consteval hash (always evaluated at compile time for literals)
   consteval uint32_t CompileTimeStringHash(std::string_view s)
   {
      uint32_t hash = 0x811c9dc5;
      for (char c : s)
      {
         hash ^= static_cast<uint8_t>(c);
         hash *= 0x01000193;
      }
      return hash;
   }

   // User-defined literal for CompileTimeStringHash.
   // Usage: `"some_string"_h`, this is equivalent to `CompileTimeStringHash("some_string")`.
   consteval uint32_t operator"" _h(const char* str, size_t len)
   {
	  return CompileTimeStringHash(str);
   }

   uint32_t FindNextUniqueNumberInRange(uint32_t value, uint32_t min_value, uint32_t max_value, std::unordered_set<uint32_t> excluded_values)
   {
      assert(value >= min_value && value <= max_value);
      if( excluded_values.contains( value ) )
      {
         uint32_t mid_value = min_value + ((max_value - min_value) / 2);
         bool go_forward = value < mid_value; // Go in the direction with the most range
         assert(excluded_values.size() < mid_value); // Aproximate check, a solution isn't guaranteed
         do
         {
            value += go_forward ? 1 : -1;
         } while (excluded_values.contains(value));
      }
      return value;
   }

   void RemoveNonLetterOrNumberCharacters(char* str, char replacement_character = '\0')
   {
      if (!str) return; // Handle null pointers
      char* write = str; // Pointer to write the result
      char* read = str;  // Pointer to read the characters

      while (*read)
      {
         if (std::isalpha(static_cast<unsigned char>(*read)) || std::isdigit(static_cast<unsigned char>(*read)))
         {
            *write++ = *read; // Copy the character if it's a letter
         }
         else if (replacement_character != '\0')
         {
            *write++ = replacement_character;
         }
         read++;
      }
      *write = '\0'; // Null-terminate the string
   }

   // Inserts bit range from bit field
   template<typename T>
   T Bextract(T op, uint32_t first, uint32_t count)
   {
      if (!count)
         return 0;

      T mask = (T(2) << (count - 1)) - T(1);
      return (op >> first) & mask;
   }

   // From Vert- to Hor+ scaling
   float ScaleHorizontalFOV(float fov, bool true_rad_false_deg /*= false*/, float source_aspect_target /*= 16.f / 9.f*/, float target_aspect_target)
   {
      float scaling = true_rad_false_deg ? 1.f : (M_PI / 180.f);
      float fov_rad = fov * scaling;
      fov_rad = std::atan(std::tan(fov_rad / 2.f) * (target_aspect_target / source_aspect_target)) * 2.f;
      return fov_rad / scaling;
   }

   template <size_t N>
   std::vector<std::byte> MakeFloatsPatternS(const float (&v)[N])
   {
       std::vector<std::byte> out;
       out.resize(N * sizeof(float));

       for (size_t i = 0; i < N; ++i)
       {
           std::uint32_t bits = 0;
           std::memcpy(&bits, &v[i], sizeof(bits)); // Avoids strict-aliasing UB

           out[i * 4 + 0] = std::byte((bits >>  0) & 0xFFu);
           out[i * 4 + 1] = std::byte((bits >>  8) & 0xFFu);
           out[i * 4 + 2] = std::byte((bits >> 16) & 0xFFu);
           out[i * 4 + 3] = std::byte((bits >> 24) & 0xFFu);
       }

       return out;
   }

   template <size_t N>
   consteval auto MakeFloatsPattern(const std::array<float, N>& in)
      -> std::array<std::byte, N * sizeof(float)>
   {
       std::array<std::byte, N * sizeof(float)> out{};

       for (size_t i = 0; i < N; ++i)
       {
           auto b = std::bit_cast<std::array<std::byte, 4>>(in[i]);
           out[i * 4 + 0] = b[0];
           out[i * 4 + 1] = b[1];
           out[i * 4 + 2] = b[2];
           out[i * 4 + 3] = b[3];
       }

       return out;
   }
}