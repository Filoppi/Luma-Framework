#pragma once

#include <stdint.h>
#include <string.h>

constexpr float srgb_white_level = 80;
static float default_paper_white = 203; // ITU White Level
constexpr float default_peak_white = 1000;

namespace Globals
{
	enum class ModDevelopmentState : uint8_t
	{
		// Might be broken or simply an empty mod that might not do anything
		NonFunctional,
		// Mod is WIP, might be a release candidate undergoing testing, or simply be in development
		WorkInProgress,
		// The mod is playable and has no known blocking issues, though it's still missing features etc
		Playable,
		// Generally means the mod won't be improved any further (though obviously it always might)
		Finished,
	};

	// Don't change this unless you have a reason to. There might be some other references to this name hardcoded in other strings.
	static const char* MOD_NAME = "Luma";
	// The following are meant to be replaced per game:
	static char GAME_NAME[64] = "Template";
	static char DESCRIPTION[256] = "Template Luma mod";
	static char WEBSITE[256] = "";
	static uint32_t VERSION = 1; // Internal version (not public facing, no major/minor/revision versioning yet). Always starts from 1. Remember to manually add a check if you delete shaders in between versions and you want to force remove them from people's installations too.
	static ModDevelopmentState DEVELOPMENT_STATE = ModDevelopmentState::Finished; // Change this as the mod is developed to inform users and warn them when it's not finished

	__forceinline void SetGlobals(const char* game_name, const char* mod_description = "", const char* mod_website = "", uint32_t mod_version = 0)
	{
		// We should complain if the attempted written size is greater but whatever
		strcpy_s(GAME_NAME, sizeof(GAME_NAME), game_name);
		strcpy_s(DESCRIPTION, sizeof(DESCRIPTION), mod_description);
		strcpy_s(WEBSITE, sizeof(WEBSITE), mod_website);

		if (mod_version != 0 && mod_version != uint32_t(-1))
		{
			VERSION = mod_version;
		}
	}
}
