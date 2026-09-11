# Voxel Engine

A voxel engine passion project built with modern C++ and OpenGL.

![Voxel World Screenshot](screenshot.png "Voxel World Screenshot")

## Features
- 'Infinite' Terrain with Chunking
	- Multi-Threaded Chunk Generation
	- Multi-Threaded Mesh Building
- Meshing
	- Bitwise Occupancy Masks
	- Packed Faces, 4 Bytes Each
- Rendering
	- Hybrid Pipeline
		- Deferred (Opaque)
		- Forward (Translucent)
	- Lighting
		- Blinn-Phong Model
		- Directional, Point, and Spot
	- Screen Space Ambient Occlusion
	- Frustum Culling
	- Skybox
	- Water
- Profiled with Tracy

## Building
Tested on Windows 10 with the basic Visual Studio 2026 setup.

### Dependencies
- [GLFW](https://www.glfw.org/) - Windowing and input
- [GLM](https://glm.g-truc.net/) - OpenGL Math
- [GLAD](https://glad.dav1d.de/) - OpenGL loader
- [ImGui](https://github.com/ocornut/imgui) - GUI Library
- [STB Image](https://github.com/nothings/stb) - Image loading
- [Tracy](https://github.com/wolfpld/tracy) - Profiling

Dependencies belong in `external/`

### Resources
Skybox png textures belong in `resources/textures/skybox` (named: top, bottom, left, right, front, back)

Models (vox) belong in `resources/models`
