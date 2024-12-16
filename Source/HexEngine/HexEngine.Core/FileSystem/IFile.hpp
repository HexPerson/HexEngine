

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class IFile
	{
	public:
		/// <summary>
		/// Open a file
		/// </summary>
		/// <returns></returns>
		virtual bool Open() = 0;

		/// <summary>
		/// Delete a file
		/// </summary>
		/// <returns></returns>
		virtual bool Delete() = 0;

		/// <summary>
		/// Determine if a file exists or not
		/// </summary>
		/// <returns></returns>
		virtual bool DoesExist() = 0;

		/// <summary>
		/// Close the file
		/// </summary>
		/// <returns></returns>
		virtual void Close() = 0;

		/// <summary>
		/// Get the size of the file
		/// </summary>
		/// <returns></returns>
		virtual uint32_t GetSize() = 0;

		/// <summary>
		/// Write some data to the file
		/// </summary>
		/// <param name="data">The data to write</param>
		/// <param name="size">How much date to write</param>
		/// <returns></returns>
		virtual uint32_t Write(void* data, uint32_t size) = 0;

		/// <summary>
		/// Read some data from the file
		/// </summary>
		/// <param name="outData"></param>
		/// <param name="size"></param>
		/// <returns></returns>
		virtual uint32_t Read(void* outData, uint32_t size) = 0;

		/// <summary>
		/// Flush the file buffers
		/// </summary>
		virtual void Flush() = 0;

		/// <summary>
		/// Determine if this file is currently open
		/// </summary>
		/// <returns></returns>
		virtual bool IsOpen() const = 0;
	};
}
