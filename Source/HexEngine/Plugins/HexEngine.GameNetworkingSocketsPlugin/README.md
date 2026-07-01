# HexEngine.GameNetworkingSocketsPlugin

Game-networking transport for HexEngine, wrapping Valve's
[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
(the open-source standalone of Steam's `ISteamNetworkingSockets`). Implements the
core `HexEngine::INetworkSystem` interface, exposed at runtime as
`g_pEnv->_networkSystem` (null when this plugin isn't loaded -> single-player).

The host-authoritative replication layer that sits on top of the transport lives
in core (`HexEngine.Core/Scene/NetworkReplicationSystem.*`,
`HexEngine.Core/Entity/Component/NetworkComponent.*`).

## Dependency (vcpkg)

GameNetworkingSockets + protobuf + a crypto backend are provided via vcpkg. The
package is declared in the repo-root `vcpkg.json`. Materialize it once:

```
vcpkg install --x-manifest-root=C:\HexEngine --x-install-root=C:\HexEngine\vcpkg_installed --triplet x64-windows
```

This produces `C:\HexEngine\vcpkg_installed\x64-windows\` with:
- `include\GameNetworkingSockets\steam\*.h`
- `lib\GameNetworkingSockets.lib` (+ `debug\lib\...`)
- `bin\GameNetworkingSockets.dll`, `abseil_dll.dll`, `libprotobuf(d).dll`,
  `libcrypto-3-x64.dll`, `libssl-3-x64.dll` (+ `debug\bin\...`)

The plugin's `.vcxproj` references that tree directly. Its PreBuildEvent fails
with this command if the lib is missing; its PostBuildEvent copies the plugin DLL
to `Bin\x64\<Config>\Plugins\` and the GNS runtime DLLs to `Bin\x64\<Config>\`.

## Usage / testing

Console cvars (settable from the in-engine console, auto-reset after firing):
- `net_host <port>` - start a listen server.
- `net_connect <port>` - connect to `127.0.0.1:<port>` (loopback testing).
- `net_disconnect 1` - leave the session.

Mark entities to replicate by adding a **NetworkComponent**. Authored entities
present in the same scene on both peers match automatically (by explicit net id,
or a CRC32 of the entity name). Prefab instances created at runtime via
`Scene::GetNetworkReplicationSystem()->SpawnNetworked(prefabPath, pos, rot)` are
spawned/despawned across the wire.

v1 is host-authoritative: the host simulates; clients render smoothed remote
proxies. Client-owned entities (the `ClientAuthoritative` flag / `ClientTransform`
message) are reserved for a follow-up.
