// Minimal, dependency-free unit-test harness for HexEngine hardening work.
// Compiles the pure sources under test directly (no heavy Core link). PR6 will
// formalize this project (CI integration, more coverage, static analysis).

#include <cstdio>
#include <string>

#include "../HexEngine.Core/Plugin/PluginManifest.hpp"
#include "../HexEngine.Core/Utility/Sha256.hpp"

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

int main()
{
	std::printf("HexEngine.Tests\n");
	TestManifestParse();
	TestEvaluate();
	TestSha256();
	std::printf("\n%d/%d checks passed.\n", g_total - g_fail, g_total);
	std::printf(g_fail == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
	return g_fail == 0 ? 0 : 1;
}
