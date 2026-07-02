
#include "BridgeServer.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <vector>

namespace HexEngine
{
namespace EditorBridge
{
	namespace
	{
		std::wstring Widen(const std::string& s)
		{
			if (s.empty()) return std::wstring();
			int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
			std::wstring w(n, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
			return w;
		}

		constexpr DWORD kPipeBuffer = 64 * 1024;
		constexpr size_t kMaxRequestBytes = 256 * 1024; // reject oversized requests
	}

	BridgeServer::~BridgeServer()
	{
		Stop();
	}

	bool BridgeServer::Start(const std::string& pipeName, LineHandler handler)
	{
		if (_running.load())
			return true;
		_pipeName = pipeName;
		_handler  = std::move(handler);

		_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual-reset
		if (!_stopEvent)
			return false;

		_running.store(true);
		_thread = std::thread(&BridgeServer::AcceptLoop, this);
		return true;
	}

	void BridgeServer::Stop()
	{
		if (_stopEvent)
			SetEvent((HANDLE)_stopEvent);

		if (_thread.joinable())
			_thread.join();

		_running.store(false);

		if (_stopEvent)
		{
			CloseHandle((HANDLE)_stopEvent);
			_stopEvent = nullptr;
		}
	}

	void BridgeServer::AcceptLoop()
	{
		const std::wstring wpipe = Widen(_pipeName);
		const HANDLE stopEvent = (HANDLE)_stopEvent;

		while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
		{
			// One overlapped pipe instance per client.
			HANDLE pipe = CreateNamedPipeW(
				wpipe.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				1,               // single instance / one client at a time
				kPipeBuffer, kPipeBuffer, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
				break;

			HANDLE connectEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			OVERLAPPED ov{}; ov.hEvent = connectEvt;

			BOOL connected = ConnectNamedPipe(pipe, &ov);
			DWORD err = GetLastError();
			if (!connected && err == ERROR_IO_PENDING)
			{
				HANDLE waits[2] = { connectEvt, stopEvent };
				DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
				if (w != WAIT_OBJECT_0)
				{
					// stop requested (or failure): tear down and exit.
					CancelIo(pipe);
					CloseHandle(connectEvt);
					CloseHandle(pipe);
					break;
				}
			}
			else if (!connected && err != ERROR_PIPE_CONNECTED)
			{
				CloseHandle(connectEvt);
				CloseHandle(pipe);
				continue;
			}

			// --- Serve exactly one request/response, then disconnect. ---
			std::string request;
			bool haveLine = false;
			std::vector<char> chunk(8192);
			while (!haveLine && request.size() < kMaxRequestBytes)
			{
				ResetEvent(connectEvt);
				OVERLAPPED rov{}; rov.hEvent = connectEvt;
				DWORD read = 0;
				BOOL ok = ReadFile(pipe, chunk.data(), (DWORD)chunk.size(), &read, &rov);
				if (!ok)
				{
					if (GetLastError() != ERROR_IO_PENDING) break;
					HANDLE waits[2] = { connectEvt, stopEvent };
					if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) { CancelIo(pipe); break; }
					if (!GetOverlappedResult(pipe, &rov, &read, FALSE)) break;
				}
				if (read == 0) break;
				request.append(chunk.data(), read);
				if (auto nl = request.find('\n'); nl != std::string::npos)
				{
					request.resize(nl);
					haveLine = true;
				}
			}

			if (haveLine && _handler)
			{
				std::string response = _handler(request);
				response.push_back('\n');
				ResetEvent(connectEvt);
				OVERLAPPED wov{}; wov.hEvent = connectEvt;
				DWORD written = 0;
				BOOL ok = WriteFile(pipe, response.data(), (DWORD)response.size(), &written, &wov);
				if (!ok && GetLastError() == ERROR_IO_PENDING)
				{
					HANDLE waits[2] = { connectEvt, stopEvent };
					WaitForMultipleObjects(2, waits, FALSE, INFINITE);
					GetOverlappedResult(pipe, &wov, &written, FALSE);
				}
				FlushFileBuffers(pipe);
			}

			DisconnectNamedPipe(pipe);
			CloseHandle(connectEvt);
			CloseHandle(pipe);
		}
	}
}
}
