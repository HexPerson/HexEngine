

#include "KeyValues.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{
	bool KeyValues::Parse(DiskFile* file)
	{
		if (file->IsOpen() == false)
		{
			if (file->Open() == false)
			{
				LOG_CRIT("Failed to open DiskFile '%s' for KeyValues parse", file->GetAbsolutePath().c_str());
				return false;
			}
		}

		std::string contents;
		file->ReadAll(contents);

		if (contents.length() == 0)
			return false;

		int32_t coffset = 0;
		for (auto c : contents)
		{
			if (c == '\n')
				_lineEndings.push_back(coffset);

			coffset++;
		}

		size_t offset = 0;
		size_t result = 0;

		bool hasGbufferLines = contents.find("GBUFFER_RESOURCE") != contents.npos;

		while (_readOffset < file->GetSize())
		{
			std::string key;
			if (!FindKey(contents, key))
				break;

			int32_t lineOffset = 0;

			for (auto l : _lineEndings)
			{
				if (l >= _readOffset)
				{
					break;
				}

				lineOffset++;
			}

			std::string value;
			if (!FindValue(contents, value))
				break;

			ValueData valueData;
			valueData.value = value;
			valueData.lineoffset = lineOffset + 2;

			if (hasGbufferLines)
				valueData.lineoffset -= 4;

			_keyValues[key] = valueData;
		}
		
		return true;
	}

	bool KeyValues::Parse(const std::string& data)
	{
		if (data.length() == 0)
			return false;

		int32_t coffset = 0;
		for (auto c : data)
		{
			if (c == '\n')
				_lineEndings.push_back(coffset);

			coffset++;
		}

		size_t offset = 0;
		size_t result = 0;

		bool hasGbufferLines = data.find("GBUFFER_RESOURCE") != data.npos;

		while (_readOffset < data.size())
		{
			std::string key;
			if (!FindKey(data, key))
				break;

			int32_t lineOffset = 0;

			for (auto l : _lineEndings)
			{
				if (l >= _readOffset)
				{
					break;
				}

				lineOffset++;
			}

			std::string value;
			if (!FindValue(data, value))
				break;

			ValueData valueData;
			valueData.value = value;
			valueData.lineoffset = lineOffset + 2;

			if (hasGbufferLines)
				valueData.lineoffset -= 4;

			_keyValues[key] = valueData;
		}

		return true;
	}

	bool KeyValues::FindKey(const std::string& contents, std::string& key)
	{
		size_t start;
		if (start = contents.find_first_of('\"', _readOffset); start == contents.npos)
		{
			return false;
		}

		_readOffset = start + 1;

		size_t end;
		if (end = contents.find_first_of('\"', _readOffset); end == contents.npos)
		{
			return false;
		}

		key = contents.substr(start + 1, end - (start + 1));

		return true;
	}

	bool KeyValues::FindValue(const std::string& contents, std::string& value)
	{
		size_t start;
		if (start = contents.find_first_of('{', _readOffset); start == contents.npos)
		{
			return false;
		}

		_readOffset = start + 1;

		size_t end;
		uint32_t nestCount = 1;

		for(auto i = _readOffset; i < contents.length(); ++i)
		{
			if (contents[i] == '{')
				nestCount++;
			else if (contents[i] == '}')
				nestCount--;

			if (nestCount == 0)
			{
				end = i;
				break;
			}

		}

		value = contents.substr(start + 1, end - (start + 1));

		_readOffset = end + 1;

		return true;
	}

	KeyValues::KvMap& KeyValues::GetKeyValues()
	{
		return _keyValues;
	}

	bool KeyValues::GetLines(const ValueData& data, std::vector<std::string>& lines) const
	{
		std::stringstream ss;
		ss << data.value;

		std::string line;

		while (std::getline(ss, line))
		{
			if (line.length() == 0)
				continue;

			while (line.size() && (line.at(0) == '\t' || line.at(0) == ' '))
				line.erase(line.begin());

			lines.push_back(line);
		}

		return lines.size() > 0;
	}

	void KeyValues::ParseValue(const std::string& line, std::string& variable, std::vector<std::string>& values) const
	{
		size_t offset = 0;

		if (offset = line.find_first_of(" \t="); offset != line.npos)
		{
			variable = line.substr(0, offset);

			std::transform(variable.begin(), variable.end(), variable.begin(), ::tolower);
		}

		while (true)
		{
			if (auto p = line.find_first_not_of(" \t=", offset); p != line.npos)
			{
				// split it again for multi-part values

				std::string value = line.substr(p);

				bool isString = value[0] == '"';

				size_t off = 0;

				if (isString)
				{
					if (auto e = value.find_last_of("\""); e != value.npos)
					{
						value = value.substr(1, e-1);

						off = 2;
					}
				}
				else if (auto e = value.find_first_of(" \t"); e != value.npos)
				{
					value = value.substr(0, e);
				}

				values.push_back(value);

				offset = p + value.length() + off;
			}
			else
				break;
		}
	}

	std::string KeyValues::BuildStringFromArgs(const std::vector<std::string>& values, size_t startIndex)
	{
		if (startIndex >= values.size())
			return values[0];

		std::string ret;

		for (size_t i = startIndex; i < values.size(); ++i)
		{
			ret.append(values[i]);
			ret.append(" ");
		}
		return ret;
	}
}