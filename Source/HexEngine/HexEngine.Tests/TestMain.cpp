// Minimal, dependency-free unit-test harness for HexEngine hardening work.
// Compiles the pure sources under test directly (no heavy Core link). PR6 will
// formalize this project (CI integration, more coverage, static analysis).

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

#include <thread>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../HexEngine.Core/Plugin/PluginManifest.hpp"
#include "../HexEngine.Core/Utility/Sha256.hpp"
#include "../HexEngine.Core/FileSystem/BinaryReader.hpp"
#include "../HexEngine.Core/Utility/BlockingQueue.hpp"
#include "../HexEngine.Core/FileSystem/PackageVerification.hpp"
#include "../HexEngine.Core/FileSystem/PackageManifest.hpp"
#include "../Plugins/HexEngine.EditorBridgePlugin/EditorBridgeProtocol.hpp"
#include "../Tools/HexEngine.McpServer/StaticTools.hpp"
#include "../Tools/HexEngine.McpServer/BridgeClient.hpp"
#include "../HexEngine.Core/Scene/OceanWaveModel.hpp"
#include "../HexEngine.Core/Physics/RayMath.hpp"

using namespace HexEngine;

static int g_total = 0;
static int g_fail = 0;

#define CHECK(cond) do { ++g_total; if(!(cond)) { ++g_fail; std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); } } while(0)

static void TestManifestParse()
{
	PluginManifest m; std::string err;

	CHECK(ParsePluginManifest(R"({"plugins":[{"name":"P","module":"P.dll"}]})", m, err));
	CHECK(m.entries.size() == 1);
	CHECK(m.entries.size() == 1 && m.entries[0].module == "P.dll");
	CHECK(m.entries.size() == 1 && m.entries[0].enabled == true);

	CHECK(!ParsePluginManifest("{ not json ", m, err));                    // malformed JSON
	CHECK(!ParsePluginManifest(R"({"nope":1})", m, err));                  // missing 'plugins'
	CHECK(!ParsePluginManifest(R"({"plugins":42})", m, err));              // 'plugins' not an array
	CHECK(!ParsePluginManifest(R"({"plugins":[{"name":"x"}]})", m, err));  // entry missing 'module'
}

static void TestEvaluate()
{
	PluginManifest m; std::string err;
	ParsePluginManifest(R"({"plugins":[
		{"name":"Good","module":"Good.dll","enabled":true,"sha256":"ABCD"},
		{"name":"Off","module":"Off.dll","enabled":false},
		{"name":"NoHash","module":"NoHash.dll","enabled":true}
	]})", m, err);

	const std::string good = "abcd"; // matches "ABCD" case-insensitively
	const std::string bad  = "0000";

	// Developer: permissive.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Unlisted.dll", nullptr) == PluginLoadDecision::Allow);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Off.dll",      nullptr) == PluginLoadDecision::Reject_Disabled);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     nullptr) == PluginLoadDecision::Allow);            // skips uncomputed hash
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     &bad)    == PluginLoadDecision::Reject_HashMismatch); // but rejects a real mismatch
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     &good)   == PluginLoadDecision::Allow);

	// Production: fail closed.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Unlisted.dll", &good)   == PluginLoadDecision::Reject_NotInManifest);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Off.dll",      &good)   == PluginLoadDecision::Reject_Disabled);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     nullptr) == PluginLoadDecision::Reject_HashUnverified);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     &bad)    == PluginLoadDecision::Reject_HashMismatch);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     &good)   == PluginLoadDecision::Allow);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "NoHash.dll",   nullptr) == PluginLoadDecision::Allow); // listed+enabled, no hash pinned

	// Case-insensitive module matching.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "good.DLL",     &good)   == PluginLoadDecision::Allow);
}

static void TestSha256()
{
	// Known NIST vectors.
	CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(Sha256Hex("", 0)    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// --- BinaryReader ---------------------------------------------------------

// Little-endian u32 as 4 bytes, matching how DiskFile::WriteString / the loaders
// emit length prefixes on this (x64, little-endian) target.
static void PushU32(std::vector<uint8_t>& v, uint32_t x)
{
	v.push_back((uint8_t)(x & 0xFF));
	v.push_back((uint8_t)((x >> 8) & 0xFF));
	v.push_back((uint8_t)((x >> 16) & 0xFF));
	v.push_back((uint8_t)((x >> 24) & 0xFF));
}

static void PushStr(std::vector<uint8_t>& v, const std::string& s)
{
	PushU32(v, (uint32_t)s.size());
	v.insert(v.end(), s.begin(), s.end());
}

static void TestBinaryReaderBasics()
{
	// POD reads advance the cursor and stop cleanly at EOF.
	std::vector<uint8_t> buf;
	PushU32(buf, 0xDEADBEEF);
	buf.push_back(0x2A); // one trailing byte

	BinaryReader r(buf);
	CHECK(r.Size() == 5);
	CHECK(r.Remaining() == 5);

	uint32_t u = 0;
	CHECK(r.Read(u) && u == 0xDEADBEEFu);
	CHECK(r.Position() == 4 && r.Remaining() == 1);

	uint8_t b = 0;
	CHECK(r.Read(b) && b == 0x2A);
	CHECK(r.Eof() && r.Remaining() == 0 && r.Good());

	// Reading past the end fails, latches, and leaves the cursor at EOF.
	uint8_t after = 0;
	CHECK(!r.Read(after));
	CHECK(!r.Good());                    // sticky failure
	CHECK(!r.Read(u));                   // subsequent reads no-op

	// ReadOr returns the fallback (and latches) when short.
	BinaryReader r2(buf.data(), 2);      // only 2 bytes -> can't read a u32
	CHECK(r2.ReadOr<uint32_t>(0x1234) == 0x1234u);
	CHECK(!r2.Good());
}

static void TestBinaryReaderBytesAndSeek()
{
	std::vector<uint8_t> buf = { 1,2,3,4,5,6,7,8 };
	BinaryReader r(buf);

	uint8_t dst[4] = {};
	CHECK(r.ReadBytes(dst, 4) && dst[0] == 1 && dst[3] == 4);
	CHECK(r.Position() == 4);

	// Asking for more than remains fails without copying / advancing past end.
	uint8_t big[8] = {};
	CHECK(!r.ReadBytes(big, 8));
	CHECK(!r.Good());

	// Seek recovers position; out-of-range seek latches.
	BinaryReader r2(buf);
	CHECK(r2.Seek(6) && r2.Position() == 6 && r2.Remaining() == 2);
	CHECK(!r2.Seek(9));                  // past end
	CHECK(!r2.Good());

	// Skip respects bounds.
	BinaryReader r3(buf);
	CHECK(r3.Skip(8) && r3.Eof());
	BinaryReader r4(buf);
	CHECK(!r4.Skip(9));

	// ReadInPlace hands back a pointer into the span without copying.
	BinaryReader r5(buf);
	const uint8_t* p = r5.ReadInPlace(3);
	CHECK(p != nullptr && p[0] == 1 && p[2] == 3 && r5.Position() == 3);
	CHECK(r5.ReadInPlace(100) == nullptr);
}

static void TestBinaryReaderString()
{
	// Valid round-trip.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "material/brick.hmat");
		PushU32(buf, 7); // trailing extra field, proves the cursor stops after the string
		BinaryReader r(buf);
		std::string s;
		CHECK(r.ReadString(s) && s == "material/brick.hmat");
		uint32_t tail = 0;
		CHECK(r.Read(tail) && tail == 7);
	}

	// Zero-length string is valid and consumes only the prefix.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "");
		BinaryReader r(buf);
		std::string s = "dirty";
		CHECK(r.ReadString(s) && s.empty() && r.Eof());
	}

	// Length exceeding the bytes actually present -> fail, no over-read/alloc.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0xFFFFFFFFu);       // claims ~4 GB
		buf.push_back('h'); buf.push_back('i');
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s));
		CHECK(s.empty() && !r.Good());
	}

	// Length within the span but above an explicit caller cap -> fail.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "abcdefghij");       // 10 bytes
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s, 4));        // cap of 4 rejects a 10-byte string
		CHECK(!r.Good());
	}

	// Truncated prefix (fewer than 4 bytes) -> fail, not a read of garbage length.
	{
		std::vector<uint8_t> buf = { 0x05, 0x00 };
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s));
		CHECK(!r.Good());
	}
}

static void TestBinaryReaderCount()
{
	// Valid count that fits the remaining bytes.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 3);                  // 3 elements
		for (int i = 0; i < 3 * 8; ++i) buf.push_back((uint8_t)i); // 3 * 8-byte elems
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(r.ReadCount(n, 8) && n == 3);
		CHECK(r.Remaining() == 24);
	}

	// Bogus huge count in a tiny buffer -> fail before any allocation.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0x10000000u);        // 268M elements
		buf.push_back(0); buf.push_back(0); // only 2 payload bytes
		BinaryReader r(buf);
		uint32_t n = 123;
		CHECK(!r.ReadCount(n, 16));        // 268M * 16 bytes >> remaining
		CHECK(n == 0 && !r.Good());
	}

	// count * elementSize must not wrap: a count just over the remaining budget fails.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 5);
		for (int i = 0; i < 4; ++i) buf.push_back(0); // only room for 4 one-byte elems
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 1));         // claims 5, only 4 remain
		CHECK(!r.Good());
	}

	// Count above an explicit max cap fails even if bytes would fit.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 10);
		for (int i = 0; i < 10; ++i) buf.push_back(0);
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 1, /*maxCount*/ 4));
		CHECK(!r.Good());
	}

	// Zero elementSize is a programming error and is rejected.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 1);
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 0));
	}
}

// Mirrors the AssetFile V1 block walk: a stream of {fixed record header, then
// `size` inline payload bytes}. The hardened walk uses ReadInPlace(headerSize) +
// Skip(size); a record whose size runs past the buffer must be rejected instead
// of walking the cursor out of bounds (the original OOB heap read).
static bool WalkPackedBlocks(const std::vector<uint8_t>& buf, uint32_t numBlocks, size_t headerSize, int& outWalked)
{
	outWalked = 0;
	BinaryReader r(buf);
	for (uint32_t i = 0; i < numBlocks; ++i)
	{
		const uint8_t* hdr = r.ReadInPlace(headerSize);
		if (!hdr)
			return false;                 // header runs past end
		uint32_t size = 0;
		std::memcpy(&size, hdr, sizeof(size)); // first field of the record is the payload size
		if (!r.Skip(size))
			return false;                 // payload runs past end
		++outWalked;
	}
	return true;
}

static void TestBinaryReaderPackedWalk()
{
	const size_t headerSize = 8; // {uint32 size; uint32 pad;}

	// Two well-formed blocks: sizes 3 and 2.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 3); PushU32(buf, 0); buf.insert(buf.end(), { 'a','b','c' });
		PushU32(buf, 2); PushU32(buf, 0); buf.insert(buf.end(), { 'd','e' });
		int walked = 0;
		CHECK(WalkPackedBlocks(buf, 2, headerSize, walked));
		CHECK(walked == 2);
	}

	// Malicious: header claims a 4 GB payload in a tiny buffer -> rejected, and
	// the walk stops without ever dereferencing out of bounds.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0xFFFFFFFFu); PushU32(buf, 0); buf.insert(buf.end(), { 'x','y' });
		int walked = 0;
		CHECK(!WalkPackedBlocks(buf, 1, headerSize, walked));
		CHECK(walked == 0);
	}

	// Count claims more blocks than the buffer holds -> second header read fails.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 1); PushU32(buf, 0); buf.insert(buf.end(), { 'z' });
		int walked = 0;
		CHECK(!WalkPackedBlocks(buf, 5, headerSize, walked)); // asks for 5, only 1 present
		CHECK(walked == 1);
	}
}

// --- BlockingQueue --------------------------------------------------------

static void TestBlockingQueueBasics()
{
	BlockingQueue<int> q;
	CHECK(q.Empty() && q.Size() == 0 && !q.IsShutdown());

	q.Push(1); q.Push(2); q.Push(3);
	CHECK(q.Size() == 3);

	int v = 0;
	// FIFO order.
	CHECK(q.WaitPop(v) && v == 1);
	CHECK(q.WaitPop(v) && v == 2);
	CHECK(q.TryPop(v) && v == 3);
	CHECK(!q.TryPop(v));            // now empty
	CHECK(q.Empty());

	// Shutdown makes WaitPop return false and refuses new work.
	q.Shutdown();
	CHECK(q.IsShutdown());
	q.Push(99);                     // ignored while shut down
	CHECK(q.Empty());
	int untouched = 42;
	CHECK(!q.WaitPop(untouched) && untouched == 42);
}

static void TestBlockingQueueConcurrent()
{
	// Multi-producer / multi-consumer: every produced item is consumed exactly
	// once, and Shutdown() reliably wakes idle consumers so nothing hangs.
	BlockingQueue<int> q;
	const int producers = 4;
	const int perProducer = 1000;
	const int total = producers * perProducer;

	std::atomic<int> consumedCount{ 0 };
	std::atomic<long long> consumedSum{ 0 };

	std::vector<std::thread> consumers;
	for (int c = 0; c < 3; ++c)
	{
		consumers.emplace_back([&]
		{
			int v = 0;
			while (q.WaitPop(v))
			{
				consumedCount.fetch_add(1, std::memory_order_relaxed);
				consumedSum.fetch_add(v, std::memory_order_relaxed);
			}
		});
	}

	long long expectedSum = 0;
	std::vector<std::thread> prods;
	for (int p = 0; p < producers; ++p)
	{
		for (int i = 0; i < perProducer; ++i)
			expectedSum += (p * perProducer + i);

		prods.emplace_back([&, p]
		{
			for (int i = 0; i < perProducer; ++i)
				q.Push(p * perProducer + i);
		});
	}

	for (auto& t : prods) t.join();

	// Wait for consumers to drain everything before shutting down (Shutdown
	// drops any still-queued items, so we must let them all be consumed first).
	while (consumedCount.load(std::memory_order_relaxed) < total || !q.Empty())
		std::this_thread::yield();

	q.Shutdown();
	for (auto& t : consumers) t.join();  // must not hang - Shutdown woke them

	CHECK(consumedCount.load() == total);
	CHECK(consumedSum.load() == expectedSum);
}

// --- DirectoryWatcher shutdown contract -----------------------------------

static void TestDirectoryWatchStopJoin()
{
	// Mirrors FileSystem's watcher shutdown: a worker blocks on
	// WaitForMultipleObjects({completion, stop}, INFINITE); signalling the
	// manual-reset stop event must wake it so the owning thread's join()
	// returns instead of hanging (the whole point of moving off a detached
	// thread + synchronous ReadDirectoryChangesW).
	HANDLE completion = CreateEventW(NULL, FALSE, FALSE, NULL); // auto-reset
	HANDLE stop       = CreateEventW(NULL, TRUE,  FALSE, NULL); // manual-reset
	CHECK(completion != NULL && stop != NULL);

	std::atomic<bool> exited{ false };
	std::atomic<int>  iterations{ 0 };
	std::thread worker([&]
	{
		HANDLE handles[2] = { completion, stop };
		while (true)
		{
			iterations.fetch_add(1, std::memory_order_relaxed);
			DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
			if (r != WAIT_OBJECT_0)   // stop signalled (WAIT_OBJECT_0+1) or failure
				break;
			// completion signalled -> re-arm (loop again)
		}
		exited.store(true, std::memory_order_relaxed);
	});

	// Poke completion twice to prove the loop re-arms and does NOT exit early...
	SetEvent(completion);
	SetEvent(completion);
	// ...then request stop and join. This must return promptly, not hang.
	SetEvent(stop);
	worker.join();

	CHECK(exited.load(std::memory_order_relaxed));
	CHECK(iterations.load(std::memory_order_relaxed) >= 1);

	CloseHandle(completion);
	CloseHandle(stop);
}

// --- Package verification -------------------------------------------------

static void TestPackageBlobBounds()
{
	// A realistic V2 layout: 64-byte header, 2 TOC entries (dataStart = 64+X),
	// file is 1000 bytes.
	const uint64_t dataStart = 300;
	const uint64_t fileSize  = 1000;
	const uint64_t maxUncmp  = kDefaultMaxUncompressedAssetBytes;

	// Valid blob wholly inside the data section.
	CHECK(IsPackageBlobInBounds(/*off*/300, /*csize*/200, /*usize*/500, dataStart, fileSize, maxUncmp));
	// Blob ending exactly at EOF is fine.
	CHECK(IsPackageBlobInBounds(800, 200, 200, dataStart, fileSize, maxUncmp));
	// Zero-size blob at a valid offset is fine.
	CHECK(IsPackageBlobInBounds(500, 0, 0, dataStart, fileSize, maxUncmp));

	// Offset inside the header/TOC region -> reject (would let a blob overlap
	// metadata).
	CHECK(!IsPackageBlobInBounds(299, 10, 10, dataStart, fileSize, maxUncmp));
	// Offset past EOF -> reject.
	CHECK(!IsPackageBlobInBounds(1001, 0, 0, dataStart, fileSize, maxUncmp));
	// Blob runs off the end of the file -> reject.
	CHECK(!IsPackageBlobInBounds(900, 200, 200, dataStart, fileSize, maxUncmp));
	// offset + size overflow attempt (huge size) -> reject, no wraparound.
	CHECK(!IsPackageBlobInBounds(400, 0xFFFFFFFFFFFFFF00ull, 0, dataStart, fileSize, maxUncmp));
	// Decompression bomb: claimed uncompressed size over the cap -> reject.
	CHECK(!IsPackageBlobInBounds(300, 10, maxUncmp + 1, dataStart, fileSize, maxUncmp));
}

static void TestPackageManifestRoundtrip()
{
	PackageManifest m;
	m.version = 1;
	m.packageFileName = "GameData.pkg";
	m.packageSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	m.packageSize = 123456;

	std::string text = SerializePackageManifest(m);

	PackageManifest parsed; std::string err;
	CHECK(ParsePackageManifest(text, parsed, err));
	CHECK(parsed.version == 1);
	CHECK(parsed.packageFileName == "GameData.pkg");
	CHECK(parsed.packageSha256 == m.packageSha256);
	CHECK(parsed.packageSize == 123456);

	// Case-insensitive hash comparison (packer/reader may differ in case).
	CHECK(HashesEqual("ABCDEF", "abcdef"));
	CHECK(!HashesEqual("abcdef", "abcde0"));
	CHECK(!HashesEqual("", ""));                 // empty never matches

	// Rejections.
	PackageManifest bad; std::string e2;
	CHECK(!ParsePackageManifest("{ not json", bad, e2));                      // malformed
	CHECK(!ParsePackageManifest(R"({"package":{"sha256":"x"}})", bad, e2));   // missing version
	CHECK(!ParsePackageManifest(R"({"version":1})", bad, e2));                // missing package obj
	CHECK(!ParsePackageManifest(R"({"version":1,"package":{"sha256":"tooshort"}})", bad, e2)); // sha not 64 chars
}

// --- Editor bridge protocol ------------------------------------------------

static void TestBridgeProtocol()
{
	using namespace HexEngine::EditorBridge;
	Request r; std::string err;

	// Valid request (with + without params).
	CHECK(ParseRequestText(R"({"id":1,"method":"get_editor_status","params":{}})", r, err) && r.id == 1 && r.method == "get_editor_status");
	CHECK(ParseRequestText(R"({"id":2,"method":"list_entities"})", r, err) && r.params.is_object());

	// Rejections: invalid JSON, missing method, missing id, non-integer id, bad params.
	CHECK(!ParseRequestText("{ not json", r, err));
	CHECK(!ParseRequestText(R"({"id":3})", r, err));
	CHECK(!ParseRequestText(R"({"method":"x"})", r, err));
	CHECK(!ParseRequestText(R"({"id":"x","method":"y"})", r, err));
	CHECK(!ParseRequestText(R"({"id":4,"method":""})", r, err));
	CHECK(!ParseRequestText(R"({"id":5,"method":"y","params":[]})", r, err));

	// Response envelope shapes.
	auto ok = MakeResult(7, EditorBridge::json{ {"a", 1} });
	CHECK(ok["ok"] == true && ok["id"] == 7 && ok["result"]["a"] == 1);
	auto e = MakeError(8, ErrorCode::UnknownMethod, "no");
	CHECK(e["ok"] == false && e["error"]["code"] == "UnknownMethod");

	// Unknown method rejects (via a minimal dispatcher over the parsed request).
	auto dispatch = [](const std::string& line) -> EditorBridge::json {
		Request rq; std::string er;
		if (!ParseRequestText(line, rq, er)) return MakeError(0, ErrorCode::InvalidRequest, er);
		if (rq.method == "known") return MakeResult(rq.id, EditorBridge::json::object());
		return MakeError(rq.id, ErrorCode::UnknownMethod, "no bridge method named " + rq.method);
	};
	CHECK(dispatch(R"({"id":1,"method":"known"})")["ok"] == true);
	CHECK(dispatch(R"({"id":1,"method":"nope"})")["error"]["code"] == "UnknownMethod");

	// --- Phase 3: per-session auth token ---
	CHECK(ParseRequestText(R"({"id":1,"method":"m","token":"abc"})", r, err) && r.token == "abc");
	CHECK(!ParseRequestText(R"({"id":1,"method":"m","token":123})", r, err)); // token must be a string
	CHECK(TokensMatch("deadbeef", "deadbeef"));
	CHECK(!TokensMatch("deadbeef", "deadbee0"));
	CHECK(!TokensMatch("", ""));           // empty never matches
	CHECK(!TokensMatch("abc", "abcd"));    // length mismatch

	// Session round-trip preserves the token.
	SessionInfo si; si.pid = 42; si.pipeName = "\\\\.\\pipe\\x"; si.token = "sekret"; si.startedAtUnix = 7;
	SessionInfo si2;
	CHECK(SessionFromJson(SessionToJson(si), si2) && si2.token == "sekret" && si2.pid == 42);

	// An auth-gated dispatcher rejects a missing/wrong token, accepts the right one.
	const std::string serverToken = "sekret";
	auto authed = [&](const std::string& line) -> EditorBridge::json {
		Request rq; std::string er;
		if (!ParseRequestText(line, rq, er)) return MakeError(0, ErrorCode::InvalidRequest, er);
		if (!TokensMatch(rq.token, serverToken)) return MakeError(rq.id, ErrorCode::Unauthorized, "no");
		return MakeResult(rq.id, EditorBridge::json::object());
	};
	CHECK(authed(R"({"id":1,"method":"m"})")["error"]["code"] == "Unauthorized");                 // missing
	CHECK(authed(R"({"id":1,"method":"m","token":"wrong"})")["error"]["code"] == "Unauthorized");  // wrong
	CHECK(authed(R"({"id":1,"method":"m","token":"sekret"})")["ok"] == true);                      // right

	// --- v2: entity references ---
	{
		using J = EditorBridge::json;
		EntityRef ref; std::string er;
		CHECK(ParseEntityRefValue(J("Crate"), ref, er) && ref.kind == EntityRef::Kind::Name && ref.name == "Crate");
		CHECK(ParseEntityRefValue(J(42), ref, er) && ref.kind == EntityRef::Kind::Index && ref.index == 42);
		CHECK(ParseEntityRefValue(J::parse(R"({"index":7,"generation":3})"), ref, er) && ref.kind == EntityRef::Kind::Id && ref.index == 7 && ref.generation == 3);
		CHECK(ParseEntityRefValue(J::parse(R"({"index":7})"), ref, er) && ref.kind == EntityRef::Kind::Index && ref.index == 7);
		CHECK(!ParseEntityRefValue(J(""), ref, er));                                   // empty name
		CHECK(!ParseEntityRefValue(J(-1), ref, er));                                   // negative index
		CHECK(!ParseEntityRefValue(J(true), ref, er));                                 // wrong type
		CHECK(!ParseEntityRefValue(J::parse(R"({"index":7,"generation":-1})"), ref, er));
		CHECK(!ParseEntityRefValue(J::parse(R"({"name":"x"})"), ref, er));           // object without index

		// Primary key wins; legacy keys only when allowed.
		CHECK(GetEntityRef(J::parse(R"({"entity":"A","name":"B"})"), "entity", true, ref, er) && ref.name == "A");
		CHECK(GetEntityRef(J::parse(R"({"name":"B"})"), "entity", true, ref, er) && ref.name == "B");
		CHECK(GetEntityRef(J::parse(R"({"index":5})"), "entity", true, ref, er) && ref.kind == EntityRef::Kind::Index);
		CHECK(GetEntityRef(J::parse(R"({"id":{"index":5,"generation":1}})"), "entity", true, ref, er) && ref.kind == EntityRef::Kind::Id);
		CHECK(!GetEntityRef(J::parse(R"({"name":"B"})"), "entity", false, ref, er));  // legacy disabled
		CHECK(!GetEntityRef(J::object(), "entity", true, ref, er));                    // nothing present
	}

	// --- v2: batch $ref resolution ---
	{
		using J = EditorBridge::json;
		std::vector<J> results = {
			J::parse(R"({"id":{"index":10,"generation":2},"name":"House"})"),
			J(nullptr),                                                   // op 1 failed
			J::parse(R"({"entities":[{"name":"a"},{"name":"b"}]})"),
		};
		BatchLabels labels{ {"house", 0}, {"broken", 1} };
		std::string er;

		J p = J::parse(R"({"entity":{"$ref":"0.id"},"parent":{"$ref":"house.name"},"deep":[{"$ref":"2.entities.1.name"}]})");
		CHECK(ResolveBatchRefs(p, results, 3, labels, er));
		CHECK(p["entity"]["index"] == 10 && p["entity"]["generation"] == 2);
		CHECK(p["parent"] == "House");
		CHECK(p["deep"][0] == "b");

		J whole = J::parse(R"({"x":{"$ref":"0"}})");
		CHECK(ResolveBatchRefs(whole, results, 3, labels, er) && whole["x"]["name"] == "House");

		J notARef = J::parse(R"({"x":{"$ref":"0.id","extra":1}})");             // 2 keys -> left alone
		CHECK(ResolveBatchRefs(notARef, results, 3, labels, er) && notARef["x"].contains("extra"));

		J forward = J::parse(R"({"x":{"$ref":"2.entities"}})");
		CHECK(!ResolveBatchRefs(forward, results, 2, labels, er));             // op 2 is not earlier than op 2
		J failed = J::parse(R"({"x":{"$ref":"broken.id"}})");
		CHECK(!ResolveBatchRefs(failed, results, 3, labels, er));              // references a failed op
		J missing = J::parse(R"({"x":{"$ref":"0.nope"}})");
		CHECK(!ResolveBatchRefs(missing, results, 3, labels, er));
		J badIdx = J::parse(R"({"x":{"$ref":"2.entities.9"}})");
		CHECK(!ResolveBatchRefs(badIdx, results, 3, labels, er));
		J unknownLabel = J::parse(R"({"x":{"$ref":"nope.id"}})");
		CHECK(!ResolveBatchRefs(unknownLabel, results, 3, labels, er));
		J nonString = J::parse(R"({"x":{"$ref":5}})");
		CHECK(!ResolveBatchRefs(nonString, results, 3, labels, er));
	}

	// --- Phase B: component field shape checks ---
	{
		using J = EditorBridge::json;
		std::string er;
		CHECK(JsonFieldType(J(true)) == "bool" && JsonFieldType(J(3)) == "int" && JsonFieldType(J(2.5)) == "float");
		CHECK(JsonFieldType(J::parse("[1,2,3]")) == "vec3" && JsonFieldType(J::parse("[1,2,3,4]")) == "vec4");
		CHECK(JsonFieldType(J::parse("[1,2,3,4,5]")) == "array" && JsonFieldType(J::parse("[\"a\"]")) == "array");
		CHECK(JsonFieldType(J::parse("{}")) == "object" && JsonFieldType(J("s")) == "string");

		CHECK(CheckFieldShape(J(1.0), J(2), "f", er));                       // int into float is fine
		CHECK(CheckFieldShape(J(1), J(4.0), "i", er));                       // integral float into int is fine
		CHECK(!CheckFieldShape(J(1), J(4.5), "i", er));                      // fractional into int
		CHECK(!CheckFieldShape(J(1.0), J("x"), "f", er));                    // string into number
		CHECK(!CheckFieldShape(J(1.0), J(true), "f", er));                   // bool into number
		CHECK(!CheckFieldShape(J(true), J(1), "b", er));                     // number into bool
		CHECK(CheckFieldShape(J::parse("[0,0,0]"), J::parse("[1,2,3]"), "v", er));
		CHECK(!CheckFieldShape(J::parse("[0,0,0]"), J::parse("[1,2]"), "v", er));      // vec3 keeps its length
		CHECK(!CheckFieldShape(J::parse("[0,0,0]"), J::parse("[1,\"a\",3]"), "v", er));
		CHECK(CheckFieldShape(J::parse("[]"), J::parse("[1,2,3,4,5,6]"), "list", er)); // variable lists may resize
		CHECK(!CheckFieldShape(J(1.0), J(nullptr), "f", er));                // no nulls
		CHECK(CheckFieldShape(J(nullptr), J("anything"), "n", er));          // unknown current shape -> allowed
		CHECK(CheckFieldShape(J::parse(R"({"box":{"aabb":1}})"), J::parse(R"({"sphere":{"radius":2}})"), "shapeData", er)); // new nested key ok
		CHECK(!CheckFieldShape(J::parse(R"({"box":{"aabb":1}})"), J::parse(R"({"box":{"aabb":"x"}})"), "shapeData", er));  // nested type checked
		CHECK(er.find("shapeData.box.aabb") != std::string::npos);           // error names the nested path
	}
}

// --- MCP static tools + discovery ------------------------------------------

static void TestMcpStaticTools()
{
	namespace fs = std::filesystem;
	std::error_code ec;

	// ValidateNlohmannStaging: empty tree -> not staged; full tree -> staged.
	fs::path inc = fs::temp_directory_path() / "hexmcp_test_inc";
	fs::remove_all(inc, ec);
	fs::create_directories(inc / "nlohmann", ec);
	auto v0 = HexEngine::Mcp::ValidateNlohmannStaging(inc.string());
	CHECK(v0["staged"] == false && v0["missing"].size() == 3);
	for (const char* h : { "json.hpp", "adl_serializer.hpp", "json_fwd.hpp" })
		{ std::ofstream f(inc / "nlohmann" / h); f << "x"; }
	auto v1 = HexEngine::Mcp::ValidateNlohmannStaging(inc.string());
	CHECK(v1["staged"] == true && v1["missing"].empty());
	fs::remove_all(inc, ec);

	// ParseMsbuildLog: two errors (error C + fatal error LNK) + one warning.
	const std::string log =
		"foo.cpp(1,1): error C1083: cannot open\n"
		"bar.cpp(2): warning C4267: conversion\n"
		"just an ordinary line\n"
		"qux.obj : fatal error LNK1120: unresolved\n";
	auto p = HexEngine::Mcp::ParseMsbuildLog(log);
	CHECK(p["errorCount"] == 2 && p["warningCount"] == 1 && p["ok"] == false);

	// DiscoverSessions with an empty session dir -> no sessions (the precondition
	// that makes live MCP tools return EditorNotConnected).
	fs::path sdir = fs::temp_directory_path() / "hexmcp_empty_sessions_test";
	fs::remove_all(sdir, ec);
	fs::create_directories(sdir, ec);
	_putenv_s("HEXENGINE_BRIDGE_SESSION_DIR", sdir.string().c_str());
	CHECK(HexEngine::Mcp::DiscoverSessions().empty());
	_putenv_s("HEXENGINE_BRIDGE_SESSION_DIR", "");
	fs::remove_all(sdir, ec);
}

// Ocean wave model (underwater S0): the CPU mirror of WaterCommon.shader. These
// can't compare against the GPU, so they pin the PROPERTIES every consumer
// relies on - the camera-submerged test, the wet band and (later) swimming.
static void TestOceanWaveModel()
{
	using namespace HexEngine::OceanWaves;

	auto makeInputs = [](float windSpeed, float scale, float time)
	{
		WaveInputs in;
		in.windDirX = 0.8f; in.windDirZ = 0.6f;
		in.windSpeed = windSpeed; in.waveScale = scale; in.time = time;
		return in;
	};

	// 1. Nothing exceeds the amplitude bound (it gates the underwater pass and
	//    sizes the wet line - a height above it would be a missed submersion).
	//    Swept over calm / fresh / storm, the scene's pinned scale, space, time.
	{
		const float winds[] = { 0.0f, 9.0f, 17.0f, 30.0f };
		const float scales[] = { 1.0f, 2.28f };
		bool withinBound = true, finite = true;
		float worstRatio = 0.0f;
		for (float wind : winds) for (float scale : scales)
		{
			for (int t = 0; t < 6; ++t)
			{
				const WaveInputs in = makeInputs(wind, scale, 13.7f * (float)t + 1000.0f);
				const float bound = AmplitudeBound(in);
				for (int i = 0; i < 40; ++i)
				{
					const float x = 900.0f + 3.7f * (float)i, z = 262.0f - 5.3f * (float)i;
					const float h = SurfaceOffset(x, z, in);
					finite = finite && std::isfinite(h);
					withinBound = withinBound && (std::fabs(h) <= bound + 1e-4f);
					worstRatio = std::max(worstRatio, std::fabs(h) / std::max(bound, 1e-6f));
				}
			}
		}
		CHECK(finite);
		CHECK(withinBound);
		// ...and the bound is not uselessly loose: real seas reach a decent
		// fraction of it somewhere in that sweep.
		CHECK(worstRatio > 0.25f);
	}

	// 2. The Gerstner inversion converges: the grid point it finds really does
	//    displace onto the queried (x, z). The fixed-point contraction factor is
	//    the summed swell steepness, so the tolerance is per sea state: microns
	//    in ordinary weather, millimetres in a full storm at the scene's 2.28
	//    wave scale (4 rounds left CENTIMETRES there - this test found it).
	{
		float worstTypical = 0.0f, worstStorm = 0.0f;
		for (int i = 0; i < 60; ++i)
		{
			const float x = -400.0f + 17.3f * (float)i, z = 1200.0f - 9.1f * (float)i;
			worstTypical = std::max(worstTypical, InversionResidual(x, z, makeInputs(17.0f, 2.28f, 4321.0f)));
			worstStorm = std::max(worstStorm, InversionResidual(x, z, makeInputs(30.0f, 2.28f, 4321.0f)));
		}
		CHECK(worstTypical < 1e-3f);
		CHECK(worstStorm < 1e-2f);
	}

	// 3. Wind scales AMPLITUDE only - the phase (so the position of every
	//    crest) must not depend on it, or weather transitions teleport the
	//    sea. Bound grows monotonically with wind; a calm sea is near-flat.
	{
		CHECK(AmplitudeBound(makeInputs(0.0f, 1.0f, 0.0f)) < AmplitudeBound(makeInputs(10.0f, 1.0f, 0.0f)));
		CHECK(AmplitudeBound(makeInputs(10.0f, 1.0f, 0.0f)) < AmplitudeBound(makeInputs(30.0f, 1.0f, 0.0f)));
		CHECK(AmplitudeBound(makeInputs(0.0f, 1.0f, 0.0f)) < 0.35f);
		CHECK(AmplitudeBound(makeInputs(0.0f, 0.0f, 0.0f)) == 0.0f);   // r_oceanWaveScale 0 = flat
		CHECK(SurfaceOffset(10.0f, 20.0f, makeInputs(20.0f, 0.0f, 50.0f)) == 0.0f);
	}

	// 4. Continuity in time and space - a camera bobbing at the waterline must
	//    not see the surface jump (one 60 Hz frame, one centimetre).
	{
		const WaveInputs a = makeInputs(17.0f, 2.28f, 500.0f);
		WaveInputs b = a; b.time += 1.0f / 60.0f;
		const float h0 = SurfaceOffset(900.0f, 262.0f, a);
		CHECK(std::fabs(SurfaceOffset(900.0f, 262.0f, b) - h0) < 0.05f);
		CHECK(std::fabs(SurfaceOffset(900.01f, 262.0f, a) - h0) < 0.01f);
	}

	// 5. The tessellation band-limit only ever REMOVES displaced detail with
	//    distance, and matches the hull-shader curve's end points.
	{
		CHECK(std::fabs(TessSegmentLength(0.0f) - 0.5f) < 1e-5f);     // factor 16
		CHECK(std::fabs(TessSegmentLength(165.0f) - 8.0f) < 1e-4f);   // raw grid
		CHECK(TessSegmentLength(80.0f) > TessSegmentLength(20.0f));
	}
}

// PhysUtils::RayCast maths (Physics/RayMath.hpp). The old raycast returned the
// FIRST triangle it found (not the nearest), averaged OBJECT-space vertex
// normals, and transformed every vertex per test; these pin the replacement.
static void TestRayMath()
{
	using namespace HexEngine::RayMath;
	auto Approx = [](float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; };
	auto ApproxV = [&](const Vec3& a, const Vec3& b, float eps = 1e-4f) { return Approx(a.x, b.x, eps) && Approx(a.y, b.y, eps) && Approx(a.z, b.z, eps); };

	struct TriList
	{
		std::vector<Vec3> pos;
		std::vector<uint32_t> idx;
		void Add(const Vec3& a, const Vec3& b, const Vec3& c)
		{
			const uint32_t base = (uint32_t)pos.size();
			pos.push_back(a); pos.push_back(b); pos.push_back(c);
			idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
		}
		// Up-facing ((b-a)x(c-a) = +Y) triangle covering the origin at height y.
		void AddFloor(float y) { Add(Vec3(-1, y, -1), Vec3(-1, y, 3), Vec3(3, y, -1)); }
		TriangleHit Cast(const ObjectRay& r, float maxT = 1e9f) const
		{
			return NearestTriangle(r, idx.size() / 3, [&](size_t i) { return idx[i]; }, pos.size(),
				[&](uint32_t v) { return pos[v]; }, maxT);
		}
	};

	// Front face convention: (b-a)x(c-a).
	CHECK(ApproxV(FaceNormal(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)), Vec3(0, 1, 0)));

	// 1. Nearest of many, regardless of list order (old code: first found).
	{
		TriList t;
		t.AddFloor(1.0f); // far
		t.AddFloor(5.0f); // NEAREST from y=10 going down
		t.AddFloor(3.0f);
		const ObjectRay down{ Vec3(0, 10, 0), Vec3(0, -1, 0) };
		const TriangleHit h = t.Cast(down);
		CHECK(h.hit);
		CHECK(h.triangle == 1);
		CHECK(Approx(h.t, 5.0f));
		// maxT clips: nothing nearer than 4 units.
		CHECK(!t.Cast(down, 4.0f).hit);
		// Barycentrics reproduce the hit point.
		const Vec3 p = t.pos[3] * (1.0f - h.u - h.v) + t.pos[4] * h.u + t.pos[5] * h.v;
		CHECK(ApproxV(p, Vec3(0, 5, 0)));
		// Miss to the side; ray pointing away.
		CHECK(!t.Cast(ObjectRay{ Vec3(10, 10, 10), Vec3(0, -1, 0) }).hit);
		CHECK(!t.Cast(ObjectRay{ Vec3(0, 10, 0), Vec3(0, 1, 0) }).hit);
	}

	// 2. Back face: both sides hit; the side is reported, not filtered.
	{
		TriList t;
		t.AddFloor(0.0f);
		const ObjectRay up{ Vec3(0, -2, 0), Vec3(0, 1, 0) };
		const TriangleHit h = t.Cast(up);
		CHECK(h.hit && Approx(h.t, 2.0f));
		const Vec3 n = FaceNormal(t.pos[0], t.pos[1], t.pos[2]);
		CHECK(IsBackFace(n, up.direction));
		CHECK(!IsBackFace(n, Vec3(0, -1, 0)));
	}

	// 3. Pass-through: a steep wall in front of a floor is skipped by the slope
	//    veto and the floor BEHIND it still wins.
	{
		TriList t;
		// Wall in the plane x = 1, facing -X (towards the ray origin).
		t.Add(Vec3(1, -5, -5), Vec3(1, -5, 5), Vec3(1, 5, -5));
		t.Add(Vec3(-10, -3, -10), Vec3(-10, -3, 30), Vec3(30, -3, -10)); // floor y=-3, +Y
		CHECK(ApproxV(FaceNormal(t.pos[0], t.pos[1], t.pos[2]) / 100.0f, Vec3(-1, 0, 0)));
		// Diagonal ray down-right: wall at x=1 (t=sqrt2), floor at y=-3 (t=3*sqrt2).
		const Vec3 dir = Vec3(1, -1, 0) / std::sqrt(2.0f);
		const ObjectRay r{ Vec3(0, 0, 0), dir };
		const TriangleHit wall = t.Cast(r);
		CHECK(wall.hit && wall.triangle == 0 && Approx(wall.t, std::sqrt(2.0f)));
		const float minNormalY = 0.5f;
		const TriangleHit floor = NearestTriangle(r, 2, [&](size_t i) { return t.idx[i]; }, t.pos.size(),
			[&](uint32_t v) { return t.pos[v]; }, 1e9f,
			[&](uint32_t, float, const Vec3& a, const Vec3& b, const Vec3& c)
			{
				Vec3 n = FaceNormal(a, b, c); n.Normalize();
				return !RejectsSlope(n, minNormalY);
			});
		CHECK(floor.hit && floor.triangle == 1 && Approx(floor.t, 3.0f * std::sqrt(2.0f)));
		CHECK(!RejectsSlope(Vec3(-1, 0, 0), -2.0f)); // disabled
		CHECK(RejectsSlope(Vec3(-1, 0, 0), 0.5f));
		CHECK(!RejectsSlope(Vec3(0, 1, 0), 0.5f));
	}

	// 4. Full object-space pipeline under rotation + NON-UNIFORM scale +
	//    translation: hit distance is the world distance, and the normal is
	//    the world surface normal (old code: untransformed object normal).
	{
		TriList t;
		t.Add(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 1, 0)); // slanted, faces (-1, 1, 0)
		const Mat4 world = Mat4::CreateScale(2.0f, 1.0f, 3.0f) *
			Mat4::CreateRotationZ(DirectX::XM_PIDIV2 * 0.5f) * Mat4::CreateRotationY(0.7f) *
			Mat4::CreateTranslation(3.0f, 4.0f, 5.0f);
		Mat4 inv;
		CHECK(InvertWorld(world, inv));

		// Reference normal: the cross product of the WORLD-space triangle.
		const Vec3 wa = Vec3::Transform(t.pos[0], world), wb = Vec3::Transform(t.pos[1], world), wc = Vec3::Transform(t.pos[2], world);
		Vec3 expectedN = FaceNormal(wa, wb, wc); expectedN.Normalize();
		const Vec3 n = NormalToWorld(FaceNormal(t.pos[0], t.pos[1], t.pos[2]), inv);
		CHECK(ApproxV(n, expectedN));
		CHECK(Approx(n.Length(), 1.0f));
		// The naive transform (no inverse-transpose) is wrong under this scale.
		Vec3 naive = Vec3::TransformNormal(FaceNormal(t.pos[0], t.pos[1], t.pos[2]), world); naive.Normalize();
		CHECK(!ApproxV(naive, expectedN, 1e-2f));

		// Shoot at a known surface point from 7 world units along the normal.
		const Vec3 surface = Vec3::Transform(Vec3(0.25f, 0.25f, 0.25f), world); // u = v = 0.25
		const Vec3 origin = surface + expectedN * 7.0f;
		const Vec3 dir = -expectedN;
		const ObjectRay local = ToObjectSpace(origin, dir, inv);
		const TriangleHit h = t.Cast(local);
		CHECK(h.hit);
		CHECK(Approx(h.t, 7.0f, 1e-3f));                 // t is the WORLD distance
		CHECK(ApproxV(origin + dir * h.t, surface, 1e-3f));
		CHECK(!IsBackFace(n, dir));
		CHECK(IsBackFace(n, -dir));
		// maxT is in world units too.
		CHECK(!t.Cast(local, 6.9f).hit);
	}

	// 5. Pure rotation: a floor rotated 90 degrees about Z becomes a wall
	//    facing -X (right-handed).
	{
		Mat4 inv;
		CHECK(InvertWorld(Mat4::CreateRotationZ(DirectX::XM_PIDIV2), inv));
		CHECK(ApproxV(NormalToWorld(Vec3(0, 1, 0), inv), Vec3(-1, 0, 0)));
	}

	// 6. Mirroring keeps the outward side outward; zero scale is rejected.
	{
		Mat4 inv;
		CHECK(InvertWorld(Mat4::CreateScale(-1.0f, 1.0f, 1.0f), inv));
		CHECK(ApproxV(NormalToWorld(Vec3(0, 1, 0), inv), Vec3(0, 1, 0)));
		CHECK(ApproxV(NormalToWorld(Vec3(1, 0, 0), inv), Vec3(-1, 0, 0)));
		CHECK(!InvertWorld(Mat4::CreateScale(1.0f, 0.0f, 1.0f), inv));
		CHECK(ApproxV(NormalToWorld(Vec3(0, 0, 0), Mat4()), Vec3(0, 0, 0))); // degenerate -> zero
	}

	// 7. Degenerate triangle and parallel ray never hit; out-of-range indices skip.
	{
		float tt, u, v;
		CHECK(!IntersectTriangle(Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0), tt, u, v));
		CHECK(!IntersectTriangle(Vec3(-5, 0, 0.2f), Vec3(1, 0, 0), Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0), tt, u, v));
		TriList t;
		t.AddFloor(0.0f);
		t.idx[2] = 99;
		CHECK(!t.Cast(ObjectRay{ Vec3(0, 1, 0), Vec3(0, -1, 0) }).hit);
	}
}

// TriangleBVH (Physics/RayMath.hpp): per-mesh acceleration for big meshes
// (terrain). It must return EXACTLY what the brute-force NearestTriangle
// returns - same t, same triangle (up to exact ties) - for any ray.
static void TestTriangleBVH()
{
	using namespace HexEngine::RayMath;

	// Deterministic LCG so failures reproduce.
	uint32_t seed = 12345u;
	auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) * (1.0f / 16777216.0f); };
	auto rr = [&](float a, float b) { return a + (b - a) * rnd(); };

	// 1. Random triangle soup + a bumpy heightfield, many random rays.
	std::vector<Vec3> pos;
	std::vector<uint32_t> idx;
	for (int i = 0; i < 1500; ++i)
	{
		const Vec3 c(rr(-50, 50), rr(-50, 50), rr(-50, 50));
		const uint32_t b = (uint32_t)pos.size();
		for (int k = 0; k < 3; ++k)
			pos.push_back(c + Vec3(rr(-4, 4), rr(-4, 4), rr(-4, 4)));
		idx.push_back(b); idx.push_back(b + 1); idx.push_back(b + 2);
	}
	const int N = 40;
	const uint32_t gridBase = (uint32_t)pos.size();
	for (int z = 0; z <= N; ++z)
		for (int x = 0; x <= N; ++x)
			pos.push_back(Vec3(-60.0f + x * 3.0f, -55.0f + std::sin(x * 0.7f) * std::cos(z * 0.5f) * 3.0f, -60.0f + z * 3.0f));
	for (int z = 0; z < N; ++z)
		for (int x = 0; x < N; ++x)
		{
			const uint32_t a = gridBase + z * (N + 1) + x, b = a + 1, c = a + (N + 1), d = c + 1;
			idx.push_back(a); idx.push_back(c); idx.push_back(b); // +Y facing
			idx.push_back(b); idx.push_back(c); idx.push_back(d);
		}
	idx.push_back(0); idx.push_back(1); idx.push_back(999999); // out-of-range: skipped by both

	const size_t triCount = idx.size() / 3;
	auto index = [&](size_t i) { return idx[i]; };
	auto position = [&](uint32_t v) { return pos[v]; };

	TriangleBVH bvh;
	bvh.Build(triCount, index, pos.size(), position);
	CHECK(!bvh.Empty());
	CHECK(bvh.TriangleCount() == triCount - 1);

	int mismatches = 0, hits = 0;
	for (int r = 0; r < 3000; ++r)
	{
		ObjectRay ray;
		if (r % 3 == 0)
		{
			ray.origin = Vec3(rr(-60, 60), 80.0f, rr(-60, 60));  // straight down (ground probe)
			ray.direction = Vec3(0, -1, 0);
		}
		else
		{
			ray.origin = Vec3(rr(-70, 70), rr(-70, 70), rr(-70, 70));
			ray.direction = Vec3(rr(-1, 1), rr(-1, 1), rr(-1, 1)) * rr(0.2f, 3.0f); // un-normalised, as in object space
		}
		const float maxT = (r % 5 == 0) ? rr(1.0f, 60.0f) : 1e9f;
		const TriangleHit a = NearestTriangle(ray, triCount, index, pos.size(), position, maxT);
		const TriangleHit b = bvh.Intersect(ray, maxT);
		if (a.hit != b.hit || (a.hit && (a.t != b.t || (a.triangle != b.triangle && a.t != b.t))))
			++mismatches;
		if (a.hit)
			++hits;
	}
	CHECK(mismatches == 0);
	CHECK(hits > 1000); // the comparison actually exercised hits

	// 2. Slope veto passes through inside the BVH too: from above the
	//    heightfield, rejecting everything with normal.y < 0.999 must agree
	//    with brute force.
	{
		auto steep = [&](uint32_t, float, const Vec3& a, const Vec3& b, const Vec3& c)
		{
			Vec3 n = FaceNormal(a, b, c); n.Normalize();
			return !RejectsSlope(n, 0.999f);
		};
		int diff = 0;
		for (int r = 0; r < 500; ++r)
		{
			const ObjectRay ray{ Vec3(rr(-60, 60), 80.0f, rr(-60, 60)), Vec3(rr(-0.3f, 0.3f), -1.0f, rr(-0.3f, 0.3f)) };
			const TriangleHit a = NearestTriangle(ray, triCount, index, pos.size(), position, 1e9f, steep);
			const TriangleHit b = bvh.Intersect(ray, 1e9f, steep);
			if (a.hit != b.hit || (a.hit && a.t != b.t))
				++diff;
		}
		CHECK(diff == 0);
	}

	// 3. Degenerate inputs: all centroids coincident (forces the median
	//    fallback), an empty list, and a lone triangle.
	{
		std::vector<Vec3> p2;
		std::vector<uint32_t> i2;
		for (int i = 0; i < 300; ++i)
		{
			const float s = 1.0f + i * 0.01f; // nested, same centroid
			const uint32_t b = (uint32_t)p2.size();
			p2.push_back(Vec3(-s, 0, -s)); p2.push_back(Vec3(-s, 0, 2 * s)); p2.push_back(Vec3(2 * s, 0, -s));
			i2.push_back(b); i2.push_back(b + 1); i2.push_back(b + 2);
		}
		TriangleBVH d;
		d.Build(i2.size() / 3, [&](size_t i) { return i2[i]; }, p2.size(), [&](uint32_t v) { return p2[v]; });
		const TriangleHit h = d.Intersect(ObjectRay{ Vec3(0.1f, 5, 0.1f), Vec3(0, -1, 0) }, 1e9f);
		CHECK(h.hit && std::fabs(h.t - 5.0f) < 1e-5f);

		TriangleBVH e;
		e.Build(0, [&](size_t i) { return i2[i]; }, p2.size(), [&](uint32_t v) { return p2[v]; });
		CHECK(e.Empty());
		CHECK(!e.Intersect(ObjectRay{ Vec3(0, 5, 0), Vec3(0, -1, 0) }, 1e9f).hit);

		TriangleBVH one;
		one.Build(1, [&](size_t i) { return i2[i]; }, p2.size(), [&](uint32_t v) { return p2[v]; });
		CHECK(one.Intersect(ObjectRay{ Vec3(0, 5, 0), Vec3(0, -1, 0) }, 1e9f).hit);
		CHECK(!one.Intersect(ObjectRay{ Vec3(0, 5, 0), Vec3(0, -1, 0) }, 4.0f).hit); // maxT respected
	}
}

int main()
{
	std::printf("HexEngine.Tests\n");
	TestManifestParse();
	TestEvaluate();
	TestSha256();
	TestBinaryReaderBasics();
	TestBinaryReaderBytesAndSeek();
	TestBinaryReaderString();
	TestBinaryReaderCount();
	TestBinaryReaderPackedWalk();
	TestBlockingQueueBasics();
	TestBlockingQueueConcurrent();
	TestDirectoryWatchStopJoin();
	TestPackageBlobBounds();
	TestPackageManifestRoundtrip();
	TestBridgeProtocol();
	TestMcpStaticTools();
	TestOceanWaveModel();
	TestRayMath();
	TestTriangleBVH();
	std::printf("\n%d/%d checks passed.\n", g_total - g_fail, g_total);
	std::printf(g_fail == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
	return g_fail == 0 ? 0 : 1;
}
