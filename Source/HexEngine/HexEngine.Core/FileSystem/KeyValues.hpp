
#pragma once

#include "DiskFile.hpp"

namespace HexEngine
{
	class KeyValues
	{
	public:
		struct ValueData
		{
			int32_t lineoffset;
			std::string value;
		};

		typedef std::unordered_map<std::string, ValueData> KvMap;
	
		bool Parse(DiskFile* file);

		bool Parse(const std::string& data);

		KvMap& GetKeyValues();

		/// <summary>
		/// Parse a block of key Values and outputs a vector of strings representing each line of the file
		/// </summary>
		/// <param name="data"></param>
		/// <param name="lines"></param>
		/// <returns></returns>
		bool GetLines(const ValueData& data, std::vector<std::string>& lines) const;

		/// <summary>
		/// Parses a line of text and outputs the variable name along with its' values
		/// </summary>
		void ParseValue(const std::string& line, std::string& variable, std::vector<std::string>& values) const;

		std::string BuildStringFromArgs(const std::vector<std::string>& values, size_t startIndex = 0);

	private:
		bool FindKey(const std::string& contents, std::string& key);

		bool FindValue(const std::string& contents, std::string& value);

	private:
		KvMap _keyValues;
		size_t _readOffset = 0;
		std::vector<int32_t> _lineEndings;
	};
}
