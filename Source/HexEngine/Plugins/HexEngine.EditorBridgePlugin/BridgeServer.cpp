
#include "BridgeServer.hpp"
#include "EditorBridgeProtocol.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
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

		constexpr DWORD kPipeBuffer = 256 * 1024;

		// Wait for an overlapped op on `pipe` (event `evt`) or the stop event.
		// Returns false if stop was signalled or the op failed.
		bool WaitIo(HANDLE pipe, HANDLE evt, HANDLE stopEvent, OVERLAPPED& ov, DWORD& transferred)
		{
			HANDLE waits[2] = { evt, stopEvent };
			if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
			{
				CancelIo(pipe);
				// Let the cancelled op complete so `ov` isn't referenced after return.
				GetOverlappedResult(pipe, &ov, &transferred, TRUE);
				return false;
			}
			return GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE;
		}

		bool WriteAll(HANDLE pipe, HANDLE evt, HANDLE stopEvent, const std::string& data)
		{
			size_t offset = 0;
			while (offset < data.size())
			{
				ResetEvent(evt);
				OVERLAPPED ov{}; ov.hEvent = evt;
				DWORD written = 0;
				const DWORD chunk = (DWORD)std::min<size_t>(data.size() - offset, 1u << 20);
				if (!WriteFile(pipe, data.data() + offset, chunk, &written, &ov))
				{
					if (GetLastError() != ERROR_IO_PENDING) return false;
					if (!WaitIo(pipe, evt, stopEvent, ov, written)) return false;
				}
				if (written == 0) return false;
				offset += written;
			}
			return true;
		}
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

		ReapFinishedClients(true);

		_running.store(false);

		if (_stopEvent)
		{
			CloseHandle((HANDLE)_stopEvent);
			_stopEvent = nullptr;
		}
	}

	void BridgeServer::ReapFinishedClients(bool all)
	{
		std::list<std::unique_ptr<Client>> finished;
		{
			std::lock_guard<std::mutex> lk(_clientsMutex);
			for (auto it = _clients.begin(); it != _clients.end();)
			{
				if (all || (*it)->done.load())
				{
					finished.push_back(std::move(*it));
					it = _clients.erase(it);
				}
				else
					++it;
			}
		}
		for (auto& c : finished)
			if (c->thread.joinable())
				c->thread.join();
	}

	void BridgeServer::AcceptLoop()
	{
		const std::wstring wpipe = Widen(_pipeName);
		const HANDLE stopEvent = (HANDLE)_stopEvent;

		while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
		{
			ReapFinishedClients(false);

			HANDLE pipe = CreateNamedPipeW(
				wpipe.c_str(),
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				kMaxClients,
				kPipeBuffer, kPipeBuffer, 0, nullptr);
			if (pipe == INVALID_HANDLE_VALUE)
			{
				// All instances busy (or a transient failure): back off briefly
				// and retry rather than killing the bridge.
				if (WaitForSingleObject(stopEvent, 100) == WAIT_OBJECT_0)
					break;
				continue;
			}

			HANDLE connectEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			OVERLAPPED ov{}; ov.hEvent = connectEvt;

			BOOL connected = ConnectNamedPipe(pipe, &ov);
			DWORD err = GetLastError();
			if (!connected && err == ERROR_IO_PENDING)
			{
				DWORD unused = 0;
				if (!WaitIo(pipe, connectEvt, stopEvent, ov, unused))
				{
					CloseHandle(connectEvt);
					CloseHandle(pipe);
					if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0)
						break;
					continue;
				}
			}
			else if (!connected && err != ERROR_PIPE_CONNECTED)
			{
				CloseHandle(connectEvt);
				CloseHandle(pipe);
				continue;
			}
			CloseHandle(connectEvt);

			// Hand the connection to its own serving thread and immediately
			// create the next instance so other clients can connect.
			auto client = std::make_unique<Client>();
			Client* raw = client.get();
			{
				std::lock_guard<std::mutex> lk(_clientsMutex);
				_clients.push_back(std::move(client));
			}
			raw->thread = std::thread(&BridgeServer::ServeClient, this, (void*)pipe, raw);
		}
	}

	void BridgeServer::ServeClient(void* pipeHandle, Client* self)
	{
		const HANDLE pipe = (HANDLE)pipeHandle;
		const HANDLE stopEvent = (HANDLE)_stopEvent;
		HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);

		std::string buffer;          // bytes received but not yet consumed
		std::vector<char> chunk(64 * 1024);
		bool alive = (evt != nullptr);

		while (alive && WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
		{
			// Serve every complete line already buffered (pipelined requests).
			size_t nl;
			while (alive && (nl = buffer.find('\n')) != std::string::npos)
			{
				std::string line = buffer.substr(0, nl);
				buffer.erase(0, nl + 1);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty()) continue;

				std::string response = _handler ? _handler(line) : std::string("{}");
				response.push_back('\n');
				alive = WriteAll(pipe, evt, stopEvent, response);
			}
			if (!alive) break;

			if (buffer.size() > kMaxRequestBytes)
			{
				const std::string tooBig = MakeError(0, ErrorCode::InvalidRequest,
					"request exceeded the " + std::to_string(kMaxRequestBytes) + "-byte cap").dump() + "\n";
				WriteAll(pipe, evt, stopEvent, tooBig);
				break;
			}

			ResetEvent(evt);
			OVERLAPPED rov{}; rov.hEvent = evt;
			DWORD read = 0;
			if (!ReadFile(pipe, chunk.data(), (DWORD)chunk.size(), &read, &rov))
			{
				if (GetLastError() != ERROR_IO_PENDING)
					break; // client disconnected
				if (!WaitIo(pipe, evt, stopEvent, rov, read))
					break;
			}
			if (read == 0)
				break;
			buffer.append(chunk.data(), read);
		}

		// No FlushFileBuffers: every exit path is either the client hanging up,
		// a failed write, or a stop request, and flushing to a client that has
		// stopped reading would block Stop() forever.
		DisconnectNamedPipe(pipe);
		CloseHandle(pipe);
		if (evt) CloseHandle(evt);
		self->done.store(true);
	}
}
}
