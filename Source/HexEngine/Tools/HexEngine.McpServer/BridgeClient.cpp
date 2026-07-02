
#include "BridgeClient.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace HexEngine
{
namespace Mcp
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

		// RAII HANDLE.
		struct ScopedHandle
		{
			HANDLE h = INVALID_HANDLE_VALUE;
			~ScopedHandle() { if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h); }
			explicit operator bool() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
		};

		bool ProcessAlive(uint32_t pid)
		{
			HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
			if (!h) return false;
			CloseHandle(h);
			return true;
		}

		// Overlapped write of the whole buffer, bounded by timeoutMs.
		bool WriteAll(HANDLE pipe, const std::string& data, unsigned int timeoutMs, std::string& err)
		{
			ScopedHandle evt{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
			if (!evt) { err = "CreateEvent failed"; return false; }
			OVERLAPPED ov{}; ov.hEvent = evt.h;
			DWORD written = 0;
			if (!WriteFile(pipe, data.data(), (DWORD)data.size(), &written, &ov))
			{
				if (GetLastError() != ERROR_IO_PENDING) { err = "WriteFile failed"; return false; }
				if (WaitForSingleObject(evt.h, timeoutMs) != WAIT_OBJECT_0) { CancelIo(pipe); err = "write timed out"; return false; }
				if (!GetOverlappedResult(pipe, &ov, &written, FALSE)) { err = "write overlapped failed"; return false; }
			}
			return written == data.size();
		}

		// Overlapped read of one newline-delimited line, bounded by timeoutMs.
		bool ReadLine(HANDLE pipe, std::string& out, unsigned int timeoutMs, std::string& err)
		{
			ScopedHandle evt{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
			if (!evt) { err = "CreateEvent failed"; return false; }
			const DWORD start = GetTickCount();
			char chunk[4096];
			for (;;)
			{
				OVERLAPPED ov{}; ov.hEvent = evt.h;
				ResetEvent(evt.h);
				DWORD read = 0;
				BOOL ok = ReadFile(pipe, chunk, sizeof(chunk), &read, &ov);
				if (!ok)
				{
					DWORD e = GetLastError();
					if (e != ERROR_IO_PENDING) { err = "ReadFile failed (gle=" + std::to_string(e) + ")"; return false; }
					const DWORD elapsed = GetTickCount() - start;
					const DWORD remaining = (elapsed >= timeoutMs) ? 0 : (timeoutMs - elapsed);
					if (WaitForSingleObject(evt.h, remaining) != WAIT_OBJECT_0) { CancelIo(pipe); err = "read timed out"; return false; }
					if (!GetOverlappedResult(pipe, &ov, &read, FALSE)) { err = "read overlapped failed"; return false; }
				}
				out.append(chunk, read);
				if (auto nl = out.find('\n'); nl != std::string::npos)
				{
					out.resize(nl); // strip newline + anything after (one message per line)
					return true;
				}
				if (read == 0) { err = "pipe closed before newline"; return false; }
				if (GetTickCount() - start > timeoutMs) { err = "read timed out"; return false; }
			}
		}
	}

	std::string SessionDir()
	{
		if (const char* override = std::getenv("HEXENGINE_BRIDGE_SESSION_DIR"))
			return override;
		wchar_t tmp[MAX_PATH];
		DWORD n = GetTempPathW(MAX_PATH, tmp);
		fs::path p = (n > 0) ? fs::path(std::wstring(tmp, n)) : fs::temp_directory_path();
		p /= "HexEngine";
		p /= "EditorBridge";
		return p.string();
	}

	std::vector<SessionInfo> DiscoverSessions()
	{
		std::vector<SessionInfo> sessions;
		fs::path dir(SessionDir());
		std::error_code ec;
		if (!fs::is_directory(dir, ec))
			return sessions;

		for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
		{
			if (!it->is_regular_file(ec)) continue;
			const fs::path& p = it->path();
			if (p.extension() != ".json") continue;
			if (p.filename().string().rfind("session-", 0) != 0) continue;

			std::ifstream in(p, std::ios::binary);
			if (!in.is_open()) continue;
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

			json j;
			try { j = json::parse(text); } catch (...) { continue; }

			SessionInfo s;
			if (!HexEngine::EditorBridge::SessionFromJson(j, s))
				continue;
			if (!ProcessAlive(s.pid))
				continue; // stale session file (editor exited); skip
			sessions.push_back(s);
		}

		std::sort(sessions.begin(), sessions.end(),
			[](const SessionInfo& a, const SessionInfo& b) { return a.startedAtUnix > b.startedAtUnix; });
		return sessions;
	}

	bool CallBridge(const SessionInfo& session, const json& request, json& response,
		std::string& error, unsigned int timeoutMs)
	{
		const std::wstring pipe = Widen(session.pipeName);
		ScopedHandle handle;

		const DWORD start = GetTickCount();
		for (;;)
		{
			handle.h = CreateFileW(pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (handle.h != INVALID_HANDLE_VALUE)
				break;

			const DWORD e = GetLastError();
			if (e != ERROR_PIPE_BUSY)
			{
				error = "cannot connect to editor bridge pipe '" + session.pipeName + "' (gle=" + std::to_string(e) + ")";
				return false;
			}
			const DWORD elapsed = GetTickCount() - start;
			if (elapsed >= timeoutMs) { error = "timed out waiting for a free bridge pipe instance"; return false; }
			const DWORD waitMs = (timeoutMs - elapsed) < 500u ? (timeoutMs - elapsed) : 500u;
			WaitNamedPipeW(pipe.c_str(), waitMs);
		}

		// Authenticate with the session token learned from the (readable) session
		// file, so the bridge accepts the request.
		json authed = request;
		if (!session.token.empty())
			authed["token"] = session.token;
		std::string line = authed.dump();
		line.push_back('\n');
		if (!WriteAll(handle.h, line, timeoutMs, error))
			return false;

		std::string reply;
		if (!ReadLine(handle.h, reply, timeoutMs, error))
			return false;

		try { response = json::parse(reply); }
		catch (const std::exception& e) { error = std::string("bridge returned invalid JSON: ") + e.what(); return false; }
		return true;
	}
}
}
