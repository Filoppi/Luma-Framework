#pragma once

#include "dxgiformat.h"

#include <iomanip>
#include <ios>
#include <ostream>

#include <include/reshade.hpp>

#define PRINT_CRC32(crc32) "0x" << std::hex << std::setw(8) << std::setfill('0') << crc32 << std::setfill(' ') << std::dec

// ReShade specific
namespace
{
   inline std::ostream& operator<<(std::ostream& os, const reshade::api::device_api value)
   {
      switch (value)
      {
      case reshade::api::device_api::d3d9:   return os << "d3d9";
      case reshade::api::device_api::d3d10:  return os << "d3d10";
      case reshade::api::device_api::d3d11:  return os << "d3d11";
      case reshade::api::device_api::d3d12:  return os << "d3d12";
      case reshade::api::device_api::opengl: return os << "opengl";
      case reshade::api::device_api::vulkan: return os << "vulkan";
      default:                               return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::shader_stage value)
   {
      switch (value)
      {
      case reshade::api::shader_stage::vertex:          return os << "vertex";
      case reshade::api::shader_stage::hull:            return os << "hull";
      case reshade::api::shader_stage::domain:          return os << "domain";
      case reshade::api::shader_stage::geometry:        return os << "geometry";
      case reshade::api::shader_stage::pixel:           return os << "pixel";
      case reshade::api::shader_stage::compute:         return os << "compute";
      case reshade::api::shader_stage::amplification:   return os << "amplification";
      case reshade::api::shader_stage::mesh:            return os << "mesh";
      case reshade::api::shader_stage::raygen:          return os << "raygen";
      case reshade::api::shader_stage::any_hit:         return os << "any_hit";
      case reshade::api::shader_stage::closest_hit:     return os << "closest_hit";
      case reshade::api::shader_stage::miss:            return os << "miss";
      case reshade::api::shader_stage::intersection:    return os << "intersection";
      case reshade::api::shader_stage::callable:        return os << "callable";
      case reshade::api::shader_stage::all:             return os << "all";
      case reshade::api::shader_stage::all_graphics:    return os << "all_graphics";
      case reshade::api::shader_stage::all_ray_tracing: return os << "all_raytracing";
      default:                                          return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const boolean value)
   {
      return os << ((value != 0u) ? "true" : "false");
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::blend_factor value)
   {
      switch (value)
      {
      case reshade::api::blend_factor::zero:                     return os << "zero";
      case reshade::api::blend_factor::one:                      return os << "one";
      case reshade::api::blend_factor::source_color:             return os << "source_color";
      case reshade::api::blend_factor::one_minus_source_color:   return os << "one_minus_source_color";
      case reshade::api::blend_factor::dest_color:               return os << "dest_color";
      case reshade::api::blend_factor::one_minus_dest_color:     return os << "one_minus_dest_color";
      case reshade::api::blend_factor::source_alpha:             return os << "source_alpha";
      case reshade::api::blend_factor::one_minus_source_alpha:   return os << "one_minus_source_alpha";
      case reshade::api::blend_factor::dest_alpha:               return os << "dest_alpha";
      case reshade::api::blend_factor::one_minus_dest_alpha:     return os << "one_minus_dest_alpha";
      case reshade::api::blend_factor::constant_color:           return os << "constant_color";
      case reshade::api::blend_factor::one_minus_constant_color: return os << "one_minus_constant_color";
      case reshade::api::blend_factor::constant_alpha:           return os << "constant_alpha";
      case reshade::api::blend_factor::one_minus_constant_alpha: return os << "one_minus_constant_alpha";
      case reshade::api::blend_factor::source_alpha_saturate:    return os << "source_alpha_saturate";
      case reshade::api::blend_factor::source1_color:            return os << "source1_color";
      case reshade::api::blend_factor::one_minus_source1_color:  return os << "one_minus_source1_color";
      case reshade::api::blend_factor::source1_alpha:            return os << "source1_alpha";
      case reshade::api::blend_factor::one_minus_source1_alpha:  return os << "one_minus_source1_alpha";
      default:                                                   return os << "unknown";
      };
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::blend_op value)
   {
      switch (value)
      {
      case reshade::api::blend_op::add:              return os << "add";
      case reshade::api::blend_op::subtract:         return os << "subtract";
      case reshade::api::blend_op::reverse_subtract: return os << "reverse_subtract";
      case reshade::api::blend_op::min:              return os << "min";
      case reshade::api::blend_op::max:              return os << "max";
      default:                                       return os << "unknown";
      };
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::pipeline_stage value)
   {
      switch (value)
      {
      case reshade::api::pipeline_stage::vertex_shader:        return os << "vertex_shader";
      case reshade::api::pipeline_stage::hull_shader:          return os << "hull_shader";
      case reshade::api::pipeline_stage::domain_shader:        return os << "domain_shader";
      case reshade::api::pipeline_stage::geometry_shader:      return os << "geometry_shader";
      case reshade::api::pipeline_stage::pixel_shader:         return os << "pixel_shader";
      case reshade::api::pipeline_stage::compute_shader:       return os << "compute_shader";
      case reshade::api::pipeline_stage::amplification_shader: return os << "amplification_shader";
      case reshade::api::pipeline_stage::mesh_shader:          return os << "mesh_shader";
      case reshade::api::pipeline_stage::input_assembler:      return os << "input_assembler";
      case reshade::api::pipeline_stage::stream_output:        return os << "stream_output";
      case reshade::api::pipeline_stage::rasterizer:           return os << "rasterizer";
      case reshade::api::pipeline_stage::depth_stencil:        return os << "depth_stencil";
      case reshade::api::pipeline_stage::output_merger:        return os << "output_merger";
      case reshade::api::pipeline_stage::all:                  return os << "all";
      case reshade::api::pipeline_stage::all_graphics:         return os << "all_graphics";
      case reshade::api::pipeline_stage::all_ray_tracing:      return os << "all_ray_tracing";
      case reshade::api::pipeline_stage::all_shader_stages:    return os << "all_shader_stages";
      default:                                                 return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::descriptor_type value)
   {
      switch (value)
      {
      case reshade::api::descriptor_type::sampler:                      return os << "sampler";
      case reshade::api::descriptor_type::sampler_with_resource_view:   return os << "sampler_with_resource_view";
      case reshade::api::descriptor_type::shader_resource_view:         return os << "shader_resource_view";
      case reshade::api::descriptor_type::unordered_access_view:        return os << "unordered_access_view";
      case reshade::api::descriptor_type::buffer_shader_resource_view:  return os << "buffer_shader_resource_view";
      case reshade::api::descriptor_type::buffer_unordered_access_view: return os << "buffer_unordered_access_view";
      case reshade::api::descriptor_type::constant_buffer:              return os << "constant_buffer";
      case reshade::api::descriptor_type::shader_storage_buffer:        return os << "shader_storage_buffer";
      case reshade::api::descriptor_type::acceleration_structure:       return os << "acceleration_structure";
      default:                                                          return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::dynamic_state value)
   {
      switch (value)
      {
      case reshade::api::dynamic_state::alpha_test_enable:               return os << "alpha_test_enable";
      case reshade::api::dynamic_state::alpha_reference_value:           return os << "alpha_reference_value";
      case reshade::api::dynamic_state::alpha_func:                      return os << "alpha_func";
      case reshade::api::dynamic_state::srgb_write_enable:               return os << "srgb_write_enable";
      case reshade::api::dynamic_state::primitive_topology:              return os << "primitive_topology";
      case reshade::api::dynamic_state::sample_mask:                     return os << "sample_mask";
      case reshade::api::dynamic_state::alpha_to_coverage_enable:        return os << "alpha_to_coverage_enable";
      case reshade::api::dynamic_state::blend_enable:                    return os << "blend_enable";
      case reshade::api::dynamic_state::logic_op_enable:                 return os << "logic_op_enable";
      case reshade::api::dynamic_state::color_blend_op:                  return os << "color_blend_op";
      case reshade::api::dynamic_state::source_color_blend_factor:       return os << "src_color_blend_factor";
      case reshade::api::dynamic_state::dest_color_blend_factor:         return os << "dst_color_blend_factor";
      case reshade::api::dynamic_state::alpha_blend_op:                  return os << "alpha_blend_op";
      case reshade::api::dynamic_state::source_alpha_blend_factor:       return os << "src_alpha_blend_factor";
      case reshade::api::dynamic_state::dest_alpha_blend_factor:         return os << "dst_alpha_blend_factor";
      case reshade::api::dynamic_state::logic_op:                        return os << "logic_op";
      case reshade::api::dynamic_state::blend_constant:                  return os << "blend_constant";
      case reshade::api::dynamic_state::render_target_write_mask:        return os << "render_target_write_mask";
      case reshade::api::dynamic_state::fill_mode:                       return os << "fill_mode";
      case reshade::api::dynamic_state::cull_mode:                       return os << "cull_mode";
      case reshade::api::dynamic_state::front_counter_clockwise:         return os << "front_counter_clockwise";
      case reshade::api::dynamic_state::depth_bias:                      return os << "depth_bias";
      case reshade::api::dynamic_state::depth_bias_clamp:                return os << "depth_bias_clamp";
      case reshade::api::dynamic_state::depth_bias_slope_scaled:         return os << "depth_bias_slope_scaled";
      case reshade::api::dynamic_state::depth_clip_enable:               return os << "depth_clip_enable";
      case reshade::api::dynamic_state::scissor_enable:                  return os << "scissor_enable";
      case reshade::api::dynamic_state::multisample_enable:              return os << "multisample_enable";
      case reshade::api::dynamic_state::antialiased_line_enable:         return os << "antialiased_line_enable";
      case reshade::api::dynamic_state::depth_enable:                    return os << "depth_enable";
      case reshade::api::dynamic_state::depth_write_mask:                return os << "depth_write_mask";
      case reshade::api::dynamic_state::depth_func:                      return os << "depth_func";
      case reshade::api::dynamic_state::stencil_enable:                  return os << "stencil_enable";
      case reshade::api::dynamic_state::front_stencil_read_mask:         return os << "front_stencil_read_mask";
      case reshade::api::dynamic_state::front_stencil_write_mask:        return os << "front_stencil_write_mask";
      case reshade::api::dynamic_state::front_stencil_reference_value:   return os << "front_stencil_reference_value";
      case reshade::api::dynamic_state::front_stencil_func:              return os << "front_stencil_func";
      case reshade::api::dynamic_state::front_stencil_pass_op:           return os << "front_stencil_pass_op";
      case reshade::api::dynamic_state::front_stencil_fail_op:           return os << "front_stencil_fail_op";
      case reshade::api::dynamic_state::front_stencil_depth_fail_op:     return os << "front_stencil_depth_fail_op";
      case reshade::api::dynamic_state::back_stencil_read_mask:          return os << "back_stencil_read_mask";
      case reshade::api::dynamic_state::back_stencil_write_mask:         return os << "back_stencil_write_mask";
      case reshade::api::dynamic_state::back_stencil_reference_value:    return os << "back_stencil_reference_value";
      case reshade::api::dynamic_state::back_stencil_func:               return os << "back_stencil_func";
      case reshade::api::dynamic_state::back_stencil_pass_op:            return os << "back_stencil_pass_op";
      case reshade::api::dynamic_state::back_stencil_fail_op:            return os << "back_stencil_fail_op";
      case reshade::api::dynamic_state::back_stencil_depth_fail_op:      return os << "back_stencil_depth_fail_op";
      case reshade::api::dynamic_state::ray_tracing_pipeline_stack_size: return os << "ray_tracing_pipeline_stack_size";
      case reshade::api::dynamic_state::unknown:
      default:                                                           return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::resource_usage value)
   {
      switch (value)
      {
      case reshade::api::resource_usage::index_buffer:              return os << "index_buffer";
      case reshade::api::resource_usage::vertex_buffer:             return os << "vertex_buffer";
      case reshade::api::resource_usage::constant_buffer:           return os << "constant_buffer";
      case reshade::api::resource_usage::stream_output:             return os << "stream_output";
      case reshade::api::resource_usage::indirect_argument:         return os << "indirect_argument";
      case reshade::api::resource_usage::depth_stencil:
      case reshade::api::resource_usage::depth_stencil_read:
      case reshade::api::resource_usage::depth_stencil_write:       return os << "depth_stencil";
      case reshade::api::resource_usage::render_target:             return os << "render_target";
      case reshade::api::resource_usage::shader_resource:
      case reshade::api::resource_usage::shader_resource_pixel:
      case reshade::api::resource_usage::shader_resource_non_pixel: return os << "shader_resource";
      case reshade::api::resource_usage::unordered_access:          return os << "unordered_access";
      case reshade::api::resource_usage::copy_dest:                 return os << "copy_dest";
      case reshade::api::resource_usage::copy_source:               return os << "copy_source";
      case reshade::api::resource_usage::resolve_dest:              return os << "resolve_dest";
      case reshade::api::resource_usage::resolve_source:            return os << "resolve_source";
      case reshade::api::resource_usage::acceleration_structure:    return os << "acceleration_structure";
      case reshade::api::resource_usage::general:                   return os << "general";
      case reshade::api::resource_usage::present:                   return os << "present";
      case reshade::api::resource_usage::cpu_access:                return os << "cpu_access";
      case reshade::api::resource_usage::undefined:
      default:                                                      return os << "undefined";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::format value)
   {
      switch (value)
      {
      case reshade::api::format::r1_unorm:              return os << "R1 UNORM";
      case reshade::api::format::l8_unorm:              return os << "L8 UNORM";
      case reshade::api::format::a8_unorm:              return os << "A8 UNORM";
      case reshade::api::format::r8_typeless:           return os << "R8 TYPELESS";
      case reshade::api::format::r8_uint:               return os << "R8 UINT";
      case reshade::api::format::r8_sint:               return os << "R8 SINT";
      case reshade::api::format::r8_unorm:              return os << "R8 UNORM";
      case reshade::api::format::r8_snorm:              return os << "R8 SNORM";
      case reshade::api::format::l8a8_unorm:            return os << "L8A8 UNORM";
      case reshade::api::format::r8g8_typeless:         return os << "R8G8 TYPELESS";
      case reshade::api::format::r8g8_uint:             return os << "R8G8 UINT";
      case reshade::api::format::r8g8_sint:             return os << "R8G8 SINT";
      case reshade::api::format::r8g8_unorm:            return os << "R8G8 UNORM";
      case reshade::api::format::r8g8_snorm:            return os << "R8G8 SNORM";
      case reshade::api::format::r8g8b8a8_typeless:     return os << "R8G8B8A8 TYPELESS";
      case reshade::api::format::r8g8b8a8_uint:         return os << "R8G8B8A8 UINT";
      case reshade::api::format::r8g8b8a8_sint:         return os << "R8G8B8A8 SINT";
      case reshade::api::format::r8g8b8a8_unorm:        return os << "R8G8B8A8 UNORM";
      case reshade::api::format::r8g8b8a8_unorm_srgb:   return os << "R8G8B8A8 UNORM SRGB";
      case reshade::api::format::r8g8b8a8_snorm:        return os << "R8G8B8A8 SNORM";
      case reshade::api::format::r8g8b8x8_unorm:        return os << "R8G8B8X8 UNORM";
      case reshade::api::format::r8g8b8x8_unorm_srgb:   return os << "R8G8B8X8 UNORM SRGB";
      case reshade::api::format::b8g8r8a8_typeless:     return os << "B8G8R8A8 TYPELESS";
      case reshade::api::format::b8g8r8a8_unorm:        return os << "B8G8R8A8 UNORM";
      case reshade::api::format::b8g8r8a8_unorm_srgb:   return os << "B8G8R8A8 UNORM SRGB";
      case reshade::api::format::b8g8r8x8_typeless:     return os << "B8G8R8X8 TYPELESS";
      case reshade::api::format::b8g8r8x8_unorm:        return os << "B8G8R8X8 UNORM";
      case reshade::api::format::b8g8r8x8_unorm_srgb:   return os << "B8G8R8X8 UNORM SRGB";
      case reshade::api::format::r10g10b10a2_typeless:  return os << "R10G10B10A2 TYPELESS";
      case reshade::api::format::r10g10b10a2_uint:      return os << "R10G10B10A2 UINT";
      case reshade::api::format::r10g10b10a2_unorm:     return os << "R10G10B10A2 UNORM";
      case reshade::api::format::r10g10b10a2_xr_bias:   return os << "R10G10B10A2 XR BIAS";
      case reshade::api::format::b10g10r10a2_typeless:  return os << "B10G10R10A2 TYPELESS";
      case reshade::api::format::b10g10r10a2_uint:      return os << "B10G10R10A2 UINT";
      case reshade::api::format::b10g10r10a2_unorm:     return os << "B10G10R10A2 UNORM";
      case reshade::api::format::l16_unorm:             return os << "L16 UNORM";
      case reshade::api::format::r16_typeless:          return os << "R16 TYPELESS";
      case reshade::api::format::r16_uint:              return os << "R16 UINT";
      case reshade::api::format::r16_sint:              return os << "R16 SINT";
      case reshade::api::format::r16_unorm:             return os << "R16 UNORM";
      case reshade::api::format::r16_snorm:             return os << "R16 SNORM";
      case reshade::api::format::r16_float:             return os << "R16 FLOAT";
      case reshade::api::format::l16a16_unorm:          return os << "L16A16 UNORM";
      case reshade::api::format::r16g16_typeless:       return os << "R16G16 TYPELESS";
      case reshade::api::format::r16g16_uint:           return os << "R16G16 UINT";
      case reshade::api::format::r16g16_sint:           return os << "R16G16 SINT";
      case reshade::api::format::r16g16_unorm:          return os << "R16G16 UNORM";
      case reshade::api::format::r16g16_snorm:          return os << "R16G16 SNORM";
      case reshade::api::format::r16g16_float:          return os << "R16G16 FLOAT";
      case reshade::api::format::r16g16b16a16_typeless: return os << "R16G16B16A16 TYPELESS";
      case reshade::api::format::r16g16b16a16_uint:     return os << "R16G16B16A16 UINT";
      case reshade::api::format::r16g16b16a16_sint:     return os << "R16G16B16A16 SINT";
      case reshade::api::format::r16g16b16a16_unorm:    return os << "R16G16B16A16 UNORM";
      case reshade::api::format::r16g16b16a16_snorm:    return os << "R16G16B16A16 SNORM";
      case reshade::api::format::r16g16b16a16_float:    return os << "R16G16B16A16 FLOAT";
      case reshade::api::format::r32_typeless:          return os << "R32 TYPELESS";
      case reshade::api::format::r32_uint:              return os << "R32 UINT";
      case reshade::api::format::r32_sint:              return os << "R32 SINT";
      case reshade::api::format::r32_float:             return os << "R32 FLOAT";
      case reshade::api::format::r32g32_typeless:       return os << "R32G32 TYPELESS";
      case reshade::api::format::r32g32_uint:           return os << "R32G32 UINT";
      case reshade::api::format::r32g32_sint:           return os << "R32G32 SINT";
      case reshade::api::format::r32g32_float:          return os << "R32G32 FLOAT";
      case reshade::api::format::r32g32b32_typeless:    return os << "R32G32B32 TYPELESS";
      case reshade::api::format::r32g32b32_uint:        return os << "R32G32B32 UINT";
      case reshade::api::format::r32g32b32_sint:        return os << "R32G32B32 SINT";
      case reshade::api::format::r32g32b32_float:       return os << "R32G32B32 FLOAT";
      case reshade::api::format::r32g32b32a32_typeless: return os << "R32G32B32A32 TYPELESS";
      case reshade::api::format::r32g32b32a32_uint:     return os << "R32G32B32A32 UINT";
      case reshade::api::format::r32g32b32a32_sint:     return os << "R32G32B32A32 SINT";
      case reshade::api::format::r32g32b32a32_float:    return os << "R32G32B32A32 FLOAT";
      case reshade::api::format::r9g9b9e5:              return os << "R9G9B9E5";
      case reshade::api::format::r11g11b10_float:       return os << "R11G11B10 FLOAT";
      case reshade::api::format::b5g6r5_unorm:          return os << "B5G6R5 UNORM";
      case reshade::api::format::b5g5r5a1_unorm:        return os << "B5G5R5A1 UNORM";
      case reshade::api::format::b5g5r5x1_unorm:        return os << "B5G5R5X1 UNORM";
      case reshade::api::format::b4g4r4a4_unorm:        return os << "B4G4R4A4 UNORM";
      case reshade::api::format::a4b4g4r4_unorm:        return os << "A4B4G4R4 UNORM";
      case reshade::api::format::s8_uint:               return os << "S8 UINT";
      case reshade::api::format::d16_unorm:             return os << "D16 UNORM";
      case reshade::api::format::d16_unorm_s8_uint:     return os << "D16 UNORM S8 UINT";
      case reshade::api::format::d24_unorm_x8_uint:     return os << "D24 UNORM X8 UINT";
      case reshade::api::format::d24_unorm_s8_uint:     return os << "D24 UNORM S8 UINT";
      case reshade::api::format::d32_float:             return os << "D32 FLOAT";
      case reshade::api::format::d32_float_s8_uint:     return os << "D32 FLOAT S8 UINT";
      case reshade::api::format::r24_g8_typeless:       return os << "R24 G8 TYPELESS";
      case reshade::api::format::r24_unorm_x8_uint:     return os << "R24 UNORM X8 UINT";
      case reshade::api::format::x24_unorm_g8_uint:     return os << "X24 UNORM G8 UINT";
      case reshade::api::format::r32_g8_typeless:       return os << "R32 G8 TYPELESS";
      case reshade::api::format::r32_float_x8_uint:     return os << "R32 FLOAT X8 UINT";
      case reshade::api::format::x32_float_g8_uint:     return os << "X32 FLOAT G8 UINT";
      case reshade::api::format::bc1_typeless:          return os << "BC1 TYPELESS";
      case reshade::api::format::bc1_unorm:             return os << "BC1 UNORM";
      case reshade::api::format::bc1_unorm_srgb:        return os << "BC1 UNORM SRGB";
      case reshade::api::format::bc2_typeless:          return os << "BC2 TYPELESS";
      case reshade::api::format::bc2_unorm:             return os << "BC2 UNORM";
      case reshade::api::format::bc2_unorm_srgb:        return os << "BC2 UNORM SRGB";
      case reshade::api::format::bc3_typeless:          return os << "BC3 TYPELESS";
      case reshade::api::format::bc3_unorm:             return os << "BC3 UNORM";
      case reshade::api::format::bc3_unorm_srgb:        return os << "BC3 UNORM SRGB";
      case reshade::api::format::bc4_typeless:          return os << "BC4 TYPELESS";
      case reshade::api::format::bc4_unorm:             return os << "BC4 UNORM";
      case reshade::api::format::bc4_snorm:             return os << "BC4 SNORM";
      case reshade::api::format::bc5_typeless:          return os << "BC5 TYPELESS";
      case reshade::api::format::bc5_unorm:             return os << "BC5 UNORM";
      case reshade::api::format::bc5_snorm:             return os << "BC5 SNORM";
      case reshade::api::format::bc6h_typeless:         return os << "BC6H TYPELESS";
      case reshade::api::format::bc6h_ufloat:           return os << "BC6H UFLOAT";
      case reshade::api::format::bc6h_sfloat:           return os << "BC6H SFLOAT";
      case reshade::api::format::bc7_typeless:          return os << "BC7 TYPELESS";
      case reshade::api::format::bc7_unorm:             return os << "BC7 UNORM";
      case reshade::api::format::bc7_unorm_srgb:        return os << "BC7 UNORM SRGB";
      case reshade::api::format::r8g8_b8g8_unorm:       return os << "R8G8 B8G8 UNORM";
      case reshade::api::format::g8r8_g8b8_unorm:       return os << "G8R8 G8B8 UNORM";
      case reshade::api::format::intz:                  return os << "INTZ";
      case reshade::api::format::unknown:               return os << "UNKNOWN";
      default:                                          assert(false); return os << "UNKNOWN"; // We shouldn't get here unless we are missing a format
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::pipeline_layout_param_type value)
   {
      switch (value)
      {
      case reshade::api::pipeline_layout_param_type::push_constants:               return os << "push_constants";
      case reshade::api::pipeline_layout_param_type::descriptor_table:             return os << "descriptor_table";
      case reshade::api::pipeline_layout_param_type::push_descriptors:             return os << "push_descriptors";
      case reshade::api::pipeline_layout_param_type::push_descriptors_with_ranges: return os << "push_descriptors_with_ranges";
      default:                                                                     return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::color_space value)
   {
      switch (value)
      {
      case reshade::api::color_space::srgb_nonlinear:       return os << "srgb_nonlinear";
      case reshade::api::color_space::extended_srgb_linear: return os << "extended_srgb_linear";
      case reshade::api::color_space::hdr10_st2084:         return os << "hdr10_st2084";
      case reshade::api::color_space::hdr10_hlg:            return os << "hdr10_hlg";
      case reshade::api::color_space::unknown:
      default:                                              return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::resource_view_type value)
   {
      switch (value)
      {
      case reshade::api::resource_view_type::buffer:                       return os << "buffer";
      case reshade::api::resource_view_type::texture_1d:                   return os << "texture_1d";
      case reshade::api::resource_view_type::texture_1d_array:             return os << "texture_1d_array";
      case reshade::api::resource_view_type::texture_2d:                   return os << "texture_2d";
      case reshade::api::resource_view_type::texture_2d_array:             return os << "texture_2d_array";
      case reshade::api::resource_view_type::texture_2d_multisample:       return os << "texture_2d_multisample";
      case reshade::api::resource_view_type::texture_2d_multisample_array: return os << "texture_2d_multisample_array";
      case reshade::api::resource_view_type::texture_3d:                   return os << "texture_3d";
      case reshade::api::resource_view_type::texture_cube:                 return os << "texture_cube";
      case reshade::api::resource_view_type::texture_cube_array:           return os << "texture_cube_array";
      case reshade::api::resource_view_type::acceleration_structure:       return os << "acceleration_structure";
      case reshade::api::resource_view_type::unknown:
      default:                                                             return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::pipeline_subobject_type value)
   {
      switch (value)
      {
      case reshade::api::pipeline_subobject_type::vertex_shader:           return os << "vertex_shader";
      case reshade::api::pipeline_subobject_type::hull_shader:             return os << "hull_shader";
      case reshade::api::pipeline_subobject_type::domain_shader:           return os << "domain_shader";
      case reshade::api::pipeline_subobject_type::geometry_shader:         return os << "geometry_shader";
      case reshade::api::pipeline_subobject_type::pixel_shader:            return os << "pixel_shader";
      case reshade::api::pipeline_subobject_type::compute_shader:          return os << "compute_shader";
      case reshade::api::pipeline_subobject_type::input_layout:            return os << "input_layout";
      case reshade::api::pipeline_subobject_type::stream_output_state:     return os << "stream_output_state";
      case reshade::api::pipeline_subobject_type::blend_state:             return os << "blend_state";
      case reshade::api::pipeline_subobject_type::rasterizer_state:        return os << "rasterizer_state";
      case reshade::api::pipeline_subobject_type::depth_stencil_state:     return os << "depth_stencil_state";
      case reshade::api::pipeline_subobject_type::primitive_topology:      return os << "primitive_topology";
      case reshade::api::pipeline_subobject_type::depth_stencil_format:    return os << "depth_stencil_format";
      case reshade::api::pipeline_subobject_type::render_target_formats:   return os << "render_target_formats";
      case reshade::api::pipeline_subobject_type::sample_mask:             return os << "sample_mask";
      case reshade::api::pipeline_subobject_type::sample_count:            return os << "sample_count";
      case reshade::api::pipeline_subobject_type::viewport_count:          return os << "viewport_count";
      case reshade::api::pipeline_subobject_type::dynamic_pipeline_states: return os << "dynamic_pipeline_states";
      case reshade::api::pipeline_subobject_type::max_vertex_count:        return os << "max_vertex_count";
      case reshade::api::pipeline_subobject_type::amplification_shader:    return os << "amplification_shader";
      case reshade::api::pipeline_subobject_type::mesh_shader:             return os << "mesh_shader";
      case reshade::api::pipeline_subobject_type::raygen_shader:           return os << "raygen_shader";
      case reshade::api::pipeline_subobject_type::any_hit_shader:          return os << "any_hit_shader";
      case reshade::api::pipeline_subobject_type::closest_hit_shader:      return os << "closest_hit_shader";
      case reshade::api::pipeline_subobject_type::miss_shader:             return os << "miss_shader";
      case reshade::api::pipeline_subobject_type::intersection_shader:     return os << "intersection_shader";
      case reshade::api::pipeline_subobject_type::callable_shader:         return os << "callable_shader";
      case reshade::api::pipeline_subobject_type::libraries:               return os << "libraries";
      case reshade::api::pipeline_subobject_type::shader_groups:           return os << "shader_groups";
      case reshade::api::pipeline_subobject_type::max_payload_size:        return os << "max_payload_size";
      case reshade::api::pipeline_subobject_type::max_attribute_size:      return os << "max_attribute_size";
      case reshade::api::pipeline_subobject_type::max_recursion_depth:     return os << "max_recursion_depth";
      case reshade::api::pipeline_subobject_type::flags:                   return os << "flags";
      default:
      case reshade::api::pipeline_subobject_type::unknown:                 return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::indirect_command value)
   {
      switch (value)
      {
      case reshade::api::indirect_command::draw:          return os << "draw";
      case reshade::api::indirect_command::draw_indexed:  return os << "draw_indexed";
      case reshade::api::indirect_command::dispatch:      return os << "dispatch";
      case reshade::api::indirect_command::dispatch_mesh: return os << "dispatch_mesh";
      case reshade::api::indirect_command::dispatch_rays: return os << "dispatch_rays";
      default:
      case reshade::api::indirect_command::unknown:       return os << "unknown";
      }
   }

   inline std::ostream& operator<<(std::ostream& os, const reshade::api::resource_type value)
   {
      switch (value)
      {
      case reshade::api::resource_type::buffer:     return os << "buffer";
      case reshade::api::resource_type::texture_1d: return os << "texture_1d";
      case reshade::api::resource_type::texture_2d: return os << "texture_2d";
      case reshade::api::resource_type::texture_3d: return os << "texture_3d";
      case reshade::api::resource_type::surface:    return os << "surface";
      default:
      case reshade::api::resource_type::unknown:    return os << "unknown";
      }
   }
}

// TODO: move these utilities to texture.hpp or resource.hpp or something, together with mip utilities and other stuff
// DirectX specific
namespace
{
   // All float, sRGB and depth formats are returned as linear here.
   // Pass in the view format if possible.
   bool IsLinearFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_D16_UNORM:
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: // Not a depth format directly but realistically only used for depth
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: // Not a depth format directly but realistically only used for depth
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return true;
      }
      return false;
   }

   // Returns true if a format has RGB channels, and optional A too (in any order).
   // Can be used to determine if a texture is meant to be storing a RGB color or not.
   bool IsRGBAFormat(DXGI_FORMAT format, bool ignore_alpha = false)
   {
      switch (format)
      {
      case DXGI_FORMAT_B4G4R4A4_UNORM:
      case DXGI_FORMAT_A4B4G4R4_UNORM:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return true;

      // Alpha not present or not used:
      case DXGI_FORMAT_B5G6R5_UNORM:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return ignore_alpha;

      // Limited alpha cases:
      case DXGI_FORMAT_B5G5R5A1_UNORM:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
         return true;
      }
      return false;
   }

   // If true, the format is "HDR" and can go beyond 1. Depth formats are ignored.
   bool IsFloatFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return true;
      }
      return false;
   }

   // Depth formats are ignored.
   bool IsSignedFloatFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
         return true;
      }
      return false;
   }
   
   bool IsIntFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
         return true;
      // Partial int (stencil) depth formats
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
      case DXGI_FORMAT_D24_UNORM_S8_UINT:
         return false;
      }
      return false;
   }

   bool IsTypelessFormat(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G8X24_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC4_TYPELESS:
      case DXGI_FORMAT_BC5_TYPELESS:
      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_BC7_TYPELESS:
         return true;
      // Partially typeless formats aren't really typeless, they are just allowing views to access part of a resource while ignoring another part, so ignore them
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
         return false;
      }
      return false;
   }

   DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format, DXGI_FORMAT fallback_format = DXGI_FORMAT_UNKNOWN)
   {
      switch (format)
      {
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R16G16B16A16_SNORM:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
         return DXGI_FORMAT_R16G16B16A16_TYPELESS;
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
         return DXGI_FORMAT_R8G8B8A8_TYPELESS;
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
         return DXGI_FORMAT_B8G8R8A8_TYPELESS;
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
         return DXGI_FORMAT_B8G8R8X8_TYPELESS;
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
         return DXGI_FORMAT_R32G32B32A32_TYPELESS;
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
         return DXGI_FORMAT_R32G32B32_TYPELESS;
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
         return DXGI_FORMAT_R32G32_TYPELESS;
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R16G16_SNORM:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
         return DXGI_FORMAT_R16G16_TYPELESS;
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R8G8_SNORM:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
         return DXGI_FORMAT_R8G8_TYPELESS;
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_D32_FLOAT:
         return DXGI_FORMAT_R32_TYPELESS;
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R16_SNORM:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_D16_UNORM:
         return DXGI_FORMAT_R16_TYPELESS;
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_R8_SNORM:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
         return DXGI_FORMAT_R8_TYPELESS;
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
         return DXGI_FORMAT_R10G10B10A2_TYPELESS;

      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
         return DXGI_FORMAT_BC1_TYPELESS;
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
         return DXGI_FORMAT_BC2_TYPELESS;
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
         return DXGI_FORMAT_BC3_TYPELESS;
      case DXGI_FORMAT_BC4_TYPELESS:
      case DXGI_FORMAT_BC4_UNORM:
      case DXGI_FORMAT_BC4_SNORM:
         return DXGI_FORMAT_BC4_TYPELESS;
      case DXGI_FORMAT_BC5_TYPELESS:
      case DXGI_FORMAT_BC5_UNORM:
      case DXGI_FORMAT_BC5_SNORM:
         return DXGI_FORMAT_BC5_TYPELESS;
      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_BC6H_UF16:
      case DXGI_FORMAT_BC6H_SF16:
         return DXGI_FORMAT_BC6H_TYPELESS;
      case DXGI_FORMAT_BC7_TYPELESS:
      case DXGI_FORMAT_BC7_UNORM:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return DXGI_FORMAT_BC7_TYPELESS;

      case DXGI_FORMAT_R1_UNORM:
      case DXGI_FORMAT_A8_UNORM: // There's no official typeless format for this, though we could re-use "DXGI_FORMAT_R8_TYPELESS" if necessary
      case DXGI_FORMAT_R8G8_B8G8_UNORM:
      case DXGI_FORMAT_G8R8_G8B8_UNORM:
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R11G11B10_FLOAT:
         return fallback_format; // These formats don't have a TYPELESS version (usually because they are encoded or unique), so return an invalid format to highlight that. There's a few more not included here.
      }
      return fallback_format;
   }

   enum class FormatEncodingType
   {
      TYPELESS,
      UNORM,
      UNORM_SRGB,
      SNORM,
      UINT,
      SINT,
      UFLOAT,
      SFLOAT,
      // Includes unknown format
      OTHER
   };

   FormatEncodingType GetFormatEncodingType(DXGI_FORMAT format)
   {
      switch (format)
      {
      // Typeless
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G8X24_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R24G8_TYPELESS:
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_BC1_TYPELESS:
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC4_TYPELESS:
      case DXGI_FORMAT_BC5_TYPELESS:
      case DXGI_FORMAT_BC6H_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_BC7_TYPELESS:
         return FormatEncodingType::TYPELESS;

      // UNORM
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_A8_UNORM:
      case DXGI_FORMAT_R1_UNORM:
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC4_UNORM:
      case DXGI_FORMAT_BC5_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_BC7_UNORM:
      case DXGI_FORMAT_D16_UNORM:
         return FormatEncodingType::UNORM;

      // UNORM SRGB
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_BC1_UNORM_SRGB:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      case DXGI_FORMAT_BC7_UNORM_SRGB:
         return FormatEncodingType::UNORM_SRGB;

      // SNORM
      case DXGI_FORMAT_R16G16B16A16_SNORM:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R16G16_SNORM:
      case DXGI_FORMAT_R8G8_SNORM:
      case DXGI_FORMAT_R16_SNORM:
      case DXGI_FORMAT_R8_SNORM:
      case DXGI_FORMAT_BC4_SNORM:
      case DXGI_FORMAT_BC5_SNORM:
         return FormatEncodingType::SNORM;

      // UINT
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R8_UINT:
         return FormatEncodingType::UINT;

      // SINT
      case DXGI_FORMAT_R32G32B32A32_SINT:
      case DXGI_FORMAT_R32G32B32_SINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
      case DXGI_FORMAT_R32G32_SINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_R16G16_SINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_R8G8_SINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_R8_SINT:
         return FormatEncodingType::SINT;

      // UFLOAT
      case DXGI_FORMAT_R11G11B10_FLOAT:
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_BC6H_UF16:
         return FormatEncodingType::UFLOAT;

      // SFLOAT
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_D32_FLOAT:
      case DXGI_FORMAT_BC6H_SF16:
         return FormatEncodingType::SFLOAT;

      default:
         return FormatEncodingType::OTHER;
      }
   }

   // Only works on typeless formats! Returns the format unchanged otherwise.
   // Not meant to be used for depth formats.
   DXGI_FORMAT GetTypedFormat(DXGI_FORMAT typeless_format, FormatEncodingType format_encoding_type, bool return_unknown_on_failure = false)
   {
      if (format_encoding_type == FormatEncodingType::TYPELESS)
         return typeless_format; // Assuming this is already typeless!

      // TODO: would it be faster to do these with a map?
      switch (typeless_format)
      {
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R32G32B32A32_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R32G32B32A32_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R32G32B32_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R32G32B32_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R32G32B32_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R16G16B16A16_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R16G16B16A16_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R32G32_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R32G32_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R32G32_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R10G10B10A2_UINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
         case FormatEncodingType::SNORM:      return DXGI_FORMAT_R8G8B8A8_SNORM;
         case FormatEncodingType::UINT:       return DXGI_FORMAT_R8G8B8A8_UINT;
         case FormatEncodingType::SINT:       return DXGI_FORMAT_R8G8B8A8_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R16G16_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R16G16_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_R16G16_SNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R16G16_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R16G16_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R32_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R32_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R32_FLOAT;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R32_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R32_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R8G8_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R8G8_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_R8G8_SNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R8G8_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R8G8_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R16_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_R16_FLOAT;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_R16_FLOAT;
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R16_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_R16_SNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R16_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R16_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_R8_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM: return DXGI_FORMAT_R8_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_R8_SNORM;
         case FormatEncodingType::UINT:  return DXGI_FORMAT_R8_UINT;
         case FormatEncodingType::SINT:  return DXGI_FORMAT_R8_SINT;
         default: break;
         }
         break;

      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
         default: break;
         }
         break;

      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_B8G8R8X8_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
         default: break;
         }
         break;

      // BC formats
      case DXGI_FORMAT_BC1_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_BC1_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC2_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_BC2_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC3_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_BC3_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC4_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM: return DXGI_FORMAT_BC4_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_BC4_SNORM;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC5_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM: return DXGI_FORMAT_BC5_UNORM;
         case FormatEncodingType::SNORM: return DXGI_FORMAT_BC5_SNORM;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC6H_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UFLOAT: return DXGI_FORMAT_BC6H_UF16;
         case FormatEncodingType::SFLOAT: return DXGI_FORMAT_BC6H_SF16;
         default: break;
         }
         break;

      case DXGI_FORMAT_BC7_TYPELESS:
         switch (format_encoding_type)
         {
         case FormatEncodingType::UNORM:      return DXGI_FORMAT_BC7_UNORM;
         case FormatEncodingType::UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
         default: break;
         }
         break;

      default:
         {
            ASSERT_ONCE(typeless_format != DXGI_FORMAT_R32G8X24_TYPELESS && typeless_format != DXGI_FORMAT_R24G8_TYPELESS); // Depth format. We shouldn't have gotten here
         }
         break;
      }

      return return_unknown_on_failure ? DXGI_FORMAT_UNKNOWN : typeless_format;
   }

   // Works on any format. Useful to find the best view type for textures that were uprgaded.
   // Generally makes little sense to be used if the target format wasn't already typeless, and the source wasn't typeless.
   DXGI_FORMAT GetRemappedFormat(DXGI_FORMAT source_format, DXGI_FORMAT target_format)
   {
      FormatEncodingType format_encoding_type = GetFormatEncodingType(source_format);

      // Return itself if the typeless cast failed (some formats don't have a typeless version)
      target_format = GetTypelessFormat(target_format, target_format);

      // This will return the same format if it was typeless
      return GetTypedFormat(target_format, format_encoding_type);
   }

   // TODO: add a strict flag to avoid copying between e.g. UNORM and FLOAT
   bool AreFormatsCopyCompatible(DXGI_FORMAT format_a, DXGI_FORMAT format_b)
   {
      auto Is32Bit4ChannelTypeless = [](DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_B8G8R8X8_TYPELESS:
         case DXGI_FORMAT_B8G8R8A8_TYPELESS:
         case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return true;
         }
         return false;
      };

      format_a = GetTypelessFormat(format_a, format_a);
      format_b = GetTypelessFormat(format_b, format_b);
      if (format_a == format_b || (Is32Bit4ChannelTypeless(format_a) && Is32Bit4ChannelTypeless(format_b)))
         return true;
      return false;
   }

   // If channels have a different bit size, it returns the overall one.
   bool GetFormatSizeInfo(DXGI_FORMAT format, uint8_t& channels, uint8_t& bits_per_channel)
   {
      channels = 0;
      bits_per_channel = 0;

      switch (format)
      {
      // 16-bit/channel, 4-channel
      case DXGI_FORMAT_R16G16B16A16_TYPELESS:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R16G16B16A16_SNORM:
      case DXGI_FORMAT_R16G16B16A16_UINT:
      case DXGI_FORMAT_R16G16B16A16_SINT:
         channels = 4; bits_per_channel = 16; break;
      // 8-bit/channel, 4-channel
      case DXGI_FORMAT_R8G8B8A8_TYPELESS:
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_R8G8B8A8_SNORM:
      case DXGI_FORMAT_R8G8B8A8_UINT:
      case DXGI_FORMAT_R8G8B8A8_SINT:
      case DXGI_FORMAT_B8G8R8A8_TYPELESS:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
         channels = 4; bits_per_channel = 8; break;
      // 32-bit/channel, 4-channel
      case DXGI_FORMAT_R32G32B32A32_TYPELESS:
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_R32G32B32A32_UINT:
      case DXGI_FORMAT_R32G32B32A32_SINT:
         channels = 4; bits_per_channel = 32; break;
      // 32-bit/channel, 3-channel
      case DXGI_FORMAT_R32G32B32_TYPELESS:
      case DXGI_FORMAT_R32G32B32_FLOAT:
      case DXGI_FORMAT_R32G32B32_UINT:
      case DXGI_FORMAT_R32G32B32_SINT:
         channels = 3; bits_per_channel = 32; break;
      // 32-bit/channel, 2-channel
      case DXGI_FORMAT_R32G32_TYPELESS:
      case DXGI_FORMAT_R32G32_FLOAT:
      case DXGI_FORMAT_R32G32_UINT:
      case DXGI_FORMAT_R32G32_SINT:
         channels = 2; bits_per_channel = 32; break;
      // 16-bit/channel, 2-channel
      case DXGI_FORMAT_R16G16_TYPELESS:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R16G16_SNORM:
      case DXGI_FORMAT_R16G16_UINT:
      case DXGI_FORMAT_R16G16_SINT:
         channels = 2; bits_per_channel = 16; break;
      // 8-bit/channel, 2-channel
      case DXGI_FORMAT_R8G8_TYPELESS:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R8G8_SNORM:
      case DXGI_FORMAT_R8G8_UINT:
      case DXGI_FORMAT_R8G8_SINT:
         channels = 2; bits_per_channel = 8; break;
      // Single channel, 32-bit
      case DXGI_FORMAT_R32_TYPELESS:
      case DXGI_FORMAT_R32_FLOAT:
      case DXGI_FORMAT_R32_UINT:
      case DXGI_FORMAT_R32_SINT:
      case DXGI_FORMAT_D32_FLOAT:
         channels = 1; bits_per_channel = 32; break;
      // Single channel, 16-bit
      case DXGI_FORMAT_R16_TYPELESS:
      case DXGI_FORMAT_R16_FLOAT:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R16_SNORM:
      case DXGI_FORMAT_R16_UINT:
      case DXGI_FORMAT_R16_SINT:
      case DXGI_FORMAT_D16_UNORM:
         channels = 1; bits_per_channel = 16; break;
      // Single channel, 8-bit
      case DXGI_FORMAT_R8_TYPELESS:
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_R8_SNORM:
      case DXGI_FORMAT_R8_UINT:
      case DXGI_FORMAT_R8_SINT:
      case DXGI_FORMAT_A8_UNORM:
         channels = 1; bits_per_channel = 8; break;
      // 8-bit/channel, 4-channel - These are edge cases as the channels size isn't even
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
      case DXGI_FORMAT_R8G8_B8G8_UNORM:
      case DXGI_FORMAT_G8R8_G8B8_UNORM:
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_TYPELESS:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UINT:
      case DXGI_FORMAT_R11G11B10_FLOAT:
      // Single channel, 1-bit
      case DXGI_FORMAT_R1_UNORM:
         channels = 1; bits_per_channel = 1; break;

      default:
         ASSERT_ONCE_MSG(false, "Unsupported CloneTexture format"); // Please add support for more (formats with different sizes per channel might be hard)
         return false;
      }
      return true;
   }

   const char* GetFormatName(DXGI_FORMAT format)
   {
      switch (format)
      {
      case DXGI_FORMAT_UNKNOWN:                              return "Unknown";

      case DXGI_FORMAT_A8_UNORM:                             return "A8 UNORM";
      case DXGI_FORMAT_D32_FLOAT:                            return "D32 FLOAT";
      case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:                return "R24 UNORM X8 TYPELESS";
      case DXGI_FORMAT_D24_UNORM_S8_UINT:                    return "D24 UNORM S8 UINT";
      case DXGI_FORMAT_R24G8_TYPELESS:                       return "R24G8 TYPELESS";
      case DXGI_FORMAT_X24_TYPELESS_G8_UINT:                 return "X24 TYPELESS G8 UINT";
      case DXGI_FORMAT_R32G8X24_TYPELESS:                    return "R32G8X24 TYPELESS";
      case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:                 return "D32 FLOAT S8X24 UINT";
      case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:             return "R32 FLOAT X8X24 TYPELESS";
      case DXGI_FORMAT_R1_UNORM:                             return "R1 UNORM";
      case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:              return "X32 TYPELESS G8X24 UINT";
      case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:                   return "R9G9B9E5 SHAREDEXP";
      case DXGI_FORMAT_R8G8_B8G8_UNORM:                      return "R8G8 B8G8 UNORM";
      case DXGI_FORMAT_G8R8_G8B8_UNORM:                      return "G8R8 G8B8 UNORM";
      case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:           return "R10G10B10 XR BIAS A2 UNORM";

      case DXGI_FORMAT_R8_TYPELESS:                          return "R8 TYPELESS";
      case DXGI_FORMAT_R8_UNORM:                             return "R8 UNORM";
      case DXGI_FORMAT_R8_UINT:                              return "R8 UINT";
      case DXGI_FORMAT_R8_SNORM:                             return "R8 SNORM";
      case DXGI_FORMAT_R8_SINT:                              return "R8 SINT";

      case DXGI_FORMAT_R16_TYPELESS:                         return "R16 TYPELESS";
      case DXGI_FORMAT_R16_UNORM:                            return "R16 UNORM";
      case DXGI_FORMAT_R16_UINT:                             return "R16 UINT";
      case DXGI_FORMAT_R16_SNORM:                            return "R16 SNORM";
      case DXGI_FORMAT_R16_SINT:                             return "R16 SINT";
      case DXGI_FORMAT_R16_FLOAT:                            return "R16 FLOAT";
      case DXGI_FORMAT_D16_UNORM:                            return "D16 UNORM";

      case DXGI_FORMAT_R8G8_TYPELESS:                        return "R8G8 TYPELESS";
      case DXGI_FORMAT_R8G8_UNORM:                           return "R8G8 UNORM";
      case DXGI_FORMAT_R8G8_UINT:                            return "R8G8 UINT";
      case DXGI_FORMAT_R8G8_SNORM:                           return "R8G8 SNORM";
      case DXGI_FORMAT_R8G8_SINT:                            return "R8G8 SINT";

      case DXGI_FORMAT_R16G16_TYPELESS:                      return "R16G16 TYPELESS";
      case DXGI_FORMAT_R16G16_FLOAT:                         return "R16G16 FLOAT";
      case DXGI_FORMAT_R16G16_UNORM:                         return "R16G16 UNORM";
      case DXGI_FORMAT_R16G16_UINT:                          return "R16G16 UINT";
      case DXGI_FORMAT_R16G16_SNORM:                         return "R16G16 SNORM";
      case DXGI_FORMAT_R16G16_SINT:                          return "R16G16 SINT";

      case DXGI_FORMAT_R16G16B16A16_TYPELESS:                return "R16G16B16A16 TYPELESS";
      case DXGI_FORMAT_R16G16B16A16_FLOAT:                   return "R16G16B16A16 FLOAT";
      case DXGI_FORMAT_R16G16B16A16_UNORM:                   return "R16G16B16A16 UNORM";
      case DXGI_FORMAT_R16G16B16A16_UINT:                    return "R16G16B16A16 UINT";
      case DXGI_FORMAT_R16G16B16A16_SNORM:                   return "R16G16B16A16 SNORM";
      case DXGI_FORMAT_R16G16B16A16_SINT:                    return "R16G16B16A16 SINT";

      case DXGI_FORMAT_R32_TYPELESS:                         return "R32 TYPELESS";
      case DXGI_FORMAT_R32_FLOAT:                            return "R32 FLOAT";
      case DXGI_FORMAT_R32_UINT:                             return "R32 UINT";
      case DXGI_FORMAT_R32_SINT:                             return "R32 SINT";

      case DXGI_FORMAT_R32G32_TYPELESS:                      return "R32G32 TYPELESS";
      case DXGI_FORMAT_R32G32_FLOAT:                         return "R32G32 FLOAT";
      case DXGI_FORMAT_R32G32_UINT:                          return "R32G32 UINT";
      case DXGI_FORMAT_R32G32_SINT:                          return "R32G32 SINT";

      case DXGI_FORMAT_R32G32B32_TYPELESS:                   return "R32G32B32 TYPELESS";
      case DXGI_FORMAT_R32G32B32_FLOAT:                      return "R32G32B32 FLOAT";
      case DXGI_FORMAT_R32G32B32_UINT:                       return "R32G32B32 UINT";
      case DXGI_FORMAT_R32G32B32_SINT:                       return "R32G32B32 SINT";

      case DXGI_FORMAT_R32G32B32A32_TYPELESS:                return "R32G32B32A32 TYPELESS";
      case DXGI_FORMAT_R32G32B32A32_FLOAT:                   return "R32G32B32A32 FLOAT";
      case DXGI_FORMAT_R32G32B32A32_UINT:                    return "R32G32B32A32 UINT";
      case DXGI_FORMAT_R32G32B32A32_SINT:                    return "R32G32B32A32 SINT";

      case DXGI_FORMAT_R10G10B10A2_TYPELESS:                 return "R10G10B10A2 TYPELESS";
      case DXGI_FORMAT_R10G10B10A2_UNORM:                    return "R10G10B10A2 UNORM";
      case DXGI_FORMAT_R10G10B10A2_UINT:                     return "R10G10B10A2 UINT";
      case DXGI_FORMAT_R11G11B10_FLOAT:                      return "R11G11B10 FLOAT";

      case DXGI_FORMAT_R8G8B8A8_TYPELESS:                    return "R8G8B8A8 TYPELESS";
      case DXGI_FORMAT_R8G8B8A8_UNORM:                       return "R8G8B8A8 UNORM";
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:                  return "R8G8B8A8 UNORM SRGB";
      case DXGI_FORMAT_R8G8B8A8_UINT:                        return "R8G8B8A8 UINT";
      case DXGI_FORMAT_R8G8B8A8_SNORM:                       return "R8G8B8A8 SNORM";
      case DXGI_FORMAT_R8G8B8A8_SINT:                        return "R8G8B8A8 SINT";

      case DXGI_FORMAT_B5G6R5_UNORM:                         return "B5G6R5 UNORM";
      case DXGI_FORMAT_B5G5R5A1_UNORM:                       return "B5G5R5A1 UNORM";

      case DXGI_FORMAT_B8G8R8A8_TYPELESS:                    return "B8G8R8A8 TYPELESS";
      case DXGI_FORMAT_B8G8R8A8_UNORM:                       return "B8G8R8A8 UNORM";
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:                  return "B8G8R8A8 UNORM SRGB";
      case DXGI_FORMAT_B8G8R8X8_TYPELESS:                    return "B8G8R8X8 TYPELESS";
      case DXGI_FORMAT_B8G8R8X8_UNORM:                       return "B8G8R8X8 UNORM";
      case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:                  return "B8G8R8X8 UNORM SRGB";

      case DXGI_FORMAT_BC1_TYPELESS:                         return "BC1 TYPELESS";
      case DXGI_FORMAT_BC1_UNORM:                            return "BC1 UNORM";
      case DXGI_FORMAT_BC1_UNORM_SRGB:                       return "BC1 UNORM SRGB";
      case DXGI_FORMAT_BC2_TYPELESS:                         return "BC2 TYPELESS";
      case DXGI_FORMAT_BC2_UNORM:                            return "BC2 UNORM";
      case DXGI_FORMAT_BC2_UNORM_SRGB:                       return "BC2 UNORM SRGB";
      case DXGI_FORMAT_BC3_TYPELESS:                         return "BC3 TYPELESS";
      case DXGI_FORMAT_BC3_UNORM:                            return "BC3 UNORM";
      case DXGI_FORMAT_BC3_UNORM_SRGB:                       return "BC3 UNORM SRGB";
      case DXGI_FORMAT_BC4_TYPELESS:                         return "BC4 TYPELESS";
      case DXGI_FORMAT_BC4_UNORM:                            return "BC4 UNORM";
      case DXGI_FORMAT_BC4_SNORM:                            return "BC4 SNORM";
      case DXGI_FORMAT_BC5_TYPELESS:                         return "BC5 TYPELESS";
      case DXGI_FORMAT_BC5_UNORM:                            return "BC5 UNORM";
      case DXGI_FORMAT_BC5_SNORM:                            return "BC5 SNORM";
      case DXGI_FORMAT_BC6H_TYPELESS:                        return "BC6H TYPELESS";
      case DXGI_FORMAT_BC6H_UF16:                            return "BC6H UF16";
      case DXGI_FORMAT_BC6H_SF16:                            return "BC6H SF16";
      case DXGI_FORMAT_BC7_TYPELESS:                         return "BC7 TYPELESS";
      case DXGI_FORMAT_BC7_UNORM:                            return "BC7 UNORM";
      case DXGI_FORMAT_BC7_UNORM_SRGB:                       return "BC7 UNORM SRGB";

      case DXGI_FORMAT_AYUV:                                 return "AYUV";
      case DXGI_FORMAT_Y410:                                 return "Y410";
      case DXGI_FORMAT_Y416:                                 return "Y416";
      case DXGI_FORMAT_NV12:                                 return "NV12";
      case DXGI_FORMAT_P010:                                 return "P010";
      case DXGI_FORMAT_P016:                                 return "P016";
      case DXGI_FORMAT_420_OPAQUE:                           return "420 OPAQUE";
      case DXGI_FORMAT_YUY2:                                 return "YUY2";
      case DXGI_FORMAT_Y210:                                 return "Y210";
      case DXGI_FORMAT_Y216:                                 return "Y216";
      case DXGI_FORMAT_NV11:                                 return "NV11";
      case DXGI_FORMAT_AI44:                                 return "AI44";
      case DXGI_FORMAT_IA44:                                 return "IA44";
      case DXGI_FORMAT_P8:                                   return "P8";
      case DXGI_FORMAT_A8P8:                                 return "A8P8";
      case DXGI_FORMAT_P208:                                 return "P208";
      case DXGI_FORMAT_V208:                                 return "V208";
      case DXGI_FORMAT_V408:                                 return "V408";
      case DXGI_FORMAT_B4G4R4A4_UNORM:                       return "B4G4R4A4 UNORM";
      case DXGI_FORMAT_A4B4G4R4_UNORM:                       return "A4B4G4R4 UNORM";
      }
      return nullptr;
   }

   const char* GetFormatNameSafe(DXGI_FORMAT format)
   {
      const char* name = GetFormatName(format);
      return name ? name : "Unknown";
   }

   const char* GetBlendName(D3D11_BLEND blend, bool numerical = false)
   {
      switch (blend)
      {
      case D3D11_BLEND_ZERO:             return numerical ? "0" : "ZERO";
      case D3D11_BLEND_ONE:              return numerical ? "1" : "ONE";
      case D3D11_BLEND_SRC_COLOR:        return "SRC_COLOR";
      case D3D11_BLEND_INV_SRC_COLOR:    return "INV_SRC_COLOR";
      case D3D11_BLEND_SRC_ALPHA:        return "SRC_ALPHA";
      case D3D11_BLEND_INV_SRC_ALPHA:    return "INV_SRC_ALPHA";
      case D3D11_BLEND_DEST_ALPHA:       return "DEST_ALPHA";
      case D3D11_BLEND_INV_DEST_ALPHA:   return "INV_DEST_ALPHA";
      case D3D11_BLEND_DEST_COLOR:       return "DEST_COLOR";
      case D3D11_BLEND_INV_DEST_COLOR:   return "INV_DEST_COLOR";
      case D3D11_BLEND_SRC_ALPHA_SAT:    return "SRC_ALPHA_SAT";
      case D3D11_BLEND_BLEND_FACTOR:     return "BLEND_FACTOR";
      case D3D11_BLEND_INV_BLEND_FACTOR: return "INV_BLEND_FACTOR";
      case D3D11_BLEND_SRC1_COLOR:       return "SRC1_COLOR";
      case D3D11_BLEND_INV_SRC1_COLOR:   return "INV_SRC1_COLOR";
      case D3D11_BLEND_SRC1_ALPHA:       return "SRC1_ALPHA";
      case D3D11_BLEND_INV_SRC1_ALPHA:   return "INV_SRC1_ALPHA";
      default:                           return "UNKNOWN";
      }
   }

   const char* GetBlendOpName(D3D11_BLEND_OP blend_op)
   {
      switch (blend_op)
      {
      case D3D11_BLEND_OP_ADD:          return "ADD";
      case D3D11_BLEND_OP_SUBTRACT:     return "SUBTRACT";
      case D3D11_BLEND_OP_REV_SUBTRACT: return "REV_SUBTRACT";
      case D3D11_BLEND_OP_MIN:          return "MIN";
      case D3D11_BLEND_OP_MAX:          return "MAX";
      default:                          return "UNKNOWN";
      }
   }

   const char* GetBlendOpSigns(D3D11_BLEND_OP blend_op)
   {
      switch (blend_op)
      {
      case D3D11_BLEND_OP_ADD:          return "+";
      case D3D11_BLEND_OP_SUBTRACT:     return "-";
      case D3D11_BLEND_OP_REV_SUBTRACT: return "-";
      case D3D11_BLEND_OP_MIN:          return "min";
      case D3D11_BLEND_OP_MAX:          return "max";
      default:                          return "";
      }
   }

   const char* GetLogicOpName(D3D11_LOGIC_OP logicOp)
   {
      switch (logicOp)
      {
      case D3D11_LOGIC_OP_CLEAR:         return "CLEAR";
      case D3D11_LOGIC_OP_SET:           return "SET";
      case D3D11_LOGIC_OP_COPY:          return "COPY";
      case D3D11_LOGIC_OP_COPY_INVERTED: return "COPY_INVERTED";
      case D3D11_LOGIC_OP_NOOP:          return "NOOP";
      case D3D11_LOGIC_OP_INVERT:        return "INVERT";
      case D3D11_LOGIC_OP_AND:           return "AND";
      case D3D11_LOGIC_OP_NAND:          return "NAND";
      case D3D11_LOGIC_OP_OR:            return "OR";
      case D3D11_LOGIC_OP_NOR:           return "NOR";
      case D3D11_LOGIC_OP_XOR:           return "XOR";
      case D3D11_LOGIC_OP_EQUIV:         return "EQUIV";
      case D3D11_LOGIC_OP_AND_REVERSE:   return "AND_REVERSE";
      case D3D11_LOGIC_OP_AND_INVERTED:  return "AND_INVERTED";
      case D3D11_LOGIC_OP_OR_REVERSE:    return "OR_REVERSE";
      case D3D11_LOGIC_OP_OR_INVERTED:   return "OR_INVERTED";
      default:                           return "UNKNOWN";
      }
   }

   const char* GetFilterName(D3D11_FILTER filter)
   {
      switch (filter)
      {
      case D3D11_FILTER_MIN_MAG_MIP_POINT:                 return "MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR:          return "MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:    return "MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR:          return "MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:          return "MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:   return "MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT:          return "MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MIN_MAG_MIP_LINEAR:                return "MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_ANISOTROPIC:                       return "ANISOTROPIC";
      case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT:      return "CMP_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR: return "CMP_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT: return "CMP_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR: return "CMP_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT: return "CMP_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "CMP_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT: return "CMP_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR:    return "CMP_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_COMPARISON_ANISOTROPIC:           return "CMP_ANISOTROPIC";
      case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT:        return "MINIMUM_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_MAG_POINT_MIP_LINEAR: return "MINIMUM_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT: return "MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_POINT_MAG_MIP_LINEAR: return "MINIMUM_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_MIP_POINT: return "MINIMUM_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT: return "MINIMUM_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR:       return "MINIMUM_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_MINIMUM_ANISOTROPIC:              return "MINIMUM_ANISOTROPIC";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT:        return "MAXIMUM_MIN_MAG_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_POINT_MIP_LINEAR: return "MAXIMUM_MIN_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT: return "MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_MIP_LINEAR: return "MAXIMUM_MIN_POINT_MAG_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_MIP_POINT: return "MAXIMUM_MIN_LINEAR_MAG_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR: return "MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT: return "MAXIMUM_MIN_MAG_LINEAR_MIP_POINT";
      case D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_LINEAR:       return "MAXIMUM_MIN_MAG_MIP_LINEAR";
      case D3D11_FILTER_MAXIMUM_ANISOTROPIC:              return "MAXIMUM_ANISOTROPIC";
      default: return "UNKNOWN";
      }
   }

   const char* GetTextureAddressModeName(D3D11_TEXTURE_ADDRESS_MODE mode)
   {
      switch (mode)
      {
      case D3D11_TEXTURE_ADDRESS_WRAP:   return "WRAP";
      case D3D11_TEXTURE_ADDRESS_MIRROR: return "MIRROR";
      case D3D11_TEXTURE_ADDRESS_CLAMP:  return "CLAMP";
      case D3D11_TEXTURE_ADDRESS_BORDER: return "BORDER";
      case D3D11_TEXTURE_ADDRESS_MIRROR_ONCE: return "MIRROR_ONCE";
      default: return "UNKNOWN";
      }
   }
}