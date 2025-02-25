
#pragma once

#include "IFile.hpp"

namespace HexEngine
{
	enum class DiskFileOptions
	{
		None			= 0,
		CreateSubDirs	= (1 << 0)
	};

	DEFINE_ENUM_FLAG_OPERATORS(DiskFileOptions);

	class DiskFile : public IFile
	{
	public:
		DiskFile(const fs::path& absolutePath, std::ios_base::openmode openMode, DiskFileOptions options=DiskFileOptions::None);
		DiskFile(const DiskFile& file) = delete;

		virtual ~DiskFile();

		/// <summary>
		/// Open a file
		/// </summary>
		/// <returns></returns>
		virtual bool Open() override;

		/// <summary>
		/// Delete a file
		/// </summary>
		/// <returns></returns>
		virtual bool Delete() override;

		/// <summary>
		/// Determine if a file exists or not
		/// </summary>
		/// <returns></returns>
		virtual bool DoesExist() override;

		/// <summary>
		/// Close the file
		/// </summary>
		/// <returns></returns>
		virtual void Close() override;

		/// <summary>
		/// Get the size of the file
		/// </summary>
		/// <returns></returns>
		virtual uint32_t GetSize() override;

		/// <summary>
		/// Write some data to the file
		/// </summary>
		/// <param name="data">The data to write</param>
		/// <param name="size">How much date to write</param>
		/// <returns></returns>
		virtual uint32_t Write(void* data, uint32_t size) override;

		/// <summary>
		/// Read some data from the file
		/// </summary>
		/// <param name="outData"></param>
		/// <param name="size"></param>
		/// <returns></returns>
		virtual uint32_t Read(void* outData, uint32_t size) override;

		/// <summary>
		/// Flush the file buffers
		/// </summary>
		virtual void Flush() override;

		/// <summary>
		/// Determine if this file is currently open
		/// </summary>
		/// <returns></returns>
		virtual bool IsOpen() const override;

		const fs::path& GetAbsolutePath() const;

		void ReadAll(std::vector<uint8_t>& output);

		void ReadAll(std::string& output);

		void WriteString(const std::string& str);

		std::string ReadString();

		/*template<typename T>
		void Write(T& data)
		{
			Write(&data, sizeof(T));
		}*/


		template<typename T>
		void Write(T data)
		{
			Write((void*)&data, sizeof(T));
		}

		template<>
		void Write(dx::BoundingBox data)
		{
			Write<math::Vector3>(data.Center);
			Write<math::Vector3>(data.Extents);
		}

		template<>
		void Write(dx::BoundingOrientedBox data)
		{
			Write<math::Vector3>(data.Center);
			Write<math::Vector3>(data.Extents);
			Write<math::Vector4>(data.Orientation);
		}

		template<typename T>
		void Write(T* data)
		{
			Write(data, sizeof(T));
		}

		template<typename T>
		T Read()
		{
			T data;
			Read(&data, sizeof(T));
			return data;
		}

		template<typename T>
		void Read(T* data)
		{
			Read(data, sizeof(T));
		}

		template<>
		dx::BoundingBox Read()
		{
			dx::BoundingBox data;
			data.Center = Read<math::Vector3>();
			data.Extents = Read<math::Vector3>();
			return data;
		}

		template<>
		dx::BoundingOrientedBox Read()
		{
			dx::BoundingOrientedBox data;
			data.Center = Read<math::Vector3>();
			data.Extents = Read<math::Vector3>();
			data.Orientation = Read<math::Vector4>();
			return data;
		}

		uint32_t GetRemainingReadSize()
		{
			return GetSize() - (uint32_t)_stream.tellp();
		}

	protected:
		std::fstream _stream;
		std::ios_base::openmode _openMode;
		fs::path _fsPathObj;
	};
}
