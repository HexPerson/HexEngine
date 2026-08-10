
#pragma once

#include "../Required.hpp"
#include "ITexture3D.hpp"

namespace HexEngine
{
	// P4.7: .cube 3D colour LUT loader. Watches a fixed file path
	// (Data/ColourGrade.cube next to the executable's working directory) and
	// hot-reloads it when the file appears, changes, or is deleted - drop a
	// Resolve/Photoshop export in place and it applies within a couple of
	// seconds, no cvar plumbing needed (the engine has no string cvars).
	// Strength is r_colourLutStrength; the tonemap shaders bypass when no
	// volume is bound (g_lutParams.y == 0).
	//
	// Parser accepts the common .cube subset: LUT_3D_SIZE, DOMAIN_MIN/MAX,
	// TITLE, # comments, and N^3 "r g b" float lines in standard red-fastest
	// order. Uploaded as an RGBA16F volume via CreateTexture3D initial data
	// (no per-mip APIs needed - single mip, trilinear sampled).
	class HEX_API ColourLut
	{
	public:
		~ColourLut();

		// Cheap unless the file's write time changed: stat the path every
		// ~2 seconds' worth of frames and (re)load on change.
		void Poll();

		void Destroy();

		ITexture3D* GetVolume() const { return _volume; }
		// LUT edge size N, 0 when nothing is loaded.
		int32_t GetSize() const { return _volume != nullptr ? _size : 0; }

	private:
		bool LoadFromFile(const fs::path& path);

		ITexture3D* _volume = nullptr;
		int32_t _size = 0;
		uint64_t _lastPollFrame = 0;
		fs::file_time_type _loadedWriteTime{};
		bool _fileWasPresent = false;
	};
}
