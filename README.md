# HexEngine
HexEngine is a modern, modular 3D game engine written in C++ with a focus on performance, flexibility, and rapid iteration.

Built around an ECS architecture, Hex Engine provides a complete game development stack including a real-time 3D editor, physically-based rendering (PBR), HDR and post-processing, cross-platform shader compilation, physics, audio, and a fully extensible plugin system.

Designed for developers who want low-level control without sacrificing workflow, Hex Engine delivers the power of native C++ with the usability of modern game engines.

# Features
- ⚙️ **Modern C++ Engine Core**
  - High-performance design
  - Modular architecture with clean separation of systems
  - Hot-reloadable C++ game code

- 🧩 **ECS Architecture**
  - Scalable entity-component-system design
  - Flexible and extensible gameplay logic

- 🎮 **3D Editor**
  - Real-time scene editing
  - Asset management and workflow tools
  - Automatic and manual mesh combining for optimised rendering
  - Undo/redo stack
  - HLOD generation

- 🎨 **Rendering Pipeline**
  - Physically Based Rendering (PBR)
  - HDR rendering
  - Post-processing effects
  - Cross-platform shader compilation
  - HBAO
  - Nvidia real-time denoiser
  - Screen-space reflections
  - HLOD system with asynchronous streaming
  - Realtime global illumination
  - Realtime volumetric clouds
  - Realtime volumetric light sources

- 📦 **Asset System**
  - Asset packaging and dependency management
  - Efficient build pipeline

- 🔌 **Plugin System**
  - Fully extensible engine architecture
  - Create and integrate custom modules

- 🔊 **Audio System**
  - Integrated audio playback and control

- 🧱 **Physics Engine**
  - Real-time simulation and collision handling

- 🎮 **Input System**
  - Unified input handling across platforms

# Prerequesites
You'll need:

- Python
- CMake
- Visual Studio 2022

# Building
Canonical path (recommended):

1. `cmake --preset vs2022-x64-debug`
2. `cmake --build --preset minimal-bootstrap-debug`
3. `cmake --build --preset sln-build-debug-modern`

Current required dependency set bootstrapped by the modern path:
`directxtk`, `freetype`, `directxtex`, `brotli`, `rapidjson`, `retpack2d`, `physx`, `shaderconductor`, `streamline`.

vcpkg (manifest mode, recommended for CMake-managed dependencies):
1. Install vcpkg and set `VCPKG_ROOT`.
2. `cmake --preset vs2022-x64-debug-vcpkg`
3. `cmake --build --preset vcpkg-status-debug`

Release lane:

1. `cmake --preset vs2022-x64-release`
2. `cmake --build --preset minimal-bootstrap-release`
3. `cmake --build --preset sln-build-release-modern`

Convenience wrapper:
- `Setup.bat` (calls the canonical CMake debug flow)

For full migration-aware orchestration details, see [BUILDING.md](BUILDING.md).
Dependency updates are explicit (`python tools/deps/bootstrap.py --update bootstrap-minimal --configs Debug,Release`), not automatic.

# Why HexEngine?
HexEngine is built for developers who want:

- Full control over their engine and architecture
- High performance without engine bloat
- A modern ECS-based workflow
- A modular system that scales with complexity

Unlike larger engines, HexEngine is designed to stay lightweight, transparent, and developer-focused.

# Roadmap
- [ ] Improve editor UX and tooling
- [ ] Visual material editor
- [ ] Animation system
- [ ] Networking / multiplayer support
- [ ] Expanded documentation and tutorials
- [ ] Plugin marketplace
- [ ] Sample projects
- [ ] Automated build process

# Contributing
Contributions are welcome!

- Check out open issues
- Submit pull requests
- Suggest features or improvements

Join the community and help shape the future of Hex Engine.

# Community
- Discord: https://discord.gg/QtQXx3QWjk
- Discussions: GitHub Discussions
- Website: hex-engine.com (under construction)
