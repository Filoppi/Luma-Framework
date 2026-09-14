#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <dxp/sm5/Recipe.hpp>

#include "globals.h"

// Managed non-COM assets, keyed by CompileTimeStringHash like every other Luma
// asset (shaders, textures, managed_resources). Currently holds DXP recipes
// only; add new non-COM asset types (settings documents, raw data) here when
// they exist — do not expand this for COM resources (those belong in
// managed_resources.h).
struct Recipes
{
   // Parsed DXP recipes, keyed by CompileTimeStringHash(recipe name). Recipes
   // are immutable once parsed and safe to share across threads (Execute is
   // const and thread-safe); the shared_ptr keeps a stable address for the
   // async patch jobs.
   std::unordered_map<uint32_t, std::shared_ptr<dxp::sm5::Recipe>> recipes;

   // Root folder recipes are loaded from: <root>/<GAME_NAME>/Recipes.
   std::filesystem::path recipes_path;

   void SetRootPath(const std::filesystem::path& root_path)
   {
      recipes_path = root_path / Globals::GAME_NAME / "Recipes";
   }

   std::filesystem::path GetRootPath() const
   {
      return recipes_path;
   }

   // Returns the recipe for a name hash, or nullptr if not loaded.
   std::shared_ptr<dxp::sm5::Recipe> GetRecipe(uint32_t recipe_hash) const
   {
      const auto it = recipes.find(recipe_hash);
      return it != recipes.end() ? it->second : nullptr;
   }

   // Parses <recipes_path>/<recipe_name>.recipe.yml and caches it under
   // recipe_hash. Returns false if the file is missing or fails to parse.
   bool LoadFromFile(uint32_t recipe_hash, const std::string& recipe_name)
   {
      const std::filesystem::path recipe_file_path = recipes_path / (recipe_name + ".recipe.yml");
      if (!std::filesystem::is_regular_file(recipe_file_path))
      {
         return false;
      }

      auto recipe = dxp::sm5::Recipe::ParseFromFile(recipe_file_path.string());
      if (!recipe)
      {
         return false;
      }

      recipes[recipe_hash] = std::make_shared<dxp::sm5::Recipe>(std::move(*recipe));
      return true;
   }
};
