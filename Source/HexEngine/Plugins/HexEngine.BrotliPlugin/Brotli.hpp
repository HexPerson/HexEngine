
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <brotli\encode.h>
#include <brotli\decode.h>

class Brotli : public ICompressionProvider
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
	const uint32_t CompressionQuality = 9;
};
