# HexEngine
Hex Engine is a high-performance, ECS-driven 3D game engine written in modern C++, built for performance, flexibility, and fast iteration.

It combines a modular architecture with a full production-ready feature set — including a 3D editor, PBR rendering pipeline, HDR, post-processing, physics, audio, asset pipelines, and cross-platform shader compilation — all designed to give developers complete control over their games.

If you prefer building systems over fighting engine constraints, Hex Engine is built for you.
Hex Engine is a C++ open-source 3D game engine currently being developed. The goal of HexEngine is to provide an engine that is extremely performant but offering high-quality graphics.

# Features
- Full interactive world editor
- Hot-reloadable C++ game code whilst in the world editor
- HDR/SDR support
- Screen space reflections
- Nvidia Real-time Denoiser with presets
- DLSS 3.5
- Support for volumetric light sources
- Volumetric day/night light source
- Support for many light sources in one scene
- A fairly well optimised scene gathering algorithm
- Scene saving/loading
- Shader cross compilation (for when the engine supports multiple renderers, currently only DirectX 11 is in use)
- HBAO
- Post-processing effects (chromatic abberration, fog, colour grading, bloom, blur, vignette, FXAA, HDR tone-mapping)
- Temporal anti-aliasing
- Skeletel animation system
- Cascaded shadow mapping

# Prerequesites
You'll need:

- Python
- CMake
- Visual Studio 2022

# Building
Run Setup.bat and the automated build script will grab all the dependencies and begin building. This will take quite some time on the first run. 

You can run this at any time and it will pull the latest repositories from each dependencies and rebuild (if necessary).
