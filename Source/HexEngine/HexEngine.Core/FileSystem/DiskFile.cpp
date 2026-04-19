

#include "DiskFile.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	DiskFile::DiskFile(const fs::path& absolutePath, std::ios_base::openmode openMode, DiskFileOptions options) :
		_openMode(openMode),
		_fsPathObj(absolutePath)
	{

		if (HEX_HASFLAG(options,DiskFileOptions::CreateSubDirs))
		{
			g_pEnv->GetFileSystem().CreateSubDirectories(_fsPathObj);
		}
	}

	DiskFile::~DiskFile()
	{
	}

	/// <summary>
	/// Open a file
	/// </summary>
	/// <returns></returns>
	bool DiskFile::Open()
	{
		if (IsOpen())
		{
			//error
			return true;
		}

		_stream.open(_fsPathObj, _openMode);
		
		if (_stream.is_open() == false)
			return false;

		return true;
	}

	/// <summary>
	/// Delete a file
	/// </summary>
	/// <returns></returns>
	bool DiskFile::Delete()
	{
		return fs::remove(_fsPathObj);
	}

	/// <summary>
	/// Determine if a file exists or not
	/// </summary>
	/// <returns></returns>
	bool DiskFile::DoesExist()
	{
		return fs::exists(_fsPathObj);
	}

	/// <summary>
	/// Close the file
	/// </summary>
	/// <returns></returns>
	void DiskFile::Close()
	{
		if(IsOpen())
			_stream.close();
	}

	/// <summary>
	/// Get the size of the file
	/// </summary>
	/// <returns></returns>
	uint32_t DiskFile::GetSize()
	{
		return (uint32_t)fs::file_size(_fsPathObj);
	}

	/// <summary>
	/// Write some data to the file
	/// </summary>
	/// <param name="data">The data to write</param>
	/// <param name="size">How much date to write</param>
	/// <returns></returns>
	uint32_t DiskFile::Write(void* data, uint32_t size)
	{
		if (IsOpen() == false)
			return 0;

		auto previousSize = (uint32_t)_stream.tellp();

		return (uint32_t)_stream.write((const char*)data, size).tellp() - previousSize;
	}

	/// <summary>
	/// Read some data from the file
	/// </summary>
	/// <param name="outData"></param>
	/// <param name="size"></param>
	/// <returns></returns>
	uint32_t DiskFile::Read(void* outData, uint32_t size)
	{
		if (IsOpen() == false)
			return 0;

		return (uint32_t)_stream.read((char*)outData, size).gcount();
	}

	/// <summary>
	/// Flush the file buffers
	/// </summary>
	void DiskFile::Flush()
	{
		if (IsOpen() == false)
			return;

		_stream.flush();
	}

	/// <summary>
	/// Determine if this file is currently open
	/// </summary>
	/// <returns></returns>
	bool DiskFile::IsOpen() const
	{
		return _stream.is_open();
	}

	const fs::path& DiskFile::GetAbsolutePath() const
	{
		return _fsPathObj;
	}

	void DiskFile::ReadAll(std::vector<uint8_t>& output)
	{
		output.resize(this->GetSize());

		Read(output.data(), GetSize());
	}

	void DiskFile::ReadAll(std::string& output)
	{
		output.resize(this->GetSize());

		Read(output.data(), GetSize());
	}

	void DiskFile::WriteString(const std::string& str)
	{
		uint32_t len = (uint32_t)str.length();

		Write(&len, 4);
		Write((void*)str.data(), len);
	}

	std::string DiskFile::ReadString()
	{
		uint32_t len;
		Read(&len, 4);

		std::string str(len, '\0');
		Read(str.data(), len);

		return str;
	}
}