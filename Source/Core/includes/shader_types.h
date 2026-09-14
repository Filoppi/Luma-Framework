#pragma once

#include <cstddef>
#include <cstdint>

#include "matrix.h"

namespace
{
   // Remove these attributes
   #define row_major
   #define column_major

   typedef uint32_t uint;


   struct int2
   {
      int32_t x;
      int32_t y;

      friend bool operator==(const int2& lhs, const int2& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y;
      }
      friend bool operator!=(const int2& lhs, const int2& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct uint2
   {
      uint x;
      uint y;

      friend bool operator==(const uint2& lhs, const uint2& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y;
      }
      friend bool operator!=(const uint2& lhs, const uint2& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct uint3
   {
      uint x;
      uint y;
      uint z;

      friend bool operator==(const uint3& lhs, const uint3& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
      }
      friend bool operator!=(const uint3& lhs, const uint3& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct uint4
   {
      uint x;
      uint y;
      uint z;
      uint w;

      friend bool operator==(const uint4& lhs, const uint4& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
      }
      friend bool operator!=(const uint4& lhs, const uint4& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct float2
   {
      float x;
      float y;

      friend bool operator==(const float2& lhs, const float2& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y;
      }
      friend bool operator!=(const float2& lhs, const float2& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct float3
   {
      float x;
      float y;
      float z;

      friend bool operator==(const float3& lhs, const float3& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
      }
      friend bool operator!=(const float3& lhs, const float3& rhs)
      {
         return !(lhs == rhs);
      }
   };

   struct float4
   {
      float x;
      float y;
      float z;
      float w;

      friend bool operator==(const float4& lhs, const float4& rhs)
      {
         return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
      }
      friend bool operator!=(const float4& lhs, const float4& rhs)
      {
         return !(lhs == rhs);
      }
   };

   typedef Math::Matrix44F float4x4;
   static_assert(sizeof(Math::Matrix44F) == sizeof(float4) * 4);
}

// Hashes for the types above, so they can be used as keys in unordered containers.
// These types have internal linkage (anonymous namespace), so each translation unit specializes for its own distinct type.
namespace std
{
   template <>
   struct hash<uint2>
   {
      size_t operator()(const uint2& v) const noexcept
      {
         uint64_t h = 14695981039346656037ULL;
         h = (h ^ v.x) * 1099511628211ULL;
         h = (h ^ v.y) * 1099511628211ULL;
         return (size_t)h;
      }
   };

   template <>
   struct hash<uint3>
   {
      size_t operator()(const uint3& v) const noexcept
      {
         uint64_t h = 14695981039346656037ULL;
         h = (h ^ v.x) * 1099511628211ULL;
         h = (h ^ v.y) * 1099511628211ULL;
         h = (h ^ v.z) * 1099511628211ULL;
         return (size_t)h;
      }
   };

   template <>
   struct hash<uint4>
   {
      size_t operator()(const uint4& v) const noexcept
      {
         uint64_t h = 14695981039346656037ULL;
         h = (h ^ v.x) * 1099511628211ULL;
         h = (h ^ v.y) * 1099511628211ULL;
         h = (h ^ v.z) * 1099511628211ULL;
         h = (h ^ v.w) * 1099511628211ULL;
         return (size_t)h;
      }
   };
}
