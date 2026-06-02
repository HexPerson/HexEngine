
// Phase B4: state setters, pre-draw flush, draw / dispatch.
// Split out from GraphicsDeviceD3D12.cpp to keep that file's resource-creation
// / lifecycle code readable. Everything in here is GraphicsDeviceD3D12 member
// implementations; no new types.

#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

namespace
{
	D3D12_PRIMITIVE_TOPOLOGY_TYPE PrimitiveTopologyType(HexEngine::PrimitiveTopology t)
	{
		switch (t)
		{
		case HexEngine::PrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case HexEngine::PrimitiveTopology::LineList:
		case HexEngine::PrimitiveTopology::LineStrip:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case HexEngine::PrimitiveTopology::TriangleList:
		case HexEngine::PrimitiveTopology::TriangleStrip:
		default:                                          return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		}
	}
}

// ---- state setters: stash into _pending / _bindings -----------------------

void GraphicsDeviceD3D12::SetConstantBufferVS(uint32_t slot, HexEngine::IConstantBuffer* buf) { SetConstantBufferPS(slot, buf); }
void GraphicsDeviceD3D12::SetConstantBufferPS(uint32_t slot, HexEngine::IConstantBuffer* buf)
{
	if (slot >= RootSignatureD3D12::kCbvCount) return;
	auto* cb = static_cast<ConstantBufferD3D12*>(buf);
	_bindings.cbvs[slot] = cb ? cb->_cbv : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (cb && slot + 1 > _bindings.cbvHighWater) _bindings.cbvHighWater = slot + 1;
}
void GraphicsDeviceD3D12::SetConstantBufferGS(uint32_t slot, HexEngine::IConstantBuffer* buf) { SetConstantBufferPS(slot, buf); }
void GraphicsDeviceD3D12::SetConstantBufferCS(uint32_t slot, HexEngine::IConstantBuffer* buf) { SetConstantBufferPS(slot, buf); }

void GraphicsDeviceD3D12::SetIndexBuffer(HexEngine::IIndexBuffer* buf)                                   { _pending.ib = buf; _pending.dirty = true; }
void GraphicsDeviceD3D12::SetVertexBuffer(uint32_t slot, HexEngine::IVertexBuffer* buf)                  { if (slot < 8) { _pending.vbs[slot] = buf; _pending.dirty = true; } }
void GraphicsDeviceD3D12::SetTopology(HexEngine::PrimitiveTopology t)                                    { _pending.topology = t; _pending.dirty = true; }
void GraphicsDeviceD3D12::SetVertexShader(HexEngine::IShaderStage* s)                                    { _pending.vs = static_cast<ShaderStageD3D12*>(s); _pending.dirty = true; }
void GraphicsDeviceD3D12::SetPixelShader(HexEngine::IShaderStage* s)                                     { _pending.ps = static_cast<ShaderStageD3D12*>(s); _pending.dirty = true; }
void GraphicsDeviceD3D12::SetGeometryShader(HexEngine::IShaderStage* s)                                  { _pending.gs = static_cast<ShaderStageD3D12*>(s); _pending.dirty = true; }
void GraphicsDeviceD3D12::SetComputeShader(HexEngine::IShaderStage* s)                                   { _pending.cs = static_cast<ShaderStageD3D12*>(s); }
void GraphicsDeviceD3D12::SetInputLayout(HexEngine::IInputLayout* l)                                     { _pending.inputLayout = static_cast<InputLayoutD3D12*>(l); _pending.dirty = true; }

void GraphicsDeviceD3D12::SetTexture2D(uint32_t slot, HexEngine::ITexture2D* tex)
{
	if (slot >= RootSignatureD3D12::kSrvCount) return;
	auto* t = static_cast<Texture2DD3D12*>(tex);
	if (t != nullptr)
		TransitionResource(t, (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	_bindings.srvs[slot] = t ? t->_srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (t && slot + 1 > _bindings.srvHighWater) _bindings.srvHighWater = slot + 1;
}
void GraphicsDeviceD3D12::SetTexture2D(HexEngine::ITexture2D* tex) { SetTexture2D(_autoBindCursor, tex); _autoBindCursor++; }

void GraphicsDeviceD3D12::SetTexture3D(HexEngine::ITexture3D* tex)
{
	auto* t = static_cast<Texture3DD3D12*>(tex);
	if (t != nullptr)
		TransitionResource(t, (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	if (_autoBindCursor >= RootSignatureD3D12::kSrvCount) return;
	_bindings.srvs[_autoBindCursor] = t ? t->_srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (t && _autoBindCursor + 1 > _bindings.srvHighWater) _bindings.srvHighWater = _autoBindCursor + 1;
	_autoBindCursor++;
}
void GraphicsDeviceD3D12::SetGeometryTexture3D(uint32_t slot, HexEngine::ITexture3D* tex)
{
	if (slot >= RootSignatureD3D12::kSrvCount) return;
	auto* t = static_cast<Texture3DD3D12*>(tex);
	if (t != nullptr)
		TransitionResource(t, (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	_bindings.srvs[slot] = t ? t->_srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (t && slot + 1 > _bindings.srvHighWater) _bindings.srvHighWater = slot + 1;
}
void GraphicsDeviceD3D12::SetVertexStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf)
{
	if (slot >= RootSignatureD3D12::kSrvCount) return;
	auto* b = static_cast<StructuredBufferD3D12*>(buf);
	if (b != nullptr)
		TransitionResource(b, (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	_bindings.srvs[slot] = b ? b->_srv : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (b && slot + 1 > _bindings.srvHighWater) _bindings.srvHighWater = slot + 1;
}
void GraphicsDeviceD3D12::SetGeometryStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf) { SetVertexStructuredBuffer(slot, buf); }
void GraphicsDeviceD3D12::SetComputeTexture3D(uint32_t slot, HexEngine::ITexture3D* tex)               { SetGeometryTexture3D(slot, tex); }
void GraphicsDeviceD3D12::SetComputeStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf) { SetVertexStructuredBuffer(slot, buf); }

void GraphicsDeviceD3D12::SetComputeRwTexture3D(uint32_t slot, HexEngine::ITexture3D* tex)
{
	if (slot >= RootSignatureD3D12::kUavCount) return;
	auto* t = static_cast<Texture3DD3D12*>(tex);
	if (t != nullptr) TransitionResource(t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	_bindings.uavs[slot] = t ? t->_uav : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (t && slot + 1 > _bindings.uavHighWater) _bindings.uavHighWater = slot + 1;
}
void GraphicsDeviceD3D12::SetComputeRwStructuredBuffer(uint32_t slot, HexEngine::IStructuredBuffer* buf, uint32_t)
{
	if (slot >= RootSignatureD3D12::kUavCount) return;
	auto* b = static_cast<StructuredBufferD3D12*>(buf);
	if (b != nullptr) TransitionResource(b, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	_bindings.uavs[slot] = b ? b->_uav : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if (b && slot + 1 > _bindings.uavHighWater) _bindings.uavHighWater = slot + 1;
}

void GraphicsDeviceD3D12::ClearGeometryTexture3D(uint32_t slot)        { if (slot < RootSignatureD3D12::kSrvCount) _bindings.srvs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearVertexStructuredBuffer(uint32_t slot)   { if (slot < RootSignatureD3D12::kSrvCount) _bindings.srvs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearComputeTexture3D(uint32_t slot)         { if (slot < RootSignatureD3D12::kSrvCount) _bindings.srvs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearComputeRwTexture3D(uint32_t slot)       { if (slot < RootSignatureD3D12::kUavCount) _bindings.uavs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearGeometryStructuredBuffer(uint32_t slot) { if (slot < RootSignatureD3D12::kSrvCount) _bindings.srvs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearComputeStructuredBuffer(uint32_t slot)  { if (slot < RootSignatureD3D12::kSrvCount) _bindings.srvs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }
void GraphicsDeviceD3D12::ClearComputeRwStructuredBuffer(uint32_t slot){ if (slot < RootSignatureD3D12::kUavCount) _bindings.uavs[slot] = D3D12_CPU_DESCRIPTOR_HANDLE{}; }

void GraphicsDeviceD3D12::SetTexture2DArray(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures)
{
	for (size_t i = 0; i < textures.size(); ++i) SetTexture2D(slot + (uint32_t)i, textures[i]);
}
void GraphicsDeviceD3D12::SetTexture2DArray(const std::vector<HexEngine::ITexture2D*>& textures)
{
	for (auto* tex : textures) SetTexture2D(tex);
}

void GraphicsDeviceD3D12::SetPixelShaderResource(uint32_t slot, HexEngine::ITexture2D* tex) { SetTexture2D(slot, tex); }
void GraphicsDeviceD3D12::SetPixelShaderResource(HexEngine::ITexture2D* tex)                 { SetTexture2D(tex); }
void GraphicsDeviceD3D12::SetPixelShaderResources(uint32_t slot, const std::vector<HexEngine::ITexture2D*>& textures) { SetTexture2DArray(slot, textures); }
void GraphicsDeviceD3D12::SetPixelShaderResources(const std::vector<HexEngine::ITexture2D*>& textures)                { SetTexture2DArray(textures); }
void GraphicsDeviceD3D12::UnbindAllPixelShaderResources()
{
	for (auto& h : _bindings.srvs) h = D3D12_CPU_DESCRIPTOR_HANDLE{};
	_bindings.srvHighWater = 0;
	_autoBindCursor = 0;
}

// ---- pre-draw / pre-dispatch flush ----------------------------------------

void GraphicsDeviceD3D12::ResetPendingForBeginFrame()
{
	_pending  = {};
	_bindings = {};
	_autoBindCursor = 0;
}

bool GraphicsDeviceD3D12::FlushGraphics()
{
	if (_cmdList == nullptr || _activeWindow == nullptr) return false;
	if (_pending.vs == nullptr || _pending.ps == nullptr) return false;

	GfxPsoKey key = {};
	key.vsBytecode  = _pending.vs->_bytecode.data();
	key.psBytecode  = _pending.ps->_bytecode.data();
	key.gsBytecode  = _pending.gs ? _pending.gs->_bytecode.data() : nullptr;
	key.inputLayout = _pending.inputLayout;
	key.blendState  = _blendState;
	key.depthState  = _depthState;
	key.cullingMode = _cullingMode;
	key.topology    = PrimitiveTopologyType(_pending.topology);

	if (_pending.rtCount == 0)
	{
		auto& bb = _activeWindow->backbuffers[_activeWindow->currentFrameIndex];
		key.rtCount      = 1;
		key.rtFormats[0] = bb._format;
	}
	else
	{
		key.rtCount = _pending.rtCount;
		for (uint32_t i = 0; i < _pending.rtCount; ++i)
		{
			auto* rt = static_cast<Texture2DD3D12*>(_pending.rtvs[i]);
			key.rtFormats[i] = rt ? rt->_format : DXGI_FORMAT_UNKNOWN;
		}
	}
	key.dsFormat    = _pending.dsv ? (DXGI_FORMAT)_pending.dsv->GetFormat() : DXGI_FORMAT_UNKNOWN;
	key.sampleCount = 1;

	auto* inputElems  = _pending.inputLayout ? _pending.inputLayout->_elements.data() : nullptr;
	uint32_t inputCnt = _pending.inputLayout ? (uint32_t)_pending.inputLayout->_elements.size() : 0;

	ID3D12PipelineState* pso = _psoCache.ResolveGraphics(key,
		key.vsBytecode, _pending.vs->_bytecode.size(),
		key.psBytecode, _pending.ps->_bytecode.size(),
		key.gsBytecode, _pending.gs ? _pending.gs->_bytecode.size() : 0,
		inputElems, inputCnt);
	if (pso == nullptr) return false;

	_cmdList->SetPipelineState(pso);
	_cmdList->SetGraphicsRootSignature(_rootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { _shaderVisibleHeap.GetHeap() };
	_cmdList->SetDescriptorHeaps(1, heaps);

	const UINT incr = _shaderVisibleHeap.GetDescriptorSize();
	auto bindTable = [&](uint32_t rootParam, uint32_t count, const D3D12_CPU_DESCRIPTOR_HANDLE* src)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		if (!_shaderVisibleHeap.Allocate(count, cpu, gpu)) return;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (src[i].ptr != 0)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += i * incr;
				_device->CopyDescriptorsSimple(1, dst, src[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
		}
		_cmdList->SetGraphicsRootDescriptorTable(rootParam, gpu);
	};
	bindTable(RootSignatureD3D12::kCbvRootParam, RootSignatureD3D12::kCbvCount, _bindings.cbvs);
	bindTable(RootSignatureD3D12::kSrvRootParam, RootSignatureD3D12::kSrvCount, _bindings.srvs);
	bindTable(RootSignatureD3D12::kUavRootParam, RootSignatureD3D12::kUavCount, _bindings.uavs);

	D3D12_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	switch (_pending.topology)
	{
	case HexEngine::PrimitiveTopology::PointList:     topo = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;     break;
	case HexEngine::PrimitiveTopology::LineList:      topo = D3D_PRIMITIVE_TOPOLOGY_LINELIST;      break;
	case HexEngine::PrimitiveTopology::LineStrip:     topo = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;     break;
	case HexEngine::PrimitiveTopology::TriangleList:  topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;  break;
	case HexEngine::PrimitiveTopology::TriangleStrip: topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
	default: break;
	}
	_cmdList->IASetPrimitiveTopology(topo);

	D3D12_VERTEX_BUFFER_VIEW vbViews[8] = {};
	uint32_t vbCount = 0;
	for (uint32_t i = 0; i < 8; ++i)
	{
		auto* vb = static_cast<VertexBufferD3D12*>(_pending.vbs[i]);
		if (vb != nullptr) { vbViews[i] = vb->_view; vbCount = i + 1; }
	}
	if (vbCount > 0) _cmdList->IASetVertexBuffers(0, vbCount, vbViews);

	if (_pending.ib != nullptr)
	{
		auto* ib = static_cast<IndexBufferD3D12*>(_pending.ib);
		_cmdList->IASetIndexBuffer(&ib->_view);
	}

	_pending.dirty = false;
	return true;
}

bool GraphicsDeviceD3D12::FlushCompute()
{
	if (_cmdList == nullptr || _pending.cs == nullptr) return false;

	CsPsoKey key;
	key.csBytecode = _pending.cs->_bytecode.data();
	ID3D12PipelineState* pso = _psoCache.ResolveCompute(key, key.csBytecode, _pending.cs->_bytecode.size());
	if (pso == nullptr) return false;

	_cmdList->SetPipelineState(pso);
	_cmdList->SetComputeRootSignature(_rootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { _shaderVisibleHeap.GetHeap() };
	_cmdList->SetDescriptorHeaps(1, heaps);

	const UINT incr = _shaderVisibleHeap.GetDescriptorSize();
	auto bindTable = [&](uint32_t rootParam, uint32_t count, const D3D12_CPU_DESCRIPTOR_HANDLE* src)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		if (!_shaderVisibleHeap.Allocate(count, cpu, gpu)) return;
		for (uint32_t i = 0; i < count; ++i)
		{
			if (src[i].ptr != 0)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += i * incr;
				_device->CopyDescriptorsSimple(1, dst, src[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
		}
		_cmdList->SetComputeRootDescriptorTable(rootParam, gpu);
	};
	bindTable(RootSignatureD3D12::kCbvRootParam, RootSignatureD3D12::kCbvCount, _bindings.cbvs);
	bindTable(RootSignatureD3D12::kSrvRootParam, RootSignatureD3D12::kSrvCount, _bindings.srvs);
	bindTable(RootSignatureD3D12::kUavRootParam, RootSignatureD3D12::kUavCount, _bindings.uavs);
	return true;
}

// ---- draw / dispatch ------------------------------------------------------

void GraphicsDeviceD3D12::DrawIndexed(uint32_t numIndices)
{
	if (!FlushGraphics()) return;
	_cmdList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
}
void GraphicsDeviceD3D12::DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount)
{
	if (!FlushGraphics()) return;
	_cmdList->DrawIndexedInstanced(numIndices, instanceCount, 0, 0, 0);
}
void GraphicsDeviceD3D12::DrawIndexedInstancedIndirect(void*, uint32_t)
{
	static bool warned = false;
	if (!warned) { LOG_WARN("D3D12: DrawIndexedInstancedIndirect not implemented yet (needs ID3D12CommandSignature)"); warned = true; }
}
void GraphicsDeviceD3D12::Draw(uint32_t vertexCount, int32_t startVertexLocation)
{
	if (!FlushGraphics()) return;
	_cmdList->DrawInstanced(vertexCount, 1, (UINT)startVertexLocation, 0);
}
void GraphicsDeviceD3D12::DrawInstancedIndirect(HexEngine::IStructuredBuffer*, uint32_t)
{
	static bool warned = false;
	if (!warned) { LOG_WARN("D3D12: DrawInstancedIndirect not implemented yet (needs ID3D12CommandSignature)"); warned = true; }
}
void GraphicsDeviceD3D12::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
	if (!FlushCompute()) return;
	_cmdList->Dispatch(gx, gy, gz);
}
void GraphicsDeviceD3D12::DispatchIndirect(HexEngine::IStructuredBuffer*, uint32_t)
{
	static bool warned = false;
	if (!warned) { LOG_WARN("D3D12: DispatchIndirect not implemented yet (needs ID3D12CommandSignature)"); warned = true; }
}

// ---- viewport / scissor ---------------------------------------------------

void GraphicsDeviceD3D12::SetViewport(const HexEngine::Viewport& v)
{
	_viewport = v;
	if (_cmdList == nullptr) return;
	D3D12_VIEWPORT vp = { v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth };
	_cmdList->RSSetViewports(1, &vp);
}

void GraphicsDeviceD3D12::SetViewports(const std::vector<HexEngine::Viewport>& vs)
{
	if (_cmdList == nullptr || vs.empty()) return;
	std::vector<D3D12_VIEWPORT> d3d(vs.size());
	for (size_t i = 0; i < vs.size(); ++i)
	{
		d3d[i] = { vs[i].x, vs[i].y, vs[i].width, vs[i].height, vs[i].minDepth, vs[i].maxDepth };
	}
	_cmdList->RSSetViewports((UINT)d3d.size(), d3d.data());
}

void GraphicsDeviceD3D12::SetScissorRect(const HexEngine::ScissorRect& r)
{
	_scissor = r;
	if (_cmdList == nullptr) return;
	D3D12_RECT rc = { r.left, r.top, r.right, r.bottom };
	_cmdList->RSSetScissorRects(1, &rc);
}

void GraphicsDeviceD3D12::SetScissorRects(const std::vector<HexEngine::ScissorRect>& rs)
{
	if (_cmdList == nullptr || rs.empty()) return;
	std::vector<D3D12_RECT> d3d(rs.size());
	for (size_t i = 0; i < rs.size(); ++i) d3d[i] = { rs[i].left, rs[i].top, rs[i].right, rs[i].bottom };
	_cmdList->RSSetScissorRects((UINT)d3d.size(), d3d.data());
}

void GraphicsDeviceD3D12::ClearScissorRect()
{
	if (_cmdList == nullptr || _activeWindow == nullptr) return;
	D3D12_RECT rc = { 0, 0, (LONG)_activeWindow->width, (LONG)_activeWindow->height };
	_cmdList->RSSetScissorRects(1, &rc);
}
