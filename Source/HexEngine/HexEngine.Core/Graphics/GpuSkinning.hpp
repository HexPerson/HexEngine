#pragma once

#include "../Required.hpp"
#include "IVertexBuffer.hpp"

struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

namespace HexEngine
{
	class AnimatedMesh;

	// Compute-shader skinning (GpuSkinning.shader).
	//
	// Once per frame per visible animated entity, a compute pass skins the mesh's
	// bind-pose vertices with the current and previous bone palettes and writes the
	// result into two per-entity vertex buffers laid out exactly like the mesh's own
	// (AnimatedMeshVertex for the main passes, SimpleAnimatedMeshVertex for shadows).
	// Every pass then draws those buffers rigidly (OBJECT_FLAGS_PRESKINNED), so the
	// bone palette is uploaded once per frame instead of once per draw per pass, the
	// vertex shaders stop re-skinning in every pass, and shadows follow the animation.
	//
	// Raw D3D11 by the same deliberate convention as ClusteredLighting and the other
	// compute producers; other backends report IsAvailable() == false and keep the
	// vertex-shader skinning path.
	namespace GpuSkinning
	{
		// Vertex-shader SRV slot of MeshCommon.shader's g_preSkinnedVertices.
		constexpr uint32_t kPreSkinnedVertexSlot = 40;

		// r_gpuSkinning on, D3D11 backend, and the compute shader loaded.
		HEX_API bool IsAvailable();

		// r_gpuSkinning on and a D3D11 backend - IsAvailable() without loading the shader,
		// so it is safe from resource-loading threads (MeshLoader).
		HEX_API bool IsSupported();
	}

	// Bind-pose source vertices for one AnimatedMesh, readable by the compute pass.
	// Owned by the mesh, shared by every entity that draws it.
	class GpuSkinSource
	{
	public:
		~GpuSkinSource();

		bool Create(const AnimatedMesh& mesh);

		ID3D11ShaderResourceView* GetSrv() const { return _srv; }
		uint32_t GetVertexCount() const { return _vertexCount; }

	private:
		ID3D11Buffer* _buffer = nullptr;
		ID3D11ShaderResourceView* _srv = nullptr;
		uint32_t _vertexCount = 0;
	};

	// A raw buffer the compute pass writes, presented to the device as an ordinary
	// vertex buffer. SetVertexBuffer only needs the native pointer and the stride.
	class GpuSkinnedVertexBuffer : public IVertexBuffer
	{
	public:
		~GpuSkinnedVertexBuffer() override { Destroy(); }

		// `shaderResource`: also create a raw SRV so vertex shaders can read the buffer
		// (the main output, for last-frame positions - see MeshCommon's g_preSkinnedVertices).
		bool Create(uint32_t stride, uint32_t vertexCount, const void* initialData, bool shaderResource = false);

		void Destroy() override;
		void* GetNativePtr() override { return _buffer; }
		uint32_t GetStride() override { return _stride; }
		void SetVertexData(uint8_t* data, uint32_t size, uint32_t offset = 0) override {}	// GPU-written only

		ID3D11UnorderedAccessView* GetUav() const { return _uav; }
		ID3D11ShaderResourceView* GetSrv() const { return _srv; }

	private:
		ID3D11Buffer* _buffer = nullptr;
		ID3D11UnorderedAccessView* _uav = nullptr;
		ID3D11ShaderResourceView* _srv = nullptr;
		uint32_t _stride = 0;
	};

	// Per-entity skinning output (owned by SkeletalAnimationComponent).
	class GpuSkinInstance
	{
	public:
		~GpuSkinInstance();

		// Skins `mesh` with the given palettes (MAX_BONES matrices each, uploaded exactly
		// as the PerAnimationBuffer cbuffer receives them). False = use the fallback path.
		bool Skin(AnimatedMesh& mesh, const math::Matrix* bones, const math::Matrix* bonesPrev);

		IVertexBuffer* GetVertexBuffer(bool shadowPass) { return shadowPass ? &_outSimple : &_out; }

		// Binds the main output to the vertex shader at MeshCommon's g_preSkinnedVertices
		// slot, so any mesh shader can read last frame's skinned position.
		void BindForVertexShader();

		// Pose this output was skinned from (SkeletalAnimationComponent's pose counter).
		uint64_t _skinnedPoseVersion = 0;

	private:
		bool EnsureResources(const AnimatedMesh& mesh);

		const AnimatedMesh* _mesh = nullptr;
		const AnimatedMesh* _failedMesh = nullptr;	// resources couldn't be built for it; don't retry per draw
		uint32_t _vertexCount = 0;

		GpuSkinnedVertexBuffer _out;		// AnimatedMeshVertex layout
		GpuSkinnedVertexBuffer _outSimple;	// SimpleAnimatedMeshVertex layout (shadow passes)
		ID3D11Buffer* _bones = nullptr;
		ID3D11ShaderResourceView* _bonesSrv = nullptr;
		ID3D11Buffer* _constants = nullptr;		// vertex layout + count, immutable
	};
}
