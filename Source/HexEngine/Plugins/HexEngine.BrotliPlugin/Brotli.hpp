
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <brotli\encode.h>
#include <brotli\decode.h>

class Brotli : public HexEngine::ICompressionProvider
{
public:
	virtual bool Create() override { return true; }
	virtual void Destroy() override {}

	virtual bool CompressFile(const fs::path& absolutePath, const fs::path& output) override;

	virtual bool DecompressFile(const fs::path& absolutePath, const fs::path& output) override;

	virtual bool CompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output) override;

	virtual bool DecompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output) override;

	virtual bool CompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& output) override;

	virtual bool DecompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& output) override;

private:
	// Brotli's cost curve is non-linear: quality 11 is ~10x slower than 9
	// and ~80x slower than 4 with single-digit % size win. Quality 5 is the
	// usual sweet spot for build-time / runtime packing (AssetPacker,
	// VolumetricTerrain chunk persistence). Bump only if you're shipping a
	// static-distribution artifact where pack time is irrelevant.
	const uint32_t CompressionQuality = 5;
};
