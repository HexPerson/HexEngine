#include "AmbientEmitterComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <cmath>

AmbientEmitterComponent::AmbientEmitterComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

AmbientEmitterComponent::AmbientEmitterComponent(HexEngine::Entity* entity, AmbientEmitterComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_soundPath = copy->_soundPath;
		_volume = copy->_volume;
		_radius = copy->_radius;
		_times = copy->_times;
		_windScale = copy->_windScale;
		_rainScale = copy->_rainScale;
		_offset = copy->_offset;
	}
}

void AmbientEmitterComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_soundPath", _soundPath);
	file->Serialize(data, "_volume", _volume);
	file->Serialize(data, "_radius", _radius);
	file->Serialize(data, "_times", _times);
	file->Serialize(data, "_windScale", _windScale);
	file->Serialize(data, "_rainScale", _rainScale);
	file->Serialize(data, "_offset", _offset);
}

void AmbientEmitterComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_soundPath", _soundPath);
	file->Deserialize(data, "_volume", _volume);
	file->Deserialize(data, "_radius", _radius);
	file->Deserialize(data, "_times", _times);
	file->Deserialize(data, "_windScale", _windScale);
	file->Deserialize(data, "_rainScale", _rainScale);
	file->Deserialize(data, "_offset", _offset);
}

bool AmbientEmitterComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	auto* search = new AssetSearch(widget, widget->GetNextPos(), Point(w, 84), L"Sound (mono loop)", { ResourceType::Audio },
		[this](AssetSearch*, const AssetSearchResult& result)
		{
			const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
			_soundPath = chosen.string();
		});
	if (!_soundPath.empty())
		search->SetValue(std::wstring(_soundPath.begin(), _soundPath.end()));
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Volume", &_volume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Radius (m)", &_radius, 1.0f, 500.0f, 0.5f, 1);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Times (0 any, 1 day, 2 night)", &_times, 0, 2, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Wind Scale", &_windScale, 0.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Rain Scale", &_rainScale, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Offset Y (m)", &_offset.y, -50.0f, 50.0f, 0.1f, 2);
	return true;
}

void AmbientEmitterComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode() || HexEngine::g_pEnv->_debugRenderer == nullptr || GetEntity() == nullptr)
		return;
	// Audible radius as a horizontal circle.
	const math::Vector3 centre = math::Vector3::Transform(_offset, GetEntity()->GetWorldTM());
	constexpr int kSegments = 48;
	const float step = 6.28318530718f / (float)kSegments;
	math::Vector3 prev = centre + math::Vector3(_radius, 0.0f, 0.0f);
	for (int i = 1; i <= kSegments; ++i)
	{
		const float a = step * (float)i;
		const math::Vector3 p = centre + math::Vector3(std::cos(a) * _radius, 0.0f, std::sin(a) * _radius);
		HexEngine::g_pEnv->_debugRenderer->DrawLine(prev, p, math::Color(0.4f, 1.0f, 0.6f, 0.8f));
		prev = p;
	}
}
