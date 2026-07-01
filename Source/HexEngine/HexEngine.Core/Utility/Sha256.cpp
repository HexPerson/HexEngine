
#include "Sha256.hpp"

#include <Windows.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <array>

#pragma comment(lib, "bcrypt.lib") // auto-link for any consumer of HexEngine.Core

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace HexEngine
{
	namespace
	{
		std::string ToHex(const unsigned char* data, size_t n)
		{
			static const char* kHex = "0123456789abcdef";
			std::string s;
			s.resize(n * 2);
			for (size_t i = 0; i < n; ++i)
			{
				s[i * 2 + 0] = kHex[(data[i] >> 4) & 0xF];
				s[i * 2 + 1] = kHex[data[i] & 0xF];
			}
			return s;
		}

		// RAII wrappers so partial failures never leak CNG handles.
		struct Sha256Ctx
		{
			BCRYPT_ALG_HANDLE alg = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			std::vector<unsigned char> hashObject;
			std::vector<unsigned char> digest;

			~Sha256Ctx()
			{
				if (hash) BCryptDestroyHash(hash);
				if (alg) BCryptCloseAlgorithmProvider(alg, 0);
			}

			bool Init(std::string& error)
			{
				if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
				{
					error = "BCryptOpenAlgorithmProvider failed";
					return false;
				}
				DWORD cbObject = 0, cbData = 0, cbHash = 0;
				if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbObject, sizeof(DWORD), &cbData, 0) != STATUS_SUCCESS ||
					BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(DWORD), &cbData, 0) != STATUS_SUCCESS)
				{
					error = "BCryptGetProperty failed";
					return false;
				}
				hashObject.resize(cbObject);
				digest.resize(cbHash);
				if (BCryptCreateHash(alg, &hash, hashObject.data(), cbObject, nullptr, 0, 0) != STATUS_SUCCESS)
				{
					error = "BCryptCreateHash failed";
					return false;
				}
				return true;
			}

			bool Update(const void* data, size_t size, std::string& error)
			{
				if (size == 0)
					return true;
				if (BCryptHashData(hash, (PUCHAR)data, (ULONG)size, 0) != STATUS_SUCCESS)
				{
					error = "BCryptHashData failed";
					return false;
				}
				return true;
			}

			bool Finish(std::string& outHex, std::string& error)
			{
				if (BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) != STATUS_SUCCESS)
				{
					error = "BCryptFinishHash failed";
					return false;
				}
				outHex = ToHex(digest.data(), digest.size());
				return true;
			}
		};
	}

	std::string Sha256Hex(const void* data, size_t size)
	{
		Sha256Ctx ctx;
		std::string error, hex;
		if (!ctx.Init(error) || !ctx.Update(data, size, error) || !ctx.Finish(hex, error))
			return std::string();
		return hex;
	}

	bool Sha256File(const std::filesystem::path& path, std::string& outHex, std::string& error)
	{
		outHex.clear();
		error.clear();

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open())
		{
			error = "could not open file for hashing";
			return false;
		}

		Sha256Ctx ctx;
		if (!ctx.Init(error))
			return false;

		std::array<char, 64 * 1024> buffer;
		while (file)
		{
			file.read(buffer.data(), (std::streamsize)buffer.size());
			const std::streamsize got = file.gcount();
			if (got > 0 && !ctx.Update(buffer.data(), (size_t)got, error))
				return false;
		}
		if (file.bad())
		{
			error = "read error while hashing file";
			return false;
		}

		return ctx.Finish(outHex, error);
	}
}
