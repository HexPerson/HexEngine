# HexEngine
Hex Engine is an open-source 3D game engine currently being developed. The goal of Hex Engine is to provide an engine that is extremely performant but offering high-quality graphics.

# Features
- Full interactive world editor
- Screen space reflections
- DLSS 3.5
- Support for volumetric light sources
- Volumetric day/night light source
- Support for many light sources in one scene
- A fairly well optimised scene gathering algorithm
- Scene saving/loading
- Shader cross compilation (for when the engine supports multiple renderers, currently only DirectX 11 is in use)
- HBAO
- Post-processing effects (chromatic abberration, fog, colour grading, bloom, blur, vignette, FXAA)
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

# 
