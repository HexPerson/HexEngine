
#include "ColourLut.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/TimeManager.hpp"
#include "IGraphicsDevice.hpp"

#include <DirectXPackedVector.h>
#include <fstream>
#include <sstream>
#include <vector>

namespace HexEngine
{
	namespace
	{
		// Fixed watch path, relative to the working directory (Bin/x64/<cfg>).
		const wchar_t* kLutPath = L"Data/ColourGrade.cube";
		constexpr uint64_t kPollIntervalFrames = 120; // ~2s at 60fps
	}

	ColourLut::~ColourLut()
	{
		Destroy();
	}

	void ColourLut::Destroy()
	{
		SAFE_DELETE(_volume);
		_size = 0;
	}

	void ColourLut::Poll()
	{
		if (g_pEnv == nullptr || g_pEnv->_timeManager == nullptr)
			return;

		const uint64_t frame = (uint64_t)g_pEnv->_timeManager->_frameCount;
		if (frame != 0 && frame - _lastPollFrame < kPollIntervalFrames)
			return;
		_lastPollFrame = frame;

		const fs::path path(kLutPath);
		std::error_code ec;
		const bool present = fs::exists(path, ec) && !ec;

		if (!present)
		{
			if (_fileWasPresent)
			{
				LOG_INFO("ColourLut: %ls removed - LUT disabled", kLutPath);
				Destroy();
			}
			_fileWasPresent = false;
			return;
		}

		const auto writeTime = fs::last_write_time(path, ec);
		if (ec)
			return;

		if (_fileWasPresent && writeTime == _loadedWriteTime)
			return;

		if (LoadFromFile(path))
		{
			_loadedWriteTime = writeTime;
			_fileWasPresent = true;
		}
		else
		{
			// Parse failure: remember the write time anyway so a broken file
			// doesn't get re-parsed (and re-logged) every poll.
			_loadedWriteTime = writeTime;
			_fileWasPresent = true;
			Destroy();
		}
	}

	bool ColourLut::LoadFromFile(const fs::path& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
		{
			LOG_WARN("ColourLut: failed to open %ls", path.c_str());
			return false;
		}

		int32_t size = 0;
		std::vector<float> data; // rgb triples, red-fastest
		math::Vector3 domainMin(0.0f, 0.0f, 0.0f);
		math::Vector3 domainMax(1.0f, 1.0f, 1.0f);

		std::string line;
		while (std::getline(file, line))
		{
			// Strip CR and leading whitespace; skip empties and comments.
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			size_t start = line.find_first_not_of(" \t");
			if (start == std::string::npos)
				continue;
			if (line[start] == '#')
				continue;

			std::istringstream ss(line.substr(start));
			std::string token;
			ss >> token;

			if (token == "TITLE")
				continue;
			if (token == "LUT_1D_SIZE")
			{
				LOG_WARN("ColourLut: %ls is a 1D LUT - only 3D (LUT_3D_SIZE) is supported", path.c_str());
				return false;
			}
			if (token == "LUT_3D_SIZE")
			{
				ss >> size;
				if (size < 2 || size > 128)
				{
					LOG_WARN("ColourLut: unsupported LUT_3D_SIZE %d in %ls", size, path.c_str());
					return false;
				}
				data.reserve((size_t)size * size * size * 3);
				continue;
			}
			if (token == "DOMAIN_MIN")
			{
				ss >> domainMin.x >> domainMin.y >> domainMin.z;
				continue;
			}
			if (token == "DOMAIN_MAX")
			{
				ss >> domainMax.x >> domainMax.y >> domainMax.z;
				continue;
			}

			// Data line: the token we already pulled is the red component.
			float r = 0.0f, g = 0.0f, b = 0.0f;
			try { r = std::stof(token); }
			catch (...) { continue; } // unknown keyword - skip
			ss >> g >> b;
			data.push_back(r);
			data.push_back(g);
			data.push_back(b);
		}

		const size_t expected = (size_t)size * size * size * 3;
		if (size == 0 || data.size() != expected)
		{
			LOG_WARN("ColourLut: %ls parse failed (size %d, %zu/%zu floats)",
				path.c_str(), size, data.size(), expected);
			return false;
		}

		// The shader samples with a plain [0,1] uvw; honour non-identity
		// domains by noting them (rare in practice - Resolve exports 0..1).
		if (domainMin != math::Vector3(0.0f, 0.0f, 0.0f) ||
			domainMax != math::Vector3(1.0f, 1.0f, 1.0f))
		{
			LOG_WARN("ColourLut: %ls has a non-identity DOMAIN (min %.2f %.2f %.2f, max %.2f %.2f %.2f) - sampled as 0..1",
				path.c_str(), domainMin.x, domainMin.y, domainMin.z, domainMax.x, domainMax.y, domainMax.z);
		}

		// Pack to RGBA16F. .cube is red-fastest, which maps directly onto the
		// volume's x axis (u = red, v = green, w = blue).
		using DirectX::PackedVector::XMConvertFloatToHalf;
		std::vector<uint16_t> halves((size_t)size * size * size * 4);
		for (size_t i = 0; i < (size_t)size * size * size; ++i)
		{
			halves[i * 4 + 0] = XMConvertFloatToHalf(data[i * 3 + 0]);
			halves[i * 4 + 1] = XMConvertFloatToHalf(data[i * 3 + 1]);
			halves[i * 4 + 2] = XMConvertFloatToHalf(data[i * 3 + 2]);
			halves[i * 4 + 3] = XMConvertFloatToHalf(1.0f);
		}

		D3D11_SUBRESOURCE_DATA initial = {};
		initial.pSysMem = halves.data();
		initial.SysMemPitch = (UINT)(size * 4 * sizeof(uint16_t));
		initial.SysMemSlicePitch = (UINT)(size * size * 4 * sizeof(uint16_t));

		ITexture3D* volume = g_pEnv->_graphicsDevice->CreateTexture3D(
			size, size, size,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE,
			1, 1, 0,
			&initial,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE3D);
		if (volume == nullptr)
		{
			LOG_WARN("ColourLut: CreateTexture3D failed for %ls", path.c_str());
			return false;
		}

		Destroy();
		_volume = volume;
		_size = size;
		LOG_INFO("ColourLut: loaded %ls (%dx%dx%d)", path.c_str(), size, size, size);
		return true;
	}
}
