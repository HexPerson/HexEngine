
// Phase B4: state setters, pre-draw flush, draw / dispatch.
// Split out from GraphicsDeviceD3D12.cpp to keep that file's resource-creation
// / lifecycle code readable. Everything in here is GraphicsDeviceD3D12 member
// implementations; no new types.

#include "GraphicsDeviceD3D12.hpp"
#include "FormatsD3D12.hpp"
#include "StructuredBufferD3D12.hpp"
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

void GraphicsDeviceD3D12::UnbindAsRenderTargetIfBound(HexEngine::ITexture2D* tex)
{
	if (tex == nullptr || _cmdList == nullptr) return;
	bool wasRT = false;
	for (uint32_t i = 0; i < _pending.rtCount; ++i)
	{
		if (_pending.rtvs[i] == tex) wasRT = true;
	}
	if (_pending.dsv == tex) wasRT = true;
	if (!wasRT) return;

	// Drop the OMSetRenderTargets binding so the SRV transition is safe.
	// The engine's existing render-pass code calls SetRenderTarget(new) before
	// the next draw - which will re-OMSetRenderTargets and transition the new
	// RT into RENDER_TARGET state. Same contract the D3D11 plugin assumes.
	_pending.rtCount = 0;
	for (auto& r : _pending.rtvs) r = nullptr;
	_pending.dsv = nullptr;
	_cmdList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
}

void GraphicsDeviceD3D12::SetTexture2D(uint32_t slot, HexEngine::ITexture2D* tex)
{
	if (slot >= RootSignatureD3D12::kSrvCount) return;
	auto* t = static_cast<Texture2DD3D12*>(tex);
	if (t != nullptr)
	{
		UnbindAsRenderTargetIfBound(t); // hazard: SRV-and-RTV on the same resource
		TransitionResource(t, (D3D12_RESOURCE_STATES)(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
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
	// One-shot bring-up diagnostics. The "still no geometry" debugging path
	// needs to know which guard is rejecting draws. Each LOG_INFO fires only
	// once per app run via a static bool, so this isn't log spam in steady
	// state. Pull these out once D3D12 reaches scene parity.
	static bool warnedNoCmdList = false, warnedNoWindow = false, warnedNoVS = false, warnedNoPS = false;
	static bool firstFlushOK = false;

	if (_cmdList == nullptr)     { if (!warnedNoCmdList) { LOG_WARN("D3D12 FlushGraphics: no command list - draw skipped"); warnedNoCmdList = true; } return false; }
	if (_activeWindow == nullptr) { if (!warnedNoWindow)  { LOG_WARN("D3D12 FlushGraphics: no active window - draw skipped"); warnedNoWindow = true; } return false; }
	if (_pending.vs == nullptr)  { if (!warnedNoVS)      { LOG_WARN("D3D12 FlushGraphics: no vertex shader bound - draw skipped. Check that shaders were rebuilt for v2 .hcs (DXIL) and that ShaderSystem found the DXIL_SM6 blob."); warnedNoVS = true; } return false; }
	if (_pending.ps == nullptr)  { if (!warnedNoPS)      { LOG_WARN("D3D12 FlushGraphics: no pixel shader bound - draw skipped"); warnedNoPS = true; } return false; }

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
		// Compact: only feed non-null RTs into the PSO key. Engine code can
		// leave nulls in _pending.rtvs between valid entries (legacy sparse
		// binding semantics from the D3D11 shim), but D3D12 PSO descriptors
		// don't accept DXGI_FORMAT_UNKNOWN in the middle of NumRenderTargets
		// - it triggers RENDERTARGETVIEW_NOT_SET and the resulting PSO
		// produces a GPU hang on first use.
		key.rtCount = 0;
		for (uint32_t i = 0; i < 8 && key.rtCount < _pending.rtCount; ++i)
		{
			auto* rt = static_cast<Texture2DD3D12*>(_pending.rtvs[i]);
			if (rt == nullptr) continue;
			key.rtFormats[key.rtCount++] = rt->_format;
		}
		if (key.rtCount == 0)
		{
			// All entries were null - fall back to the backbuffer format so
			// the PSO at least has a valid single RT, matching the
			// rtCount==0 branch above.
			auto& bb = _activeWindow->backbuffers[_activeWindow->currentFrameIndex];
			key.rtCount      = 1;
			key.rtFormats[0] = bb._format;
		}
	}
	// Depth textures are typically created as R32_TYPELESS / R24G8_TYPELESS so
	// the same resource can be sampled as an SRV and bound as a DSV. The
	// PSO's DSVFormat field needs the TYPED depth view format
	// (D32_FLOAT / D24_UNORM_S8_UINT) - otherwise the GPU sees a typeless
	// format and the debug layer flags
	// DEPTH_STENCIL_FORMAT_MISMATCH_PIPELINE_STATE. GetDsvFormatD3D12 does
	// this mapping and is a no-op for already-typed depth formats.
	key.dsFormat = _pending.dsv
		? HexEngine::GetDsvFormatD3D12((DXGI_FORMAT)_pending.dsv->GetFormat())
		: DXGI_FORMAT_UNKNOWN;
	key.sampleCount = 1;

	auto* inputElems  = _pending.inputLayout ? _pending.inputLayout->_elements.data() : nullptr;
	uint32_t inputCnt = _pending.inputLayout ? (uint32_t)_pending.inputLayout->_elements.size() : 0;

	ID3D12PipelineState* pso = _psoCache.ResolveGraphics(key,
		key.vsBytecode, _pending.vs->_bytecode.size(),
		key.psBytecode, _pending.ps->_bytecode.size(),
		key.gsBytecode, _pending.gs ? _pending.gs->_bytecode.size() : 0,
		inputElems, inputCnt);
	if (pso == nullptr)
	{
		static bool warnedPsoFail = false;
		if (!warnedPsoFail)
		{
			LOG_WARN("D3D12 FlushGraphics: PSO resolve returned null - check earlier 'PsoCache::ResolveGraphics CreateGraphicsPipelineState failed' lines for the HR. Most likely RT/DS format mismatch, shader bytecode invalid for SM 6.0, or input layout incompatible with vertex shader.");
			warnedPsoFail = true;
		}
		return false;
	}

	if (!firstFlushOK)
	{
		LOG_INFO("D3D12 FlushGraphics: first successful flush. PSO bound, descriptor tables set. Subsequent draws should now hit the GPU.");
		firstFlushOK = true;
	}

	_cmdList->SetPipelineState(pso);
	_cmdList->SetGraphicsRootSignature(_rootSig.Get());
	ID3D12DescriptorHeap* heaps[] = { _shaderVisibleHeap.GetHeap() };
	_cmdList->SetDescriptorHeaps(1, heaps);

	const UINT incr = _shaderVisibleHeap.GetDescriptorSize();
	auto bindTable = [&](uint32_t rootParam, uint32_t count, const D3D12_CPU_DESCRIPTOR_HANDLE* src, D3D12_CPU_DESCRIPTOR_HANDLE nullDesc)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		if (!_shaderVisibleHeap.Allocate(count, cpu, gpu)) return;
		for (uint32_t i = 0; i < count; ++i)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += i * incr;
			// Always write a descriptor - either the bound resource's CPU
			// descriptor or the pre-created null one. The shader-visible
			// heap is NOT zero-initialised between frames; leaving stale
			// bits from prior draws in slots the current shader's root
			// signature reserves causes undefined GPU behaviour (TDR has
			// been observed for trivial shaders like UIBasic).
			const D3D12_CPU_DESCRIPTOR_HANDLE source = (src[i].ptr != 0) ? src[i] : nullDesc;
			_device->CopyDescriptorsSimple(1, dst, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		_cmdList->SetGraphicsRootDescriptorTable(rootParam, gpu);
	};
	bindTable(RootSignatureD3D12::kCbvRootParam, RootSignatureD3D12::kCbvCount, _bindings.cbvs, _nullCbv);
	bindTable(RootSignatureD3D12::kSrvRootParam, RootSignatureD3D12::kSrvCount, _bindings.srvs, _nullSrv);
	bindTable(RootSignatureD3D12::kUavRootParam, RootSignatureD3D12::kUavCount, _bindings.uavs, _nullUav);

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
	auto bindTable = [&](uint32_t rootParam, uint32_t count, const D3D12_CPU_DESCRIPTOR_HANDLE* src, D3D12_CPU_DESCRIPTOR_HANDLE nullDesc)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
		if (!_shaderVisibleHeap.Allocate(count, cpu, gpu)) return;
		for (uint32_t i = 0; i < count; ++i)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += i * incr;
			// See FlushGraphics: unbound slots must still carry a valid
			// (null) descriptor or the GPU reads stale bits and may TDR.
			const D3D12_CPU_DESCRIPTOR_HANDLE source = (src[i].ptr != 0) ? src[i] : nullDesc;
			_device->CopyDescriptorsSimple(1, dst, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
		_cmdList->SetComputeRootDescriptorTable(rootParam, gpu);
	};
	bindTable(RootSignatureD3D12::kCbvRootParam, RootSignatureD3D12::kCbvCount, _bindings.cbvs, _nullCbv);
	bindTable(RootSignatureD3D12::kSrvRootParam, RootSignatureD3D12::kSrvCount, _bindings.srvs, _nullSrv);
	bindTable(RootSignatureD3D12::kUavRootParam, RootSignatureD3D12::kUavCount, _bindings.uavs, _nullUav);
	return true;
}

// ---- draw / dispatch ------------------------------------------------------

void GraphicsDeviceD3D12::DrawIndexed(uint32_t numIndices)
{
	// Mirror D3D11 plugin: implicitly bind the per-object constant buffer at
	// slot b1 (VS + PS) for every draw. The engine writes per-object state
	// (worldMatrix, material flags, entityId, etc.) into this CB right before
	// calling Draw, but never explicitly binds it - it relies on the device
	// to wire it up. Without this, shaders see stale or uninitialised b1.
	if (auto* cb = _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer])
	{
		SetConstantBufferVS(1, cb);
		SetConstantBufferPS(1, cb);
	}
	if (!FlushGraphics()) { UnbindAllPixelShaderResources(); return; }
	// Record into the draw trace ring just before submission so a GPU hang
	// preserves the identity of the failing draw.
	{
		const uint64_t idx = _drawTraceCount++;
		auto& slot = _drawTrace[idx % kDrawTraceCapacity];
		slot.drawIndex     = idx;
		slot.vsBytecode    = _pending.vs ? (void*)_pending.vs->_bytecode.data() : nullptr;
		slot.psBytecode    = _pending.ps ? (void*)_pending.ps->_bytecode.data() : nullptr;
		slot.indexCount    = numIndices;
		slot.instanceCount = 1;
		slot.rtCount       = _pending.rtCount;
		slot.dsBound       = _pending.dsv ? 1 : 0;
	}
	_cmdList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
	// Mirror the D3D11 plugin: every Draw* clears pixel-shader resource
	// bindings and resets the auto-bind cursor to 0. Engine code (GuiRenderer,
	// SceneRenderer, etc.) relies on this implicit reset so the next batch's
	// parameterless SetTexture2D(tex) lands at slot 0, where shaders expect
	// their primary sampled texture. Without it the cursor grows unbounded
	// and textures end up bound to slots the shaders never read.
	UnbindAllPixelShaderResources();
}
void GraphicsDeviceD3D12::DrawIndexedInstanced(uint32_t numIndices, uint32_t instanceCount)
{
	if (auto* cb = _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer])
	{
		SetConstantBufferVS(1, cb);
		SetConstantBufferPS(1, cb);
	}
	if (!FlushGraphics()) { UnbindAllPixelShaderResources(); return; }
	_cmdList->DrawIndexedInstanced(numIndices, instanceCount, 0, 0, 0);
	UnbindAllPixelShaderResources();
}
void GraphicsDeviceD3D12::DrawIndexedInstancedIndirect(void* argsBuffer, uint32_t alignedByteOffset)
{
	// The void* form takes a RAW native args resource with no wrapper, so we
	// can't state-track it. It is also unreachable under D3D12 by design: the
	// only abstraction caller (Scene::RenderInstanced) gates the indirect path
	// on GetBackend()==D3D11, and the particle system fills an ID3D11Buffer
	// directly. The state-trackable IStructuredBuffer* forms below
	// (DrawInstancedIndirect / DispatchIndirect) are the D3D12 path. If a
	// future caller routes a D3D12 args buffer here it MUST leave it in
	// D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT first.
	if (argsBuffer == nullptr || !EnsureIndirectSignatures()) { UnbindAllPixelShaderResources(); return; }
	if (auto* cb = _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer])
	{
		SetConstantBufferVS(1, cb);
		SetConstantBufferPS(1, cb);
	}
	if (!FlushGraphics()) { UnbindAllPixelShaderResources(); return; }
	auto* res = reinterpret_cast<ID3D12Resource*>(argsBuffer);
	_cmdList->ExecuteIndirect(_drawIndexedIndirectSig.Get(), 1, res, alignedByteOffset, nullptr, 0);
	UnbindAllPixelShaderResources();
}
void GraphicsDeviceD3D12::Draw(uint32_t vertexCount, int32_t startVertexLocation)
{
	if (auto* cb = _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer])
	{
		SetConstantBufferVS(1, cb);
		SetConstantBufferPS(1, cb);
	}
	if (!FlushGraphics()) { UnbindAllPixelShaderResources(); return; }
	_cmdList->DrawInstanced(vertexCount, 1, (UINT)startVertexLocation, 0);
	UnbindAllPixelShaderResources();
}
void GraphicsDeviceD3D12::DrawInstancedIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset)
{
	auto* sb = static_cast<StructuredBufferD3D12*>(argsBuffer);
	if (sb == nullptr || sb->_resource == nullptr || !EnsureIndirectSignatures()) return;
	if (auto* cb = _engineConstantBuffers[(uint32_t)HexEngine::EngineConstantBuffer::PerObjectBuffer])
	{
		SetConstantBufferVS(1, cb);
		SetConstantBufferPS(1, cb);
	}
	if (!FlushGraphics()) { UnbindAllPixelShaderResources(); return; }
	// The args buffer was last written by a compute UAV; move it to
	// INDIRECT_ARGUMENT before ExecuteIndirect reads it. (Within a command
	// list the buffer won't auto-promote - it's no longer in COMMON - so the
	// explicit transition is required.)
	TransitionResource(sb, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	_cmdList->ExecuteIndirect(_drawIndirectSig.Get(), 1, sb->_resource.Get(), alignedByteOffset, nullptr, 0);
	UnbindAllPixelShaderResources();
}
void GraphicsDeviceD3D12::Dispatch(uint32_t gx, uint32_t gy, uint32_t gz)
{
	if (!FlushCompute()) return;
	_cmdList->Dispatch(gx, gy, gz);
}
void GraphicsDeviceD3D12::DispatchIndirect(HexEngine::IStructuredBuffer* argsBuffer, uint32_t alignedByteOffset)
{
	auto* sb = static_cast<StructuredBufferD3D12*>(argsBuffer);
	if (sb == nullptr || sb->_resource == nullptr || !EnsureIndirectSignatures()) return;
	if (!FlushCompute()) return;
	TransitionResource(sb, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	_cmdList->ExecuteIndirect(_dispatchIndirectSig.Get(), 1, sb->_resource.Get(), alignedByteOffset, nullptr, 0);
}

bool GraphicsDeviceD3D12::EnsureIndirectSignatures()
{
	if (_drawIndexedIndirectSig && _drawIndirectSig && _dispatchIndirectSig)
		return true;
	if (_device == nullptr)
		return false;

	auto make = [&](D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride,
		Microsoft::WRL::ComPtr<ID3D12CommandSignature>& out) -> bool
	{
		if (out) return true;
		D3D12_INDIRECT_ARGUMENT_DESC arg = {};
		arg.Type = type;
		D3D12_COMMAND_SIGNATURE_DESC desc = {};
		desc.ByteStride       = stride;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs   = &arg;
		// Root signature is only required when a command signature changes root
		// arguments; these are pure draw/dispatch signatures, so pass nullptr.
		HRESULT hr = _device->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&out));
		if (FAILED(hr)) { LOG_WARN("D3D12: CreateCommandSignature failed (hr=0x%08X)", (unsigned)hr); return false; }
		return true;
	};

	const bool ok =
		make(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), _drawIndexedIndirectSig) &&
		make(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,         sizeof(D3D12_DRAW_ARGUMENTS),         _drawIndirectSig) &&
		make(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,     sizeof(D3D12_DISPATCH_ARGUMENTS),     _dispatchIndirectSig);
	return ok;
}

void GraphicsDeviceD3D12::CopyStructureCount(
	HexEngine::IStructuredBuffer* sourceBuffer,
	HexEngine::IStructuredBuffer* destinationBuffer,
	uint32_t destinationByteOffset)
{
	// D3D11's CopyStructureCount(dst, offset, srcUAV) extracts the hidden
	// append/consume counter of `src` into `dst`. D3D12 stores that counter as
	// a separate 4-byte resource (StructuredBufferD3D12::_counterResource), so
	// we copy those 4 bytes with CopyBufferRegion, bracketing it with the
	// required state transitions.
	auto* src = static_cast<StructuredBufferD3D12*>(sourceBuffer);
	auto* dst = static_cast<StructuredBufferD3D12*>(destinationBuffer);
	if (src == nullptr || dst == nullptr || _cmdList == nullptr) return;
	if (src->_counterResource == nullptr || dst->_resource == nullptr) return;

	auto barrier = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		if (before == after) return;
		D3D12_RESOURCE_BARRIER b = {};
		b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource   = res;
		b.Transition.StateBefore = before;
		b.Transition.StateAfter  = after;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		_cmdList->ResourceBarrier(1, &b);
	};

	// State assumptions, valid for the real usage pattern (a compute pass
	// appends into `src` this frame, then CopyStructureCount feeds an indirect
	// draw): both the counter and the main buffer were just written via their
	// UAVs, so both are in UNORDERED_ACCESS at this point in the command list.
	// We transition each to its copy role, copy the 4-byte counter, then put
	// them back. The counter returns to UNORDERED_ACCESS (next append); the
	// destination args buffer also returns to UNORDERED_ACCESS and we keep
	// _currentState truthful so the following ExecuteIndirect's
	// UNORDERED_ACCESS->INDIRECT_ARGUMENT transition has the correct
	// StateBefore.
	barrier(src->_counterResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	barrier(dst->_resource.Get(),        dst->_currentState,                    D3D12_RESOURCE_STATE_COPY_DEST);

	_cmdList->CopyBufferRegion(dst->_resource.Get(), destinationByteOffset,
		src->_counterResource.Get(), 0, sizeof(uint32_t));

	barrier(src->_counterResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	src->_counterState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	barrier(dst->_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dst->_currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
