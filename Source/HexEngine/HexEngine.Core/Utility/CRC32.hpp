

#pragma once

#define CRC32_POLYNOMIAL 0xEDB88320

namespace HexEngine
{
	constexpr ULONG ConstCRC32_ComputeTableEntry(ULONG Index, ULONG Loop = 0)
	{
		return (Loop < 8 ? ConstCRC32_ComputeTableEntry((Index >> 1) ^ (-int(Index & 1) & CRC32_POLYNOMIAL), Loop + 1) : Index);
	}

	template<size_t N> constexpr
		ULONG ConstCRC32_ProcessBuffer(ULONG Value, const CHAR(&String)[N], ULONG Index)
	{
		return (Index < N - 1 ? ConstCRC32_ProcessBuffer((Value >> 8) ^ ConstCRC32_ComputeTableEntry((Value ^ String[Index]) & 0xFF), String, Index + 1) : Value);
	}

	template<size_t N> constexpr
		ULONG _ConstCRC32(const CHAR(&String)[N])
	{
		return ~ConstCRC32_ProcessBuffer(0xFFFFFFFF, String, 0);
	}

	class CRC32
	{
	public:
		__forceinline CRC32() :
			m_dwCrc32(0xFFFFFFFF)
		{ }

		__forceinline void Add(PVOID Data, SIZE_T Size)
		{
			for (SIZE_T i = 0; i < Size; i++)
			{
				CHAR data[2] = { ((PCHAR)Data)[i], 0 };
				m_dwCrc32 = ConstCRC32_ProcessBuffer(m_dwCrc32, data, 0);
			}
		}

		template<typename T>
		__forceinline void Add(T* Data)
		{
			Add((PVOID)Data, sizeof(T));
		}

		__forceinline ULONG Get() const
		{
			return ~m_dwCrc32;
		}

	private:
		ULONG	m_dwCrc32;

	public:
		template<typename T>
		__forceinline static ULONG HashString(const T* String)
		{
			CRC32 crc;

			while (*String)
				crc.Add((PCH)String++);

			return crc.Get();
		}
	};

	template<ULONG V>
	struct Crc32ForceStatic
	{
		static const ULONG value = V;
	};

#define ConstCRC32(x) HexEngine::Crc32ForceStatic<HexEngine::_ConstCRC32(x)>::value
}
