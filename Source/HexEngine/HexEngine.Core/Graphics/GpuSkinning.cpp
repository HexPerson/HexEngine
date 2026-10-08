#include "GpuSkinning.hpp"
#include "../HexEngine.hpp"
#include "../Scene/AnimatedMesh.hpp"
#include <d3d11.h>

namespace HexEngine
{
	HVar r_gpuSkinning("r_gpuSkinning", "Skin animated meshes once per frame in a compute pass (0 = skin in every vertex shader, the old path)", true, false, true);

	namespace
	{
		constexpr uint32_t kThreadsPerGroup = 64;	// GpuSkinning.shader [numthreads]

		// Mirrors GpuSkinConstants in GpuSkinning.shader.
		struct GpuSkinConstants
		{
			uint32_t stride, positionOffset, normalOffset, tangentOffset;
			uint32_t bitangentOffset, boneIdsOffset, boneWeightsOffset, vertexCount;
			uint32_t simpleStride, simplePositionOffset, boneCount, pad;
			uint32_t pad1[4];
		};
		static_assert(sizeof(GpuSkinConstants) % 16 == 0, "cbuffer size must be a multiple of 16");

		template<typename T, typename M>
		uint32_t FieldOffset(const T& object, const M& member)
		{
			return (uint32_t)(reinterpret_cast<const uint8_t*>(&member) - reinterpret_cast<const uint8_t*>(&object));
		}

		ID3D11Device* GetDevice()
		{
			return reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		}

		ID3D11DeviceContext* GetContext()
		{
			return reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		}

		IShader* GetSkinShader()
		{
			static std::shared_ptr<IShader> s_shader;
			static bool s_tried = false;
			if (!s_tried)
			{
				s_tried = true;
				s_shader = IShader::Create("EngineData.Shaders/GpuSkinning.hcs");
				if (!s_shader)
					LOG_WARN("GpuSkinning.hcs failed to load - animated meshes fall back to vertex-shader skinning");
			}
			return s_shader.get();
		}

		// Raw (byte-address) view over a whole buffer.
		D3D11_UNORDERED_ACCESS_VIEW_DESC RawUavDesc(uint32_t byteWidth)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
			desc.Format = DXGI_FORMAT_R32_TYPELESS;
			desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			desc.Buffer.NumElements = byteWidth / 4;
			desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			return desc;
		}
	}

	bool GpuSkinning::IsSupported()
	{
		return r_gpuSkinning._val.b
			&& g_pEnv != nullptr && g_pEnv->_graphicsDevice != nullptr
			&& g_pEnv->_graphicsDevice->GetBackend() == GraphicsBackend::D3D11;
	}

	bool GpuSkinning::IsAvailable()
	{
		return r_gpuSkinning._val.b
			&& g_pEnv->_graphicsDevice != nullptr
			&& g_pEnv->_graphicsDevice->GetBackend() == GraphicsBackend::D3D11
			&& GetSkinShader() != nullptr;
	}

	// ---- GpuSkinSource ----------------------------------------------------------------

	GpuSkinSource::~GpuSkinSource()
	{
		SAFE_RELEASE(_srv);
		SAFE_RELEASE(_buffer);
	}

	bool GpuSkinSource::Create(const AnimatedMesh& mesh)
	{
		const auto& vertices = mesh.GetVertices();
		ID3D11Device* device = GetDevice();
		if (vertices.empty() || device == nullptr)
			return false;

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)(vertices.size() * sizeof(AnimatedMeshVertex));
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = vertices.data();
		if (FAILED(device->CreateBuffer(&desc, &data, &_buffer)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = DXGI_FORMAT_R32_TYPELESS;
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srv.BufferEx.NumElements = desc.ByteWidth / 4;
		srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
		if (FAILED(device->CreateShaderResourceView(_buffer, &srv, &_srv)))
			return false;

		_vertexCount = (uint32_t)vertices.size();
		return true;
	}

	// ---- GpuSkinnedVertexBuffer -----------------------------------------------------------

	bool GpuSkinnedVertexBuffer::Create(uint32_t stride, uint32_t vertexCount, const void* initialData, bool shaderResource)
	{
		Destroy();

		ID3D11Device* device = GetDevice();
		if (device == nullptr || stride == 0 || vertexCount == 0 || (stride % 4) != 0)
			return false;

		// A buffer can only be both a vertex buffer and a UAV through a RAW view.
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = stride * vertexCount;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS | (shaderResource ? D3D11_BIND_SHADER_RESOURCE : 0u);
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

		// Seeded with the bind pose: the compute pass only rewrites the skinned
		// fields, so texcoords (and bone weights) stay valid from here on.
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = initialData;
		if (FAILED(device->CreateBuffer(&desc, initialData ? &data : nullptr, &_buffer)))
			return false;

		const D3D11_UNORDERED_ACCESS_VIEW_DESC uav = RawUavDesc(desc.ByteWidth);
		if (FAILED(device->CreateUnorderedAccessView(_buffer, &uav, &_uav)))
		{
			Destroy();
			return false;
		}

		if (shaderResource)
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
			srv.Format = DXGI_FORMAT_R32_TYPELESS;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			srv.BufferEx.NumElements = desc.ByteWidth / 4;
			srv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			if (FAILED(device->CreateShaderResourceView(_buffer, &srv, &_srv)))
			{
				Destroy();
				return false;
			}
		}

		_stride = stride;
		return true;
	}

	void GpuSkinnedVertexBuffer::Destroy()
	{
		SAFE_RELEASE(_srv);
		SAFE_RELEASE(_uav);
		SAFE_RELEASE(_buffer);
		_stride = 0;
	}

	// ---- GpuSkinInstance -----------------------------------------------------------------

	GpuSkinInstance::~GpuSkinInstance()
	{
		SAFE_RELEASE(_bonesSrv);
		SAFE_RELEASE(_bones);
		SAFE_RELEASE(_constants);
	}

	bool GpuSkinInstance::EnsureResources(const AnimatedMesh& mesh)
	{
		const auto& vertices = mesh.GetVertices();
		const auto& simpleVertices = mesh.GetSimpleVertices();
		const uint32_t vertexCount = (uint32_t)vertices.size();

		if (_mesh == &mesh && _vertexCount == vertexCount && _bones != nullptr)
			return true;

		if (_failedMesh == &mesh)
			return false;
		_failedMesh = &mesh;	// cleared below once everything exists

		if (vertexCount == 0 || simpleVertices.size() != vertices.size())
			return false;

		ID3D11Device* device = GetDevice();
		if (device == nullptr)
			return false;

		_mesh = nullptr;
		SAFE_RELEASE(_bonesSrv);
		SAFE_RELEASE(_bones);
		SAFE_RELEASE(_constants);

		if (!_out.Create(sizeof(AnimatedMeshVertex), vertexCount, vertices.data(), true) ||
			!_outSimple.Create(sizeof(SimpleAnimatedMeshVertex), vertexCount, simpleVertices.data()))
			return false;

		// Bones: current palette then previous, MAX_BONES matrices each, as float4 rows.
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(math::Matrix) * MAX_BONES * 2;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(math::Vector4);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &_bones)))
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
			srv.Format = DXGI_FORMAT_UNKNOWN;
			srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv.Buffer.NumElements = desc.ByteWidth / sizeof(math::Vector4);
			if (FAILED(device->CreateShaderResourceView(_bones, &srv, &_bonesSrv)))
				return false;
		}

		// Layout constants never change for a given mesh, so they live in an immutable buffer.
		{
			const AnimatedMeshVertex v{};
			const SimpleAnimatedMeshVertex sv{};

			// MeshCommon.shader hard-codes where vertex shaders find last frame's skinned
			// position (PRESKINNED_VERTEX_STRIDE / PRESKINNED_PREV_POSITION_OFFSET).
			static_assert(sizeof(AnimatedMeshVertex) == 92, "update PRESKINNED_VERTEX_STRIDE in MeshCommon.shader");
			if (FieldOffset(v, v._boneIds) != 60)
				LOG_WARN("GpuSkinning: AnimatedMeshVertex::_boneIds moved - update PRESKINNED_PREV_POSITION_OFFSET in MeshCommon.shader");

			GpuSkinConstants constants = {};
			constants.stride = sizeof(AnimatedMeshVertex);
			constants.positionOffset = FieldOffset(v, v._position);
			constants.normalOffset = FieldOffset(v, v._normal);
			constants.tangentOffset = FieldOffset(v, v._tangent);
			constants.bitangentOffset = FieldOffset(v, v._bitangent);
			constants.boneIdsOffset = FieldOffset(v, v._boneIds);
			constants.boneWeightsOffset = FieldOffset(v, v._boneWeights);
			constants.vertexCount = vertexCount;
			constants.simpleStride = sizeof(SimpleAnimatedMeshVertex);
			constants.simplePositionOffset = FieldOffset(sv, sv._position);
			constants.boneCount = MAX_BONES;

			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(GpuSkinConstants);
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

			D3D11_SUBRESOURCE_DATA data = {};
			data.pSysMem = &constants;
			if (FAILED(device->CreateBuffer(&desc, &data, &_constants)))
				return false;
		}

		_mesh = &mesh;
		_failedMesh = nullptr;
		_vertexCount = vertexCount;
		return true;
	}

	bool GpuSkinInstance::Skin(AnimatedMesh& mesh, const math::Matrix* bones, const math::Matrix* bonesPrev)
	{
		IShader* shader = GetSkinShader();
		ID3D11DeviceContext* context = GetContext();
		if (shader == nullptr || context == nullptr)
			return false;

		auto* stage = shader->GetShaderStage(ShaderStage::ComputeShader);
		GpuSkinSource* source = mesh.GetGpuSkinSource();
		if (stage == nullptr || source == nullptr || !EnsureResources(mesh) || source->GetVertexCount() != _vertexCount)
			return false;

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(context->Map(_bones, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			return false;
		memcpy(mapped.pData, bones, sizeof(math::Matrix) * MAX_BONES);
		memcpy(static_cast<uint8_t*>(mapped.pData) + sizeof(math::Matrix) * MAX_BONES, bonesPrev, sizeof(math::Matrix) * MAX_BONES);
		context->Unmap(_bones, 0);

		// This runs mid-pass, on the entity's first draw of the frame, so leave the
		// compute stage exactly as we found it for whoever bound it before us.
		ID3D11ComputeShader* prevShader = nullptr;
		ID3D11Buffer* prevCb = nullptr;
		ID3D11ShaderResourceView* prevSrvs[2] = {};
		ID3D11UnorderedAccessView* prevUavs[2] = {};
		context->CSGetShader(&prevShader, nullptr, nullptr);
		context->CSGetConstantBuffers(5, 1, &prevCb);
		context->CSGetShaderResources(0, 2, prevSrvs);
		context->CSGetUnorderedAccessViews(0, 2, prevUavs);

		// An output may still be the bound vertex buffer from an earlier draw; a
		// resource can't be input and UAV at once. AnimatedMesh::SetBuffers always
		// rebinds explicitly afterwards, so clearing slot 0 behind the device's back is safe.
		ID3D11Buffer* nullVb = nullptr;
		const UINT zero = 0;
		context->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
		// Same for the vertex-shader view of the main output (BindForVertexShader).
		ID3D11ShaderResourceView* nullVsSrv = nullptr;
		context->VSSetShaderResources(GpuSkinning::kPreSkinnedVertexSlot, 1, &nullVsSrv);

		ID3D11ShaderResourceView* srvs[2] = { source->GetSrv(), _bonesSrv };
		ID3D11UnorderedAccessView* uavs[2] = { _out.GetUav(), _outSimple.GetUav() };

		context->CSSetShader(reinterpret_cast<ID3D11ComputeShader*>(stage->GetNativePtr()), nullptr, 0);
		context->CSSetConstantBuffers(5, 1, &_constants);
		context->CSSetShaderResources(0, 2, srvs);
		context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

		context->Dispatch((_vertexCount + kThreadsPerGroup - 1) / kThreadsPerGroup, 1, 1);

		// Restore (this also unbinds our outputs, so they can be read as vertex buffers).
		const UINT keepCounters[2] = { (UINT)-1, (UINT)-1 };
		context->CSSetUnorderedAccessViews(0, 2, prevUavs, keepCounters);
		context->CSSetShaderResources(0, 2, prevSrvs);
		context->CSSetConstantBuffers(5, 1, &prevCb);
		context->CSSetShader(prevShader, nullptr, 0);

		SAFE_RELEASE(prevShader);
		SAFE_RELEASE(prevCb);
		for (auto*& srv : prevSrvs) SAFE_RELEASE(srv);
		for (auto*& uav : prevUavs) SAFE_RELEASE(uav);
		return true;
	}

	void GpuSkinInstance::BindForVertexShader()
	{
		// Raw context, fixed slot: outside the device's implicit SRV slot counter, which
		// the material texture binds walk. A read-only view of the bound vertex buffer is
		// fine for D3D11 (both are inputs).
		if (ID3D11DeviceContext* context = GetContext(); context != nullptr)
		{
			ID3D11ShaderResourceView* srv = _out.GetSrv();
			context->VSSetShaderResources(GpuSkinning::kPreSkinnedVertexSlot, 1, &srv);
		}
	}
}
