#pragma once

#include <bit>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "dxbc.h"

// Motion vectors for Borderlands GOTY Enhanced, which renders none, by patching whole DXBC containers (their signatures change).
// Vertex shaders run twice: the second run reads the previous frame's per-draw constants (and resources) from other slots, and only
// its SV_Position is kept, as an extra output. The first run's SV_Position is also copied to an extra output (outputs can't be read
// back), then jittered. Pixel shaders write the UV space delta from the current to the previous position to an extra target.
// The slots and registers come from a "Layout" ("MotionVectorPatches.h").
namespace MotionVectorPatch
{
   using namespace DXBC;

   constexpr char semantic_name[] = "LUMAMV";

   struct Layout
   {
      static constexpr uint32_t no_row = UINT32_MAX;

      // The per-draw buffer every projecting vertex shader declares: the jitter buffer's declaration goes after it. A shader without it
      // is refused.
      uint32_t object_slot;
      // Per-draw constant buffers, and the slots of the previous frame's copies the second run reads
      std::span<const std::pair<uint32_t, uint32_t>> previous_slots;
      // The jitter buffer. c0.xy: the upscaler's projection jitter in NDC, added to SV_Position after its unjittered copy, so motion
      // vectors never contain it.
      uint32_t jitter_slot = 9;
      // The second run reads t0 to t<resource_slots - 1> at t<previous_resources_slot> on
      uint32_t resource_slots = 16;
      uint32_t previous_resources_slot = 64;
      // Past every register the game uses, within the shader model's limit (32 on SM5, 16 vertex outputs on SM4)
      uint32_t current_position_register;
      uint32_t previous_position_register;
      // Past the game's targets
      uint32_t target_slot;
      // If set, the first "object_slot" row of the view projection: vertex shaders that never read it are refused (they don't draw the
      // scene's camera view)
      uint32_t view_projection_row = no_row;

      constexpr uint32_t PreviousSlot(uint32_t slot) const
      {
         for (const auto& [current, previous] : previous_slots)
         {
            if (current == slot)
               return previous;
         }
         return slot;
      }
   };

   // The previous frame's vertex positions for vertices the CPU rewrites every frame (a fixed world matrix, the motion is in the
   // vertices): a copy of last frame's vertex buffer as a raw buffer at t<slot>, from the draw's first vertex. The second run reads its
   // POSITION input there, at SV_VertexID * stride + offset (3 or 4 floats; past the copy's end it keeps the current one). SM5 only.
   struct PreviousVertices
   {
      uint32_t slot;
      uint32_t stride;
      uint32_t offset;
      uint32_t components; // 3 (w = 1, as the input assembler fills it) or 4
   };

   // The vertex shader with the second run, or empty if unpatchable (the draw then keeps the original shaders). "declares_resources":
   // whether it reads resources, which its second run reads at "previous_resources_slot" (the game binds them there).
   inline std::vector<uint8_t> PatchVertexShader(const uint8_t* code, size_t size, const Layout& layout, std::string* error, bool* declares_resources = nullptr,
      const PreviousVertices* previous_vertices = nullptr)
   {
      std::vector<Chunk> chunks;
      if (!ReadChunks(code, size, &chunks))
         return (*error = "container", std::vector<uint8_t>());
      Chunk* const program = FindChunk(&chunks, FourCC("SHEX"), FourCC("SHDR"));
      Chunk* const output_signature = FindChunk(&chunks, FourCC("OSGN"));
      std::vector<SignatureElement> outputs;
      if (!program || !output_signature || !ReadSignature(output_signature->data, &outputs))
         return (*error = "chunks", std::vector<uint8_t>());
      const auto position = std::ranges::find_if(outputs, [](const SignatureElement& element)
         { return element.system_value == 1; }); // D3D_NAME_POSITION
      if (position == outputs.end() || std::ranges::any_of(outputs, [&](const SignatureElement& element)
                                          { return element.reg >= layout.current_position_register; }))
         return (*error = "outputs", std::vector<uint8_t>());
      const uint32_t position_register = position->reg;

      std::vector<uint32_t> tokens;
      std::vector<Instruction> instructions;
      size_t first_body;
      if (!ReadProgram(*program, &tokens, &instructions, &first_body))
         return (*error = "lengths", std::vector<uint8_t>());
      if (first_body >= instructions.size() || instructions.back().opcode != D3D10_SB_OPCODE_RET || std::count_if(instructions.begin() + first_body, instructions.end(), [](const Instruction& instruction)
                                                                                                       { return instruction.opcode == D3D10_SB_OPCODE_RET; }) != 1)
         return (*error = "returns", std::vector<uint8_t>());

      // Added declarations: each per-draw constant buffer again at its previous frame slot (same size and indexing), each resource
      // again at the second run's slots, the jitter buffer, the two outputs, one temp
      const auto is_resource_declaration = [](D3D10_SB_OPCODE_TYPE opcode)
      { return opcode == D3D10_SB_OPCODE_DCL_RESOURCE || opcode == D3D11_SB_OPCODE_DCL_RESOURCE_RAW || opcode == D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED; };
      size_t object_index = first_body;
      for (size_t i = 0; i < first_body; i++)
      {
         const Instruction& instruction = instructions[i];
         if (instruction.opcode == D3D10_SB_OPCODE_DCL_INDEX_RANGE || instruction.opcode == D3D11_SB_OPCODE_DCL_FUNCTION_BODY)
            return (*error = "declarations", std::vector<uint8_t>());
         // A 1D immediate register: operand token, then the slot
         if (is_resource_declaration(instruction.opcode) && (instruction.length < 3 || DECODE_D3D10_SB_OPERAND_TYPE(tokens[instruction.begin + 1]) != D3D10_SB_OPERAND_TYPE_RESOURCE || DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(tokens[instruction.begin + 1]) != D3D10_SB_OPERAND_INDEX_1D || tokens[instruction.begin + 2] >= layout.resource_slots))
            return (*error = "resources", std::vector<uint8_t>());
         if (declares_resources && is_resource_declaration(instruction.opcode))
         {
            *declares_resources = true;
         }
         if (instruction.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER && instruction.length == 4)
         {
            const uint32_t slot = tokens[instruction.begin + 2];
            if (slot == layout.jitter_slot || std::ranges::any_of(layout.previous_slots, [&](const auto& slots)
                                                 { return slots.second == slot; }))
               return (*error = "slot taken", std::vector<uint8_t>());
            if (slot == layout.object_slot)
               object_index = i;
         }
      }
      if (object_index == first_body)
         return (*error = "no object constants", std::vector<uint8_t>());
      const size_t last_output = FindLastDeclaration(instructions, first_body, {D3D10_SB_OPCODE_DCL_OUTPUT, D3D10_SB_OPCODE_DCL_OUTPUT_SIV, D3D10_SB_OPCODE_DCL_OUTPUT_SGV});

      // The previous vertices: the POSITION input's register, a free one for SV_VertexID, and where their declarations go (the raw
      // buffer after the last buffer or resource, SV_VertexID after the last input)
      Chunk* input_signature = nullptr;
      std::vector<SignatureElement> inputs;
      uint32_t position_input = 0;
      uint32_t vertex_id_input = 0;
      size_t last_resource = first_body;
      size_t last_input = first_body;
      if (previous_vertices)
      {
         input_signature = FindChunk(&chunks, FourCC("ISGN"));
         if (DECODE_D3D10_SB_TOKENIZED_PROGRAM_MAJOR_VERSION(tokens[0]) < 5 || !input_signature || !ReadSignature(input_signature->data, &inputs))
            return (*error = "previous vertices: SM5 input signature", std::vector<uint8_t>());
         const auto position_element = std::ranges::find_if(inputs, [](const SignatureElement& element)
            { return element.system_value == 0 && element.semantic_index == 0 && _stricmp(element.name.c_str(), "POSITION") == 0; });
         if (position_element == inputs.end())
            return (*error = "previous vertices: no POSITION input", std::vector<uint8_t>());
         position_input = position_element->reg;
         for (const SignatureElement& element : inputs)
         {
            vertex_id_input = (std::max)(vertex_id_input, element.reg + 1);
         }
         last_resource = FindLastDeclaration(instructions, first_body, {D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER, D3D10_SB_OPCODE_DCL_SAMPLER, D3D10_SB_OPCODE_DCL_RESOURCE, D3D11_SB_OPCODE_DCL_RESOURCE_RAW, D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED});
         last_input = FindLastDeclaration(instructions, first_body, {D3D10_SB_OPCODE_DCL_INPUT, D3D10_SB_OPCODE_DCL_INPUT_SGV, D3D10_SB_OPCODE_DCL_INPUT_SIV});
         if (vertex_id_input >= D3D11_VS_INPUT_REGISTER_COUNT || last_resource == first_body || last_input == first_body)
            return (*error = "previous vertices: declarations", std::vector<uint8_t>());
      }

      uint32_t scratch;
      std::vector<uint32_t> declarations = CopyDeclarations(tokens, instructions, first_body, (previous_vertices ? 2 : 1), &scratch, [&](size_t i)
         {
            const Instruction& instruction = instructions[i];
            std::vector<uint32_t> added;
            if (is_resource_declaration(instruction.opcode))
            {
               added.assign(tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
               added[2] += layout.previous_resources_slot;
            }
            else if (instruction.opcode == D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER && instruction.length == 4 && layout.PreviousSlot(tokens[instruction.begin + 2]) != tokens[instruction.begin + 2])
            {
               added.assign(tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
               added[2] = layout.PreviousSlot(added[2]);
               if (i == object_index)
               {
                  // The jitter: one register, immediate indexing
                  added.insert(added.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
                  added[instruction.length] &= ~D3D10_SB_CONSTANT_BUFFER_ACCESS_PATTERN_MASK;
                  added[instruction.length + 2] = layout.jitter_slot;
                  added[instruction.length + 3] = 1;
               }
            }
            if (i == last_output)
            {
               for (const uint32_t reg : {layout.current_position_register, layout.previous_position_register})
                  added.insert(added.end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL), reg});
            }
            if (previous_vertices && i == last_resource)
            {
               added.insert(added.end(), {Opcode(D3D11_SB_OPCODE_DCL_RESOURCE_RAW, 3), ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_0_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_RESOURCE) | ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) | ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32), previous_vertices->slot});
            }
            if (previous_vertices && i == last_input)
            {
               added.insert(added.end(), {Opcode(D3D10_SB_OPCODE_DCL_INPUT_SGV, 4), Destination(D3D10_SB_OPERAND_TYPE_INPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_X), vertex_id_input, D3D10_SB_NAME_VERTEX_ID});
            }
            return added; });
      const uint32_t previous_position = scratch + 1; // The previous vertices' position (their second temp)

      // The two runs. Outputs are only ever destinations in vertex shaders.
      std::vector<uint32_t> body;
      bool projects = layout.view_projection_row == Layout::no_row;
      for (int run = 0; run < 2; run++)
      {
         const bool previous = run == 1;
         if (previous && previous_vertices)
         {
            // The previous position from the copy, through the scratch temp (dead until the run's first output write): its byte
            // address at SV_VertexID * stride + offset, the current position past the copy's end
            constexpr uint32_t immediate = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
            const uint32_t components = previous_vertices->components;
            body.insert(body.end(), {
                                       Opcode(D3D11_SB_OPCODE_BUFINFO, 5),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_X),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_RESOURCE, 0, 0, 0, 0),
                                       previous_vertices->slot,
                                       Opcode(D3D10_SB_OPCODE_IMAD, 9),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_W),
                                       previous_position,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 0, 0, 0, 0),
                                       vertex_id_input,
                                       immediate,
                                       previous_vertices->stride,
                                       immediate,
                                       previous_vertices->offset,
                                       Opcode(D3D10_SB_OPCODE_IADD, 7),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_Y),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3),
                                       previous_position,
                                       immediate,
                                       components * 4,
                                       Opcode(D3D10_SB_OPCODE_UGE, 7),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_Y),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1, 1, 1),
                                       scratch,
                                       Opcode(D3D11_SB_OPCODE_LD_RAW, 7),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, (components == 4 ? D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL : D3D10_SB_OPERAND_4_COMPONENT_MASK_X | D3D10_SB_OPERAND_4_COMPONENT_MASK_Y | D3D10_SB_OPERAND_4_COMPONENT_MASK_Z)),
                                       previous_position,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3),
                                       previous_position,
                                       Source(D3D10_SB_OPERAND_TYPE_RESOURCE, 0, 1, 2, 3),
                                       previous_vertices->slot,
                                    });
            if (components != 4)
            {
               body.insert(body.end(), {Opcode(D3D10_SB_OPCODE_MOV, 5), Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_W), previous_position, immediate, std::bit_cast<uint32_t>(1.f)});
            }
            body.insert(body.end(), {
                                       Opcode(D3D10_SB_OPCODE_MOVC, 9),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),
                                       previous_position,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1, 1, 1),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 2, 3),
                                       previous_position,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1, 2, 3),
                                       position_input,
                                    });
         }
         for (size_t i = first_body; i < instructions.size(); i++)
         {
            const Instruction& instruction = instructions[i];
            if (!previous && i + 1 == instructions.size())
               break; // The first run's ret
            if (instruction.opcode == D3D10_SB_OPCODE_LABEL || instruction.opcode == D3D10_SB_OPCODE_CALL || instruction.opcode == D3D10_SB_OPCODE_CALLC || instruction.opcode == D3D11_SB_OPCODE_INTERFACE_CALL || instruction.opcode == D3D10_SB_OPCODE_RETC || instruction.opcode == D3D10_SB_OPCODE_CUSTOMDATA)
               return (*error = "control flow", std::vector<uint8_t>());
            const size_t offset = body.size();
            body.insert(body.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
            bool supported = true;
            const bool walked = WalkOperands(tokens, instruction, [&](size_t token_position, size_t index_position)
               {
                  uint32_t& token = body[offset + token_position - instruction.begin];
                  const D3D10_SB_OPERAND_TYPE type = DECODE_D3D10_SB_OPERAND_TYPE(token);
                  if (type == D3D10_SB_OPERAND_TYPE_OUTPUT)
                  {
                     if (index_position == no_index || DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
                        return supported = false;
                     uint32_t& index = body[offset + index_position - instruction.begin];
                     // SV_Position goes to the scratch temp in the first run (copied to its output and ours after), to ours in
                     // the second. The second run's other outputs go to the scratch temp, dead.
                     if (previous && index == position_register)
                     {
                        index = layout.previous_position_register;
                     }
                     else if (!previous && index != position_register)
                     {
                        return true;
                     }
                     else
                     {
                        token = (token & ~D3D10_SB_OPERAND_TYPE_MASK) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP);
                        index = scratch;
                     }
                  }
                  else if (previous && previous_vertices && type == D3D10_SB_OPERAND_TYPE_INPUT)
                  {
                     if (index_position == no_index || DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
                        return supported = false;
                     // The second run's position input is the previous one (same swizzle)
                     if (uint32_t& index = body[offset + index_position - instruction.begin]; index == position_input)
                     {
                        token = (token & ~D3D10_SB_OPERAND_TYPE_MASK) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP);
                        index = previous_position;
                     }
                  }
                  else if (previous && type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && index_position != no_index)
                  {
                     uint32_t& index = body[offset + index_position - instruction.begin];
                     index = layout.PreviousSlot(index);
                  }
                  else if (type == D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER && index_position != no_index && tokens[index_position] == layout.object_slot && DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, token) == D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
                  {
                     const uint32_t row = tokens[index_position + 1];
                     projects |= row >= layout.view_projection_row && row < layout.view_projection_row + 4;
                  }
                  else if (previous && type == D3D10_SB_OPERAND_TYPE_RESOURCE)
                  {
                     if (index_position == no_index || DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
                        return supported = false;
                     body[offset + index_position - instruction.begin] += layout.previous_resources_slot;
                  }
                  return true; });
            if (!walked || !supported)
               return (*error = "operands", std::vector<uint8_t>());
         }
         if (!previous)
         {
            // Our unjittered current position output, then SV_Position with xy += jitter * w
            constexpr uint32_t mov = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5);
            constexpr uint32_t jitter_operand = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) | ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) | ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE(0, 1, 0, 0) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER) | ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D) | ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, D3D10_SB_OPERAND_INDEX_IMMEDIATE32) | ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
            body.insert(body.end(), {
                                       mov,
                                       Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),
                                       layout.current_position_register,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 2, 3),
                                       scratch,
                                       ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_xy),
                                       scratch,
                                       jitter_operand,
                                       layout.jitter_slot,
                                       0,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3),
                                       scratch,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 0),
                                       scratch,
                                       mov,
                                       Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),
                                       position_register,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 2, 3),
                                       scratch,
                                    });
         }
      }

      if (!projects)
         return (*error = "screen space (view projection unused)", std::vector<uint8_t>());
      declarations.insert(declarations.end(), body.begin(), body.end());
      WriteProgram(&declarations, program);

      SignatureElement element = {semantic_name, 0, 0, 3 /* float */, layout.current_position_register, 0xF, 0};
      outputs.push_back(element);
      element.semantic_index = 1;
      element.reg = layout.previous_position_register;
      outputs.push_back(element);
      output_signature->data = WriteSignature(outputs);
      if (previous_vertices)
      {
         inputs.push_back({"SV_VertexID", 0, D3D_NAME_VERTEX_ID, D3D_REGISTER_COMPONENT_UINT32, vertex_id_input, 0x1, 0x1});
         input_signature->data = WriteSignature(inputs);
      }
      return WriteChunks(chunks);
   }

   // A pixel shader's color target: known by name, its system value is left undefined in the signature
   inline bool IsTarget(const SignatureElement& element)
   {
      return _stricmp(element.name.c_str(), "SV_Target") == 0;
   }

   // A pixel shader's container, signatures and program, checked for the pixel shader patches: game targets below the layout's
   // "target_slot", inputs below the added ones, one final ret. "targets_only" also refuses other outputs (a depth output, as a target
   // declared after oDepth).
   struct PixelShader
   {
      std::vector<Chunk> chunks;
      Chunk* program = nullptr;
      Chunk* input_signature = nullptr;
      Chunk* output_signature = nullptr;
      std::vector<SignatureElement> inputs, outputs;
      SignatureElement target;
      std::vector<uint32_t> tokens;
      std::vector<Instruction> instructions;
      size_t first_body = 0;
   };

   inline bool ReadPixelShader(const uint8_t* code, size_t size, const Layout& layout, PixelShader* shader, std::string* error, bool targets_only)
   {
      if (!ReadChunks(code, size, &shader->chunks))
         return (*error = "container", false);
      shader->program = FindChunk(&shader->chunks, FourCC("SHEX"), FourCC("SHDR"));
      shader->input_signature = FindChunk(&shader->chunks, FourCC("ISGN"));
      shader->output_signature = FindChunk(&shader->chunks, FourCC("OSGN"));
      if (!shader->program || !shader->input_signature || !shader->output_signature || !ReadSignature(shader->input_signature->data, &shader->inputs) || !ReadSignature(shader->output_signature->data, &shader->outputs))
         return (*error = "chunks", false);
      const auto target = std::ranges::find_if(shader->outputs, IsTarget);
      if (target == shader->outputs.end() || std::ranges::any_of(shader->outputs, [&](const SignatureElement& element)
                                                { return IsTarget(element) ? element.reg >= layout.target_slot : targets_only; }) ||
          std::ranges::any_of(shader->inputs, [&](const SignatureElement& element)
             { return element.reg >= layout.current_position_register; }))
         return (*error = "signatures", false);
      shader->target = *target;

      if (!ReadProgram(*shader->program, &shader->tokens, &shader->instructions, &shader->first_body))
         return (*error = "lengths", false);
      const auto& instructions = shader->instructions;
      if (shader->first_body >= instructions.size() || instructions.back().opcode != D3D10_SB_OPCODE_RET || std::ranges::any_of(instructions.begin() + shader->first_body, instructions.end() - 1, [](const Instruction& instruction)
                                                                                                               { return instruction.opcode == D3D10_SB_OPCODE_RET || instruction.opcode == D3D10_SB_OPCODE_RETC; }))
         return (*error = "returns", false);
      return true;
   }

   // The pixel shader motion vector pieces both patches below add: the two position inputs (declared after the last input, or before
   // the first output), the delta written to "target_slot".xy and their signature entries.

   // The declaration the position inputs go after, "first_body" (refused) without an output declaration, or one that comes first
   inline size_t FindMotionVectorInputsPosition(const std::vector<Instruction>& instructions, size_t first_body)
   {
      const size_t first_output = size_t(std::ranges::find_if(instructions.begin(), instructions.begin() + first_body, [](const Instruction& instruction)
                                            { return instruction.opcode == D3D10_SB_OPCODE_DCL_OUTPUT; }) -
                                         instructions.begin());
      if (first_output == first_body || first_output == 0)
         return first_body;
      const size_t last_input = FindLastDeclaration(instructions, first_body, {D3D10_SB_OPCODE_DCL_INPUT_PS, D3D10_SB_OPCODE_DCL_INPUT_PS_SGV, D3D10_SB_OPCODE_DCL_INPUT_PS_SIV});
      return (last_input != first_body ? last_input : (first_output - 1));
   }

   inline void AppendMotionVectorInputDeclarations(std::vector<uint32_t>* tokens, const Layout& layout)
   {
      for (const uint32_t reg : {layout.current_position_register, layout.previous_position_register})
         tokens->insert(tokens->end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_INPUT_PS) | ENCODE_D3D10_SB_INPUT_INTERPOLATION_MODE(D3D10_SB_INTERPOLATION_LINEAR) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3), Destination(D3D10_SB_OPERAND_TYPE_INPUT, mask_xyw), reg});
   }

   // In UV space, into "target_slot".xy: (previous.xy / previous.w - current.xy / current.w) * (0.5, -0.5), through two temps
   inline void AppendMotionVector(std::vector<uint32_t>* tokens, const Layout& layout, uint32_t current, uint32_t previous)
   {
      constexpr uint32_t div = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7);
      tokens->insert(tokens->end(), {
                                       div,
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_xy),
                                       current,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1, 0, 0),
                                       layout.current_position_register,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 3, 3, 3, 3),
                                       layout.current_position_register,
                                       div,
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_xy),
                                       previous,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 0, 1, 0, 0),
                                       layout.previous_position_register,
                                       Source(D3D10_SB_OPERAND_TYPE_INPUT, 3, 3, 3, 3),
                                       layout.previous_position_register,
                                       ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8),
                                       Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_xy),
                                       current,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 0),
                                       previous,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 0) | ENCODE_D3D10_SB_OPERAND_EXTENDED(true),
                                       ENCODE_D3D10_SB_EXTENDED_OPERAND_MODIFIER(D3D10_SB_OPERAND_MODIFIER_NEG),
                                       current,
                                       ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10),
                                       Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, mask_xy),
                                       layout.target_slot,
                                       Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 0, 0),
                                       current,
                                       ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32),
                                       std::bit_cast<uint32_t>(0.5f),
                                       std::bit_cast<uint32_t>(-0.5f),
                                       0,
                                       0,
                                    });
   }

   // Signature masks are plain component bits (x = 1), unlike the operand token masks
   inline void AddMotionVectorInputSignature(PixelShader* shader, const Layout& layout)
   {
      shader->inputs.push_back({.name = semantic_name, .semantic_index = 0, .system_value = 0, .component_type = 3, .reg = layout.current_position_register, .mask = 0xF, .rw_mask = 0xB});
      shader->inputs.push_back({.name = semantic_name, .semantic_index = 1, .system_value = 0, .component_type = 3, .reg = layout.previous_position_register, .mask = 0xF, .rw_mask = 0xB});
      shader->input_signature->data = WriteSignature(shader->inputs);
   }

   // The pixel shader with the motion vector target, or empty if unpatchable. "targets_only": see "ReadPixelShader" (a target
   // declared after oDepth stays after it otherwise).
   inline std::vector<uint8_t> PatchPixelShader(const uint8_t* code, size_t size, const Layout& layout, std::string* error, bool targets_only = false)
   {
      PixelShader shader;
      if (!ReadPixelShader(code, size, layout, &shader, error, targets_only))
         return std::vector<uint8_t>();
      const auto& tokens = shader.tokens;
      const auto& instructions = shader.instructions;
      const size_t first_body = shader.first_body;

      // The two inputs after the last input (or before the outputs), the target after the last output
      const size_t last_input = FindMotionVectorInputsPosition(instructions, first_body);
      if (last_input == first_body)
         return (*error = "no outputs", std::vector<uint8_t>());
      // After the last color target (a depth output, oDepth, stays after it)
      size_t last_output = first_body;
      for (size_t i = 0; i < first_body; i++)
      {
         if (instructions[i].opcode == D3D10_SB_OPCODE_DCL_OUTPUT && DECODE_D3D10_SB_OPERAND_TYPE(tokens[instructions[i].begin + 1]) == D3D10_SB_OPERAND_TYPE_OUTPUT)
            last_output = i;
      }
      uint32_t temp;
      std::vector<uint32_t> declarations = CopyDeclarations(tokens, instructions, first_body, 2, &temp, [&](size_t i)
         {
            std::vector<uint32_t> added;
            if (i == last_input)
            {
               AppendMotionVectorInputDeclarations(&added, layout);
            }
            if (i == last_output)
            {
               added.insert(added.end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, mask_xy), layout.target_slot});
            }
            return added; });

      // Before the final ret
      const size_t last = instructions.back().begin;
      declarations.insert(declarations.end(), tokens.begin() + instructions[first_body].begin, tokens.begin() + last);
      AppendMotionVector(&declarations, layout, temp, temp + 1);
      declarations.insert(declarations.end(), tokens.begin() + last, tokens.end());
      WriteProgram(&declarations, shader.program);

      AddMotionVectorInputSignature(&shader, layout);
      SignatureElement output = shader.target;
      output.semantic_index = layout.target_slot;
      output.reg = layout.target_slot;
      output.component_type = D3D_REGISTER_COMPONENT_FLOAT32; // Whatever the game's target holds
      output.mask = 0x3;
      output.rw_mask = 0;
      shader.outputs.insert(std::ranges::find_if(shader.outputs.rbegin(), shader.outputs.rend(), IsTarget).base(), output);
      shader.output_signature->data = WriteSignature(shader.outputs);
      return WriteChunks(shader.chunks);
   }

   // FSR's reactive and transparency & composition masks from the game's alpha blended draws.
   // The pixel shader with the mask target: the color goes to a temp, copied to the target before the final ret, then the draw's
   // mask to "reactive_slot": "additive" (blended ONE onto the scene) x = reactivity max(r, g, b) * a (the color part tonemapped
   // x / (1 + x) so HDR stays within 0-1) and y = 0 (an unwritten component is undefined; the max blend keeps what another draw
   // wrote), else (blended INV_SRC_ALPHA) x and y = a, reactivity (thresholded by the fill: water, fountains) and transparency &
   // composition (faint glass and haze). Empty if unpatchable.
   // "ALPHA_MOTION_VECTORS" (blended INV_SRC_ALPHA, the vertex shader patched too): the draw also writes its motion vector to
   // "target_slot" with a weight in w, for a blend state that blends that target like the color: opaque texels (a chain link fence's
   // wires) move with the draw and keep their history, empty ones keep what's behind. The weight is the alpha thresholded around 0.5
   // (0 up to 0.4, 1 from 0.6): faint texels (a dirty or cracked windshield) would otherwise pull the motion of the background seen
   // through them toward their own.
   // A mask too (x and y = a) only with a "reactive_slot": FSR's masks are for content without motion vectors, so it is for the draws
   // whose motion vectors are only the camera's.
   enum class ReactiveMode : uint8_t
   {
      ALPHA,
      ADDITIVE,
      ALPHA_MOTION_VECTORS,
   };

   inline std::vector<uint8_t> PatchPixelShaderReactive(const uint8_t* code, size_t size, const Layout& layout, uint32_t reactive_slot, ReactiveMode mode, std::string* error)
   {
      PixelShader shader;
      if (!ReadPixelShader(code, size, layout, &shader, error, /* targets_only */ true))
         return {};
      auto& [chunks, program, input_signature, output_signature, inputs, outputs, target, tokens, instructions, first_body] = shader;
      const bool additive = mode == ReactiveMode::ADDITIVE;
      const bool motion_vectors = mode == ReactiveMode::ALPHA_MOTION_VECTORS;
      const bool mask = reactive_slot != UINT32_MAX;
      // The game's other targets are written as they were (UE3's translucents also write o1.x); the
      // blended color, and its alpha, must then be RT0's
      if ((outputs.size() > 1 && target.reg != 0) || (!mask && !motion_vectors))
         return (*error = "targets", std::vector<uint8_t>());
      // The color is copied whole and its alpha read: a float4 target only
      if (target.component_type != D3D_REGISTER_COMPONENT_FLOAT32 || target.mask != 0xF)
         return (*error = "target type", std::vector<uint8_t>());
      const size_t last_output = FindLastDeclaration(instructions, first_body, {D3D10_SB_OPCODE_DCL_OUTPUT});
      if (last_output == first_body)
         return (*error = "no outputs", std::vector<uint8_t>());
      const size_t last_input = FindMotionVectorInputsPosition(instructions, first_body);
      if (motion_vectors && last_input == first_body)
         return (*error = "no input position", std::vector<uint8_t>());
      uint32_t temp;
      // The motion vectors take a third temp
      std::vector<uint32_t> declarations = CopyDeclarations(tokens, instructions, first_body, motion_vectors ? 3 : 2, &temp, [&](size_t i)
         {
            std::vector<uint32_t> added;
            if (motion_vectors && i == last_input)
            {
               AppendMotionVectorInputDeclarations(&added, layout);
            }
            if (i == last_output)
            {
               if (motion_vectors)
               {
                  added.insert(added.end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL), layout.target_slot});
               }
               if (mask)
               {
                  added.insert(added.end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DCL_OUTPUT) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(3), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, mask_xy), reactive_slot});
               }
            }
            return added; });

      // The body with the target retyped to the color temp (outputs are only ever destinations)
      const uint32_t color = temp;
      const uint32_t scratch = temp + 1;
      const uint32_t previous_position_temp = temp + 2; // The motion vector's second temp ("AppendMotionVector")
      for (size_t i = first_body; i + 1 < instructions.size(); i++)
      {
         const Instruction& instruction = instructions[i];
         const size_t offset = declarations.size();
         declarations.insert(declarations.end(), tokens.begin() + instruction.begin, tokens.begin() + instruction.begin + instruction.length);
         bool supported = true;
         const bool walked = WalkOperands(tokens, instruction, [&](size_t token_position, size_t index_position)
            {
               uint32_t& token = declarations[offset + token_position - instruction.begin];
               if (DECODE_D3D10_SB_OPERAND_TYPE(token) != D3D10_SB_OPERAND_TYPE_OUTPUT)
                  return true;
               if (index_position == no_index || DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0, token) != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
                  return supported = false;
               if (tokens[index_position] != target.reg)
                  return true;
               token = (token & ~D3D10_SB_OPERAND_TYPE_MASK) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_TEMP);
               declarations[offset + index_position - instruction.begin] = color;
               return true; });
         if (!walked || !supported)
            return (*error = "operands", std::vector<uint8_t>());
      }

      constexpr uint32_t mask_x = D3D10_SB_OPERAND_4_COMPONENT_MASK_X;
      constexpr uint32_t mov = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(5);
      constexpr uint32_t max = ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7);
      constexpr uint32_t immediate = ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) | ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32);
      constexpr uint32_t one = std::bit_cast<uint32_t>(1.f);
      std::vector<uint32_t> reactive = {mov, Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL), target.reg, Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 1, 2, 3), color};
      if (additive)
      {
         // reactive.y = 0
         reactive.insert(reactive.end(), {ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_Y), reactive_slot, immediate, 0, 0, 0, 0});
         // scratch.x = max(r, g, b, 0), scratch.y = scratch.x + 1, reactive.x = saturate(scratch.x / scratch.y * a)
         reactive.insert(reactive.end(), {
                                            max,
                                            Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_x),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            color,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1, 1, 1),
                                            color,
                                            max,
                                            Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_x),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 2, 2, 2, 2),
                                            color,
                                            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MAX) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10),
                                            Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_x),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            scratch,
                                            immediate,
                                            0,
                                            0,
                                            0,
                                            0,
                                            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_ADD) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(10),
                                            Destination(D3D10_SB_OPERAND_TYPE_TEMP, D3D10_SB_OPERAND_4_COMPONENT_MASK_Y),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            scratch,
                                            immediate,
                                            one,
                                            one,
                                            one,
                                            one,
                                            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_DIV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7),
                                            Destination(D3D10_SB_OPERAND_TYPE_TEMP, mask_x),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 1, 1, 1, 1),
                                            scratch,
                                            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MUL) | ENCODE_D3D10_SB_INSTRUCTION_SATURATE(true) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(7),
                                            Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, mask_x),
                                            reactive_slot,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 0, 0, 0, 0),
                                            scratch,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3),
                                            color,
                                         });
      }
      else if (motion_vectors)
      {
         // The motion vector (as "PatchPixelShader"), then w = saturate(a * 5 - 2) for the target's blend (z = 0: an unwritten
         // component is undefined)
         AppendMotionVector(&reactive, layout, scratch, previous_position_temp);
         constexpr uint32_t weight_scale = std::bit_cast<uint32_t>(5.f); // saturate(a * 5 - 2): 0 up to 0.4, 1 from 0.6
         constexpr uint32_t weight_offset = std::bit_cast<uint32_t>(-2.f);
         reactive.insert(reactive.end(), {
                                            ENCODE_D3D10_SB_OPCODE_TYPE(D3D10_SB_OPCODE_MOV) | ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8),
                                            Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_Z),
                                            layout.target_slot,
                                            immediate,
                                            0,
                                            0,
                                            0,
                                            0,
                                            Opcode(D3D10_SB_OPCODE_MAD, 15, true),
                                            Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, D3D10_SB_OPERAND_4_COMPONENT_MASK_W),
                                            layout.target_slot,
                                            Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3),
                                            color,
                                            immediate,
                                            weight_scale,
                                            weight_scale,
                                            weight_scale,
                                            weight_scale,
                                            immediate,
                                            weight_offset,
                                            weight_offset,
                                            weight_offset,
                                            weight_offset,
                                         });
      }
      if (mask && !additive)
      {
         reactive.insert(reactive.end(), {mov | ENCODE_D3D10_SB_INSTRUCTION_SATURATE(true), Destination(D3D10_SB_OPERAND_TYPE_OUTPUT, mask_xy), reactive_slot, Source(D3D10_SB_OPERAND_TYPE_TEMP, 3, 3, 3, 3), color});
      }
      declarations.insert(declarations.end(), reactive.begin(), reactive.end());
      declarations.insert(declarations.end(), tokens.begin() + instructions.back().begin, tokens.end());
      WriteProgram(&declarations, program);

      // Targets like the game's (signature masks are plain component bits, x = 1, unlike operand token masks)
      SignatureElement output = target;
      output.rw_mask = 0;
      if (motion_vectors)
      {
         AddMotionVectorInputSignature(&shader, layout);
         output.semantic_index = layout.target_slot;
         output.reg = layout.target_slot;
         output.mask = 0xF;
         outputs.push_back(output);
      }
      if (mask)
      {
         output.semantic_index = reactive_slot;
         output.reg = reactive_slot;
         output.mask = 0x3;
         outputs.push_back(output);
      }
      output_signature->data = WriteSignature(outputs);
      return WriteChunks(chunks);
   }
} // namespace MotionVectorPatch
