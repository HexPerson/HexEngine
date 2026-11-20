#include "AnimationEditor.hpp"
#include "AnimationViewport.hpp"
#include "AnimationTimeline.hpp"
#include "BoneHierarchyView.hpp"
#include "../EditorUI.hpp"
#include "../Actions/SceneView.hpp"
#include <HexEngine.Core/GUI/Elements/TabView.hpp>
#include <HexEngine.Core/GUI/Elements/TabItem.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/GUI/Elements/ContextMenu.hpp>

namespace HexEditor
{
	using namespace HexEngine;
	using AnimationUtils::Track;
	using AnimationUtils::NodePose;

	namespace
	{
		constexpr int32_t kLeftWidth = 230;
		constexpr int32_t kRightWidth = 300;
		constexpr int32_t kBottomHeight = 270;
		constexpr int32_t kTransportHeight = 34;

		std::vector<AnimationEditor*>& OpenEditors()
		{
			static std::vector<AnimationEditor*> s_editors;
			return s_editors;
		}

		fs::path NormalisePath(const fs::path& path)
		{
			std::error_code ec;
			const fs::path canonical = fs::weakly_canonical(path, ec);
			return ec ? path : canonical;
		}

		bool IsTypingInField()
		{
			return dynamic_cast<LineEdit*>(g_pEnv->GetUIManager().GetInputFocus()) != nullptr;
		}

		math::Vector4 ReadKey(const AnimChannel& channel, Track track, float time, bool& found)
		{
			found = false;
			switch (track)
			{
			case Track::Position:
				if (const int32_t i = AnimationUtils::FindKeyAt(channel.positionKeys, time); i >= 0)
				{
					found = true;
					const auto& v = channel.positionKeys[i].second;
					return math::Vector4(v.x, v.y, v.z, 0.0f);
				}
				break;
			case Track::Rotation:
				if (const int32_t i = AnimationUtils::FindKeyAt(channel.rotationKeys, time); i >= 0)
				{
					found = true;
					return math::Vector4(channel.rotationKeys[i].second);
				}
				break;
			case Track::Scale:
				if (const int32_t i = AnimationUtils::FindKeyAt(channel.scaleKeys, time); i >= 0)
				{
					found = true;
					const auto& v = channel.scaleKeys[i].second;
					return math::Vector4(v.x, v.y, v.z, 0.0f);
				}
				break;
			}
			return math::Vector4::Zero;
		}

		void WriteKey(AnimChannel& channel, Track track, float time, const math::Vector4& value)
		{
			switch (track)
			{
			case Track::Position: AnimationUtils::SetKey(channel.positionKeys, time, math::Vector3(value.x, value.y, value.z)); break;
			case Track::Rotation:
			{
				math::Quaternion q(value);
				q.Normalize();
				AnimationUtils::SetKey(channel.rotationKeys, time, q);
				break;
			}
			case Track::Scale: AnimationUtils::SetKey(channel.scaleKeys, time, math::Vector3(value.x, value.y, value.z)); break;
			}
		}

		bool EraseKey(AnimChannel& channel, Track track, float time)
		{
			switch (track)
			{
			case Track::Position: return AnimationUtils::RemoveKey(channel.positionKeys, time);
			case Track::Rotation: return AnimationUtils::RemoveKey(channel.rotationKeys, time);
			default: return AnimationUtils::RemoveKey(channel.scaleKeys, time);
			}
		}

		// Modal-ish strip shown over the editor when closing with unsaved changes.
		class ClosePrompt : public Element
		{
		public:
			ClosePrompt(Element* parent, const Point& position, const Point& size, const std::wstring& message) :
				Element(parent, position, size), _message(message) {}

			void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override
			{
				const auto abs = GetAbsolutePosition();
				renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, AnimEditorColours::Panel);
				renderer->Frame(abs.x, abs.y, _size.x, _size.y, 2, AnimEditorColours::Unkeyed);
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Small, abs.x + 12, abs.y + 20, AnimEditorColours::Text, FontAlign::CentreUD, _message);
			}

			bool OnInputEvent(InputEvent event, InputData* data) override
			{
				// Swallow clicks on the prompt itself so they don't reach the views below.
				if (event == InputEvent::MouseDown && IsMouseOver(true))
					return true;
				return false;
			}

		private:
			std::wstring _message;
		};
	}

	// ---- opening -------------------------------------------------------------------------

	bool AnimationEditor::Open(const fs::path& meshPath)
	{
		if (g_pUIManager == nullptr || g_pUIManager->GetSceneView() == nullptr)
			return false;

		auto* sceneView = g_pUIManager->GetSceneView();
		const fs::path key = NormalisePath(meshPath);

		for (auto* editor : OpenEditors())
		{
			if (NormalisePath(editor->Doc().GetPath()) == key)
			{
				sceneView->SetActiveWorkspaceTab(editor->_tab);
				return true;
			}
		}

		auto document = std::make_unique<AnimationDocument>();
		std::wstring error;
		if (!document->Open(meshPath, error))
		{
			LOG_WARN("Animation editor: %S", error.c_str());
			return false;
		}

		auto* tab = sceneView->AddWorkspaceTab(L"Animation: " + meshPath.stem().wstring());
		if (tab == nullptr)
			return false;

		const int32_t tabHeaderHeight = g_pEnv->GetUIManager().GetRenderer()->_style.tab_height;
		const auto tabSize = tab->GetSize();
		auto* editor = new AnimationEditor(tab,
			Point(-tab->GetPosition().x, tabHeaderHeight),
			Point(tabSize.x, std::max(1, tabSize.y - tabHeaderHeight)),
			tab, std::move(document));

		tab->SetClosable(true, [editor]() { return editor->RequestClose(); });
		sceneView->SetActiveWorkspaceTab(tab);
		return true;
	}

	AnimationEditor::AnimationEditor(Element* parent, const Point& position, const Point& size, TabItem* tab, std::unique_ptr<AnimationDocument> document) :
		Element(parent, position, size),
		_doc(std::move(document)),
		_tab(tab)
	{
		OpenEditors().push_back(this);

		_tabVisible = tab != nullptr && tab->IsSelected();
		if (tab != nullptr)
		{
			tab->SetOnSelectedChanged([this](bool selected)
			{
				_tabVisible = selected;
				if (!selected)
					SetPlaying(false);
			});
		}

		BuildPreviewScene();
		BuildUi();

		_renderHook = OffscreenRenderHooks::Add([this]()
		{
			if (_tabVisible)
				RenderPreview();
		});

		FrameCharacter();
		SetStatus(std::format(L"{} clip(s). Click a joint to select it, drag the gizmo to pose, K to key.", _doc->GetClipCount()));
	}

	AnimationEditor::~AnimationEditor()
	{
		if (_renderHook != 0)
			OffscreenRenderHooks::Remove(_renderHook);

		if (_menu != nullptr)
			_menu->DeleteMe();

		DestroyPreviewScene();

		auto& editors = OpenEditors();
		editors.erase(std::remove(editors.begin(), editors.end(), this), editors.end());
	}

	// ---- preview scene -----------------------------------------------------------------------

	void AnimationEditor::BuildPreviewScene()
	{
		auto& sceneManager = *g_pEnv->_sceneManager;

		// Never registered: SceneManager's update/render loops repoint the "current
		// scene" at every scene they touch, which the rest of the editor relies on.
		_scene = sceneManager.CreateEmptyScene(false, nullptr, false);
		g_pEnv->_debugGui->RemoveCallback(_scene.get());
		_scene->SetName(L"AnimationEditorPreview");
		_scene->SetFlags(SceneFlags::Renderable | SceneFlags::PreviewLighting);

		// Components look up the current scene while they attach (as IconService does).
		auto previous = sceneManager.GetCurrentScene();
		sceneManager.SetActiveScene(_scene);

		_scene->CreateDefaultSunLight();
		if (auto* sun = _scene->GetSunLight(); sun != nullptr && sun->GetEntity() != nullptr)
			sun->GetEntity()->SetFlag(EntityFlags::DoNotSave);

		auto* cameraEntity = _scene->CreateEntity("AnimEditorCamera", math::Vector3(0.0f, 1.0f, -4.0f));
		cameraEntity->SetFlag(EntityFlags::DoNotSave);
		_camera = cameraEntity->AddComponent<Camera>();
		_scene->SetMainCamera(_camera);
		_camera->SetPespectiveParameters(45.0f, 1.0f, 0.02f, 2000.0f);

		_character = _scene->CreateEntity("AnimEditorCharacter");
		_character->SetFlag(EntityFlags::DoNotSave);
		auto* meshComponent = _character->AddComponent<StaticMeshComponent>();
		// A preview prop that is re-posed every frame must never enter GI: it isn't
		// part of the level, and its changes would thrash the voxel triangle cache.
		// Set before SetMesh so the mesh assignment doesn't notify either.
		meshComponent->SetExcludeFromGI(true);
		meshComponent->SetMesh(_doc->GetMesh());
		_skeleton = _character->AddComponent<SkeletalAnimationComponent>();
		_skeleton->SetAnimationData(_doc->GetMesh(), _doc->GetMesh()->GetAnimationData());
		_skeleton->SetEditorMode(true);

		sceneManager.SetActiveScene(previous);
	}

	void AnimationEditor::DestroyPreviewScene()
	{
		_character = nullptr;
		_skeleton = nullptr;
		_camera = nullptr;
		if (_scene != nullptr && g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
		{
			if (g_pEnv->_sceneManager->GetCurrentScene() == _scene)
				g_pEnv->_sceneManager->SetActiveScene(nullptr);

			// Destroy() frees the entities, components and their GPU resources. The empty
			// Scene object itself is parked rather than released: a scene made by
			// CreateEmptyScene has no resource loader, so its deleter could only log a
			// leak warning (and would drop the "" entry from the resource cache).
			_scene->Destroy();
			// Heap-allocated and never freed on purpose: a static vector would run the
			// deleter during process exit, after the environment is gone.
			static auto* s_destroyedPreviewScenes = new std::vector<std::shared_ptr<Scene>>();
			s_destroyedPreviewScenes->push_back(std::move(_scene));
		}
	}

	void AnimationEditor::SyncSkeletonToEditor()
	{
		if (_skeleton == nullptr)
			return;

		_skeleton->SetAnimationIndex((uint32_t)std::max(0, _doc->GetClipIndex()));
		_skeleton->SetEditorTime(_time);
		_skeleton->EvaluateNow();
	}

	void AnimationEditor::RenderPreview()
	{
		if (_scene == nullptr || _camera == nullptr)
			return;

		auto& sceneManager = *g_pEnv->_sceneManager;
		auto previous = sceneManager.GetCurrentScene();
		sceneManager.SetActiveScene(_scene);

		const float dt = g_pEnv->_timeManager ? g_pEnv->_timeManager->_frameTime : 0.016f;
		if (_playing)
		{
			const Animation* clip = _doc->GetClip();
			const float length = GetClipLength();
			_time += dt * (clip ? AnimationUtils::GetTicksPerSecond(*clip) : 25.0f) * _speed;
			if (_time > length)
			{
				if (_loop)
					_time = length > 0.0f ? fmod(_time, length) : 0.0f;
				else
				{
					_time = length;
					_playing = false;
				}
			}
		}

		SyncSkeletonToEditor();
		_viewport->ApplyCamera(_camera);
		_scene->Update(dt);
		RebuildNodes();

		// Seed visibility by hand (the preview holds a single character) - the same
		// approach IconService takes for its private scene.
		if (auto* pvs = _camera->GetPVS(); pvs != nullptr)
		{
			pvs->ClearPVS();
			pvs->AddEntity(_character);
			if (auto* sun = _scene->GetSunLight(); sun != nullptr)
			{
				for (int32_t i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
				{
					sun->GetPVS(i)->ClearPVS();
					sun->GetPVS(i)->AddEntity(_character);
				}
			}

			PVSParams params;
			params.lodPartition = _camera->GetFarZ();
			params.shape.frustum.sm = _camera->GetFrustum();
			params.shapeType = PVSParams::ShapeType::Frustum;
			params.camera = _camera;
			pvs->CalculateVisibility(_scene.get(), params);
		}

		// The pose above is all the skeleton overlay needs; only render the scene
		// when the mesh is actually shown. PreviewLighting runs the deferred light
		// pass without the post chain - a plain non-post render is unlit albedo.
		if (_showMesh)
		{
			g_pEnv->GetGraphicsDevice().SetClearColour(math::Color(HEX_RGB_TO_FLOAT3(46, 49, 54)));
			g_pEnv->_sceneRenderer->RenderScene(_scene.get(), _camera, SceneFlags::Renderable | SceneFlags::PreviewLighting);
		}

		sceneManager.SetActiveScene(previous);
	}

	void AnimationEditor::RebuildNodes()
	{
		_nodes.clear();
		if (_skeleton == nullptr)
			return;

		const auto& nodes = _skeleton->GetEvaluatedNodes();
		const auto& globals = _skeleton->GetEvaluatedNodeGlobals();
		if (nodes.size() != globals.size())
			return;

		// Character sits at the origin with an identity transform, so mesh space is
		// world space. Composed matrices are column-convention: transpose to row form.
		const math::Matrix& clipRoot = _skeleton->GetEvaluatedClipRoot();
		_nodes.resize(nodes.size());
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			const auto& source = nodes[i];
			auto& view = _nodes[i];
			view.name = source.channel->nodeName;
			view.parent = source.parent;
			view.bone = source.bone;
			view.keyed = AnimationUtils::HasKeys(*source.channel);
			view.depth = source.parent >= 0 ? _nodes[source.parent].depth + 1 : 0;

			int32_t boneParent = source.parent;
			while (boneParent >= 0 && nodes[boneParent].bone < 0)
				boneParent = nodes[boneParent].parent;
			view.boneParent = boneParent;

			view.world = (clipRoot * globals[i]).Transpose();
			view.parentWorld = source.parent >= 0 ? (clipRoot * globals[source.parent]).Transpose() : clipRoot.Transpose();
		}
	}

	int32_t AnimationEditor::FindNode(const std::string& name) const
	{
		if (name.empty())
			return -1;
		for (int32_t i = 0; i < (int32_t)_nodes.size(); ++i)
		{
			if (_nodes[i].name == name)
				return i;
		}
		return -1;
	}

	void AnimationEditor::FrameCharacter()
	{
		if (_viewport == nullptr || _doc->GetMesh() == nullptr)
			return;

		const auto& aabb = _doc->GetMesh()->GetAABB();
		const math::Vector3 centre(aabb.Center.x, aabb.Center.y, aabb.Center.z);
		const math::Vector3 extents(aabb.Extents.x, aabb.Extents.y, aabb.Extents.z);
		_viewport->Frame(centre, std::max(0.25f, extents.Length()));
	}

	// ---- time ----------------------------------------------------------------------------------

	float AnimationEditor::GetClipLength() const
	{
		const Animation* clip = _doc->GetClip();
		if (clip == nullptr)
			return 1.0f;
		return std::max({ 1.0f, clip->duration, AnimationUtils::GetLastKeyTime(*clip) });
	}

	void AnimationEditor::SetTime(float ticks)
	{
		ticks = std::max(0.0f, ticks);
		if (std::abs(ticks - _time) < 1e-5f)
			return;

		// Like most animation tools, unkeyed pose changes don't survive a time change.
		if (_skeleton != nullptr && !_skeleton->GetPoseOverrides().empty())
		{
			_skeleton->ClearPoseOverrides();
			SetStatus(L"Unkeyed pose changes were discarded (key them with K before changing frame).");
		}
		_time = ticks;
	}

	void AnimationEditor::SetPlaying(bool playing)
	{
		if (playing && _skeleton != nullptr && !_skeleton->GetPoseOverrides().empty())
		{
			_skeleton->ClearPoseOverrides();
			SetStatus(L"Unkeyed pose changes were discarded.");
		}
		if (playing && _time >= GetClipLength() - 1e-3f)
			_time = 0.0f;
		_playing = playing;
	}

	void AnimationEditor::StepFrames(int32_t frames)
	{
		SetPlaying(false);
		SetTime(std::clamp(std::round(_time) + (float)frames, 0.0f, GetClipLength()));
	}

	void AnimationEditor::JumpToKey(bool next)
	{
		const Animation* clip = _doc->GetClip();
		if (clip == nullptr)
			return;

		std::vector<float> times;
		const auto gather = [&](const AnimChannel& channel)
		{
			for (const auto& k : channel.positionKeys) times.push_back(k.first);
			for (const auto& k : channel.rotationKeys) times.push_back(k.first);
			for (const auto& k : channel.scaleKeys) times.push_back(k.first);
		};

		if (const AnimChannel* selected = AnimationUtils::FindChannel(*clip, _selectedNode); selected != nullptr)
			gather(*selected);
		else
			for (const auto& channel : clip->channels)
				gather(channel);

		std::sort(times.begin(), times.end());
		const float eps = 1e-3f;
		float target = _time;
		if (next)
		{
			auto it = std::upper_bound(times.begin(), times.end(), _time + eps);
			if (it != times.end()) target = *it;
		}
		else
		{
			auto it = std::lower_bound(times.begin(), times.end(), _time - eps);
			if (it != times.begin()) target = *(--it);
		}
		SetPlaying(false);
		SetTime(target);
	}

	std::wstring AnimationEditor::DescribeTime() const
	{
		const Animation* clip = _doc->GetClip();
		const float fps = clip ? AnimationUtils::GetTicksPerSecond(*clip) : 25.0f;
		return std::format(L"Frame {:.1f} / {:.0f}   ({:.2f}s @ {:.0f} fps)", _time, GetClipLength(), _time / fps, fps);
	}

	// ---- selection & pose ------------------------------------------------------------------------

	void AnimationEditor::SelectNode(const std::string& name)
	{
		_selectedNode = name;
		if (_bones != nullptr)
			_bones->ScrollToSelection();
	}

	bool AnimationEditor::GetDisplayedPose(const std::string& node, NodePose& out) const
	{
		if (_skeleton != nullptr)
		{
			if (const NodePose* pose = _skeleton->GetPoseOverride(node); pose != nullptr)
			{
				out = *pose;
				return true;
			}
		}

		const Animation* clip = _doc->GetClip();
		const AnimChannel* channel = clip ? AnimationUtils::FindChannel(*clip, node) : nullptr;
		if (channel == nullptr)
			return false;

		out = AnimationUtils::SampleChannel(*channel, _time);
		return true;
	}

	void AnimationEditor::SetPoseOverride(const std::string& node, const NodePose& pose)
	{
		if (_skeleton != nullptr)
			_skeleton->SetPoseOverride(node, pose);
	}

	bool AnimationEditor::HasPoseOverride(const std::string& node) const
	{
		return _skeleton != nullptr && _skeleton->GetPoseOverride(node) != nullptr;
	}

	void AnimationEditor::ClearPoseOverride(const std::string& node)
	{
		if (_skeleton != nullptr)
			_skeleton->ClearPoseOverride(node);
	}

	void AnimationEditor::ResetPose()
	{
		if (_skeleton != nullptr)
			_skeleton->ClearPoseOverrides();
		SetStatus(L"Pose reset to the clip.");
	}

	void AnimationEditor::CopyPose()
	{
		_poseClipboard.clear();
		for (const auto& node : _nodes)
		{
			NodePose pose;
			if (node.bone >= 0 && GetDisplayedPose(node.name, pose))
				_poseClipboard[node.name] = pose;
		}
		SetStatus(std::format(L"Copied the pose of {} bones.", _poseClipboard.size()));
	}

	void AnimationEditor::PastePose()
	{
		if (_poseClipboard.empty())
		{
			SetStatus(L"No pose copied.", true);
			return;
		}
		for (const auto& [name, pose] : _poseClipboard)
			SetPoseOverride(name, pose);
		SetStatus(L"Pose pasted - use Key All to key it at this frame.");
	}

	// ---- key edits -------------------------------------------------------------------------------

	std::map<std::string, AnimationEditor::TrackState> AnimationEditor::CaptureTracks(const std::set<std::string>& nodes) const
	{
		std::map<std::string, TrackState> states;
		const Animation* clip = _doc->GetClip();
		if (clip == nullptr)
			return states;

		for (const auto& name : nodes)
		{
			if (const AnimChannel* channel = AnimationUtils::FindChannel(*clip, name); channel != nullptr)
				states[name] = { !channel->positionKeys.empty(), !channel->rotationKeys.empty(), !channel->scaleKeys.empty() };
		}
		return states;
	}

	void AnimationEditor::FixupPartialChannels(const std::map<std::string, TrackState>& before)
	{
		Animation* clip = _doc->GetClip();
		if (clip == nullptr)
			return;

		for (const auto& [name, state] : before)
		{
			AnimChannel* channel = AnimationUtils::FindChannel(*clip, name);
			if (channel == nullptr || !AnimationUtils::HasKeys(*channel))
				continue;	// fully unkeyed again: the runtime uses the bind pose

			const bool wasUnkeyed = !state.position && !state.rotation && !state.scale;
			const NodePose bind = AnimationUtils::DecomposeLocal(channel->nodeTransform);
			if (channel->positionKeys.empty() && (state.position || wasUnkeyed))
				AnimationUtils::SetKey(channel->positionKeys, 0.0f, bind.translation);
			if (channel->rotationKeys.empty() && (state.rotation || wasUnkeyed))
				AnimationUtils::SetKey(channel->rotationKeys, 0.0f, bind.rotation);
			if (channel->scaleKeys.empty() && (state.scale || wasUnkeyed))
				AnimationUtils::SetKey(channel->scaleKeys, 0.0f, bind.scale);
		}
	}

	void AnimationEditor::ExtendClipToKeys()
	{
		if (Animation* clip = _doc->GetClip(); clip != nullptr)
			clip->duration = std::max(clip->duration, AnimationUtils::GetLastKeyTime(*clip));
	}

	void AnimationEditor::KeyNodes(const std::vector<std::string>& nodes, bool position, bool rotation, bool scale, const std::wstring& label)
	{
		Animation* clip = _doc->GetClip();
		if (clip == nullptr || nodes.empty())
			return;

		// Sample every pose before writing any key.
		std::vector<std::pair<AnimChannel*, NodePose>> work;
		for (const auto& name : nodes)
		{
			NodePose pose;
			AnimChannel* channel = AnimationUtils::FindChannel(*clip, name);
			if (channel != nullptr && GetDisplayedPose(name, pose))
				work.push_back({ channel, pose });
		}
		if (work.empty())
			return;

		const float time = std::round(_time * 1000.0f) / 1000.0f;
		_doc->BeginEdit(label);
		for (auto& [channel, pose] : work)
		{
			AnimationUtils::SetPoseKey(*channel, time, pose, position, rotation, scale);
			ClearPoseOverride(channel->nodeName);
		}
		ExtendClipToKeys();
		_doc->EndEdit();

		SetStatus(std::format(L"{}: keyed {} node(s) at frame {:.1f}.", label, work.size(), time));
	}

	void AnimationEditor::KeySelectedNode()
	{
		if (_selectedNode.empty())
		{
			SetStatus(L"Select a bone to key it.", true);
			return;
		}
		KeyNodes({ _selectedNode }, true, true, true, L"Key bone");
	}

	void AnimationEditor::KeyAllBones()
	{
		std::vector<std::string> names;
		for (const auto& node : _nodes)
		{
			if (node.bone >= 0 || node.keyed || HasPoseOverride(node.name))
				names.push_back(node.name);
		}
		KeyNodes(names, true, true, true, L"Key all bones");
	}

	void AnimationEditor::DeleteSelectedKeys()
	{
		Animation* clip = _doc->GetClip();
		if (clip == nullptr || _selectedKeys.empty())
			return;

		std::set<std::string> touched;
		for (const auto& key : _selectedKeys)
			touched.insert(key.node);
		const auto before = CaptureTracks(touched);

		_doc->BeginEdit(L"Delete keys");
		size_t removed = 0;
		for (const auto& key : _selectedKeys)
		{
			if (AnimChannel* channel = AnimationUtils::FindChannel(*clip, key.node); channel != nullptr)
				removed += EraseKey(*channel, key.track, key.time) ? 1 : 0;
		}
		FixupPartialChannels(before);
		_doc->EndEdit();

		_selectedKeys.clear();
		SetStatus(std::format(L"Deleted {} key(s).", removed));
	}

	void AnimationEditor::MoveSelectedKeys(float deltaTicks)
	{
		Animation* clip = _doc->GetClip();
		if (clip == nullptr || _selectedKeys.empty() || std::abs(deltaTicks) < 1e-5f)
			return;

		struct Moving { AnimKeyRef key; math::Vector4 value; };
		std::vector<Moving> moving;
		for (const auto& key : _selectedKeys)
		{
			const AnimChannel* channel = AnimationUtils::FindChannel(*clip, key.node);
			bool found = false;
			const math::Vector4 value = channel ? ReadKey(*channel, key.track, key.time, found) : math::Vector4::Zero;
			if (found)
				moving.push_back({ key, value });
		}

		_doc->BeginEdit(L"Move keys");
		for (const auto& m : moving)
		{
			if (AnimChannel* channel = AnimationUtils::FindChannel(*clip, m.key.node); channel != nullptr)
				EraseKey(*channel, m.key.track, m.key.time);
		}

		std::set<AnimKeyRef> moved;
		for (const auto& m : moving)
		{
			AnimChannel* channel = AnimationUtils::FindChannel(*clip, m.key.node);
			if (channel == nullptr)
				continue;
			const float time = std::max(0.0f, m.key.time + deltaTicks);
			WriteKey(*channel, m.key.track, time, m.value);
			moved.insert({ m.key.node, m.key.track, time });
		}
		ExtendClipToKeys();
		_doc->EndEdit();

		_selectedKeys = std::move(moved);
		SetStatus(std::format(L"Moved {} key(s) by {:.2f} frames.", moving.size(), deltaTicks));
	}

	void AnimationEditor::CopySelectedKeys()
	{
		const Animation* clip = _doc->GetClip();
		if (clip == nullptr || _selectedKeys.empty())
		{
			SetStatus(L"Select keys to copy.", true);
			return;
		}

		float first = FLT_MAX;
		for (const auto& key : _selectedKeys)
			first = std::min(first, key.time);

		_keyClipboard.clear();
		for (const auto& key : _selectedKeys)
		{
			const AnimChannel* channel = AnimationUtils::FindChannel(*clip, key.node);
			bool found = false;
			const math::Vector4 value = channel ? ReadKey(*channel, key.track, key.time, found) : math::Vector4::Zero;
			if (found)
				_keyClipboard.push_back({ key.node, key.track, key.time - first, value });
		}
		SetStatus(std::format(L"Copied {} key(s).", _keyClipboard.size()));
	}

	void AnimationEditor::PasteKeys()
	{
		Animation* clip = _doc->GetClip();
		if (clip == nullptr || _keyClipboard.empty())
		{
			SetStatus(L"No keys copied.", true);
			return;
		}

		std::set<std::string> touched;
		for (const auto& entry : _keyClipboard)
			touched.insert(entry.node);
		const auto before = CaptureTracks(touched);

		const float base = std::round(_time);
		std::set<AnimKeyRef> pasted;
		size_t skipped = 0;

		_doc->BeginEdit(L"Paste keys");
		for (const auto& entry : _keyClipboard)
		{
			AnimChannel* channel = AnimationUtils::FindChannel(*clip, entry.node);
			if (channel == nullptr)
			{
				++skipped;		// e.g. pasting into a clip without that node
				continue;
			}
			const float time = base + entry.relativeTime;
			WriteKey(*channel, entry.track, time, entry.value);
			pasted.insert({ entry.node, entry.track, time });
		}
		FixupPartialChannels(before);
		ExtendClipToKeys();
		_doc->EndEdit();

		_selectedKeys = std::move(pasted);
		SetStatus(skipped > 0
			? std::format(L"Pasted {} key(s); {} skipped (node not in this clip).", _selectedKeys.size(), skipped)
			: std::format(L"Pasted {} key(s) at frame {:.0f}.", _selectedKeys.size(), base));
	}

	void AnimationEditor::SetKeyValue(const AnimKeyRef& key, const math::Vector4& value, bool liveDrag)
	{
		Animation* clip = _doc->GetClip();
		AnimChannel* channel = clip ? AnimationUtils::FindChannel(*clip, key.node) : nullptr;
		if (channel == nullptr)
			return;

		if (!_doc->IsEditing())
			_doc->BeginEdit(L"Edit key value");

		WriteKey(*channel, key.track, key.time, value);
		_doc->Touch();

		if (!liveDrag)
			_doc->EndEdit();
	}

	// ---- history / file ----------------------------------------------------------------------------

	void AnimationEditor::Undo()
	{
		const std::wstring label = _doc->GetUndoLabel();
		if (_doc->Undo())
		{
			if (_skeleton) _skeleton->ClearPoseOverrides();
			_selectedKeys.clear();
			SetStatus(L"Undo: " + label);
		}
		else
		{
			SetStatus(L"Nothing to undo.");
		}
	}

	void AnimationEditor::Redo()
	{
		if (_doc->Redo())
		{
			if (_skeleton) _skeleton->ClearPoseOverrides();
			_selectedKeys.clear();
			SetStatus(L"Redo: " + _doc->GetUndoLabel());
		}
		else
		{
			SetStatus(L"Nothing to redo.");
		}
	}

	bool AnimationEditor::Save()
	{
		std::wstring error;
		if (!_doc->Save(error))
		{
			SetStatus(L"Save failed: " + error, true);
			return false;
		}
		SetStatus(L"Saved " + _doc->GetPath().filename().wstring());
		return true;
	}

	void AnimationEditor::SetStatus(const std::wstring& text, bool isError)
	{
		_status = text;
		_statusIsError = isError;
	}

	bool AnimationEditor::RequestClose()
	{
		if (!_doc->IsDirty())
			return true;

		ShowClosePrompt(true);
		return false;
	}

	void AnimationEditor::ShowClosePrompt(bool show)
	{
		if (_closePrompt != nullptr)
		{
			_closePrompt->DeleteMe();
			_closePrompt = nullptr;
		}
		if (!show)
			return;

		const Point size(520, 76);
		_closePrompt = new ClosePrompt(this, Point((_size.x - size.x) / 2, (_size.y - kBottomHeight - size.y) / 2), size,
			_doc->GetPath().filename().wstring() + L" has unsaved animation changes.");

		const auto closeTab = [this]()
		{
			if (auto* view = dynamic_cast<TabView*>(_tab->GetParent()); view != nullptr)
				view->RemoveTab(_tab);
		};

		new Button(_closePrompt, Point(12, 42), Point(150, 24), L"Save and close", [this, closeTab](Button*)
		{
			if (Save())
				closeTab();
			return true;
		});
		new Button(_closePrompt, Point(172, 42), Point(170, 24), L"Discard and close", [this, closeTab](Button*)
		{
			// Edits went straight into the shared mesh data; put the saved state back.
			_doc->DiscardChanges();
			closeTab();
			return true;
		});
		new Button(_closePrompt, Point(352, 42), Point(150, 24), L"Cancel", [this](Button*)
		{
			ShowClosePrompt(false);
			return true;
		});
	}

	// ---- UI ----------------------------------------------------------------------------------------

	void AnimationEditor::BuildUi()
	{
		const int32_t W = _size.x;
		const int32_t H = _size.y;
		const int32_t topH = H - kBottomHeight;

		_bones = new BoneHierarchyView(this, Point(0, 0), Point(kLeftWidth, topH), this);
		_viewportPos = Point(kLeftWidth, 0);
		_viewportSize = Point(std::max(64, W - kLeftWidth - kRightWidth), topH);
		_viewport = new AnimationViewport(this, _viewportPos, _viewportSize, this);
		_timeline = new AnimationTimeline(this, Point(0, topH + kTransportHeight), Point(W, kBottomHeight - kTransportHeight), this);

		// ---- transport bar --------------------------------------------------------
		int32_t x = 8;
		const int32_t ty = topH + 5;
		const auto addButton = [&](int32_t width, const std::wstring& label, std::function<bool(Button*)> fn) -> Button*
		{
			auto* button = new Button(this, Point(x, ty), Point(width, 24), label, std::move(fn));
			x += width + 4;
			return button;
		};

		addButton(30, L"|<", [this](Button*) { SetPlaying(false); SetTime(0.0f); return true; });
		addButton(30, L"<K", [this](Button*) { JumpToKey(false); return true; });
		addButton(26, L"<", [this](Button*) { StepFrames(-1); return true; });
		_playButton = addButton(56, L"Play", [this](Button*) { SetPlaying(!_playing); return true; });
		addButton(26, L">", [this](Button*) { StepFrames(1); return true; });
		addButton(30, L"K>", [this](Button*) { JumpToKey(true); return true; });
		addButton(30, L">|", [this](Button*) { SetPlaying(false); SetTime(GetClipLength()); return true; });
		x += 10;
		_loopBox = new Checkbox(this, Point(x, ty + 3), Point(60, 18), L"Loop", &_loop);
		x += 70;
		addButton(70, L"Key (K)", [this](Button*) { KeySelectedNode(); return true; });
		addButton(70, L"Key All", [this](Button*) { KeyAllBones(); return true; });
		addButton(84, L"Delete Keys", [this](Button*) { DeleteSelectedKeys(); return true; });
		x += 6;
		_autoKeyBox = new Checkbox(this, Point(x, ty + 3), Point(84, 18), L"Auto-key", &_autoKey);
		x += 96;
		new Checkbox(this, Point(x, ty + 3), Point(60, 18), L"Mesh", &_showMesh);
		x += 70;
		new Checkbox(this, Point(x, ty + 3), Point(84, 18), L"Skeleton", &_showSkeleton);
		x += 96;
		_moveButton = addButton(56, L"Move", [this](Button*) { _viewport->SetGizmoMode(AnimationViewport::GizmoMode::Translate); return true; });
		_rotateButton = addButton(56, L"Rotate", [this](Button*) { _viewport->SetGizmoMode(AnimationViewport::GizmoMode::Rotate); return true; });
		x += 10;
		_dopeButton = addButton(78, L"Dopesheet", [this](Button*) { _timeline->SetMode(AnimationTimeline::Mode::Dopesheet); return true; });
		_curveButton = addButton(60, L"Curves", [this](Button*) { _timeline->SetMode(AnimationTimeline::Mode::Curves); return true; });
		addButton(34, L"Pos", [this](Button*) { _timeline->SetMode(AnimationTimeline::Mode::Curves); _timeline->SetCurveTrack(Track::Position); return true; });
		addButton(34, L"Rot", [this](Button*) { _timeline->SetMode(AnimationTimeline::Mode::Curves); _timeline->SetCurveTrack(Track::Rotation); return true; });
		addButton(40, L"Scale", [this](Button*) { _timeline->SetMode(AnimationTimeline::Mode::Curves); _timeline->SetCurveTrack(Track::Scale); return true; });

		// ---- right-hand properties panel ---------------------------------------------
		_propsPos = Point(W - kRightWidth, 0);
		_propsWidth = kRightWidth;
		const int32_t px = _propsPos.x + 10;
		const int32_t pw = kRightWidth - 20;

		new Button(this, Point(px + pw - 90, 8), Point(90, 22), L"Clips...", [this](Button*) { OpenClipMenu(); return true; });

		_clipName = new LineEdit(this, Point(px, 38), Point(pw, 20), L"Name");
		_clipName->SetDoesCallbackWaitForReturn(true);
		_clipName->SetOnInputFn([this](LineEdit*, const std::wstring& value)
		{
			if (!value.empty())
			{
				_doc->RenameClip(ws2s(value));
				SetStatus(L"Clip renamed.");
			}
		});

		_clipLength = new DragFloat(this, Point(px, 64), Point(pw, 20), L"Length (frames)", &_lengthField, 1.0f, 100000.0f, 1.0f, 0);
		_clipLength->SetOnDrag([this](float, float, float) { _doc->SetClipTiming(_lengthField, _fpsField); });
		_clipFps = new DragFloat(this, Point(px, 90), Point(pw, 20), L"Frames / second", &_fpsField, 1.0f, 240.0f, 0.5f, 1);
		_clipFps->SetOnDrag([this](float, float, float) { _doc->SetClipTiming(_lengthField, _fpsField); });

		const int32_t third = (pw - 8) / 3;
		new Button(this, Point(px, 118), Point(third, 22), L"New", [this](Button*)
		{
			_doc->NewClip(std::format("Clip{}", _doc->GetClipCount() + 1));
			SetTime(0.0f);
			SetStatus(L"New clip (bind pose).");
			return true;
		});
		new Button(this, Point(px + third + 4, 118), Point(third, 22), L"Duplicate", [this](Button*)
		{
			_doc->DuplicateClip();
			SetStatus(L"Clip duplicated.");
			return true;
		});
		new Button(this, Point(px + (third + 4) * 2, 118), Point(third, 22), L"Delete", [this](Button*)
		{
			if (_doc->DeleteClip())
				SetStatus(L"Clip deleted.");
			else
				SetStatus(L"A mesh needs at least one clip.", true);
			_selectedKeys.clear();
			return true;
		});

		// Selected bone's local pose. Edits are unkeyed until keyed (K / Key buttons).
		const wchar_t* axes[3] = { L"X", L"Y", L"Z" };
		const float mins[3] = { -100000.0f, -360.0f, 0.0001f };
		const float maxs[3] = { 100000.0f, 360.0f, 1000.0f };
		const float scales[3] = { 0.01f, 0.5f, 0.01f };
		const int32_t decimals[3] = { 3, 1, 3 };
		const int32_t fieldW = (pw - 70) / 3;
		for (int32_t row = 0; row < 3; ++row)
		{
			for (int32_t c = 0; c < 3; ++c)
			{
				auto* field = new DragFloat(this, Point(px + 66 + c * (fieldW + 2), 184 + row * 26), Point(fieldW, 20), axes[c],
					&_poseFields[row * 3 + c], mins[row], maxs[row], scales[row], decimals[row]);
				field->SetOnDrag([this](float, float, float) { ApplyPropertyFields(); });
			}
		}

		const int32_t half = (pw - 4) / 2;
		new Button(this, Point(px, 266), Point(half, 22), L"Key Bone (K)", [this](Button*) { KeySelectedNode(); return true; });
		new Button(this, Point(px + half + 4, 266), Point(half, 22), L"Key All Bones", [this](Button*) { KeyAllBones(); return true; });
		new Button(this, Point(px, 292), Point(third, 22), L"Reset Pose", [this](Button*) { ResetPose(); return true; });
		new Button(this, Point(px + third + 4, 292), Point(third, 22), L"Copy Pose", [this](Button*) { CopyPose(); return true; });
		new Button(this, Point(px + (third + 4) * 2, 292), Point(third, 22), L"Paste Pose", [this](Button*) { PastePose(); return true; });

		new Button(this, Point(px, 330), Point(third, 22), L"Copy Keys", [this](Button*) { CopySelectedKeys(); return true; });
		new Button(this, Point(px + third + 4, 330), Point(third, 22), L"Paste Keys", [this](Button*) { PasteKeys(); return true; });
		new Button(this, Point(px + (third + 4) * 2, 330), Point(third, 22), L"Frame (F)", [this](Button*) { FrameCharacter(); return true; });

		new Button(this, Point(px, 368), Point(third, 24), L"Undo", [this](Button*) { Undo(); return true; });
		new Button(this, Point(px + third + 4, 368), Point(third, 24), L"Redo", [this](Button*) { Redo(); return true; });
		new Button(this, Point(px + (third + 4) * 2, 368), Point(third, 24), L"Save", [this](Button*) { Save(); return true; });
	}

	void AnimationEditor::OpenClipMenu()
	{
		if (_menu != nullptr)
		{
			_menu->DeleteMe();
			_menu = nullptr;
		}

		auto* root = g_pEnv->GetUIManager().GetRootElement();
		if (root == nullptr || _doc->GetData() == nullptr)
			return;

		int32_t mx = 0, my = 0;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);
		_menu = new ContextMenu(root, Point(mx - root->GetAbsolutePosition().x, my - root->GetAbsolutePosition().y));

		const auto& clips = _doc->GetData()->_animations;
		for (int32_t i = 0; i < (int32_t)clips.size(); ++i)
		{
			const std::wstring label = (i == _doc->GetClipIndex() ? L"> " : L"   ") +
				(clips[i].name.empty() ? std::format(L"Clip {}", i) : s2ws(clips[i].name));
			_menu->AddItem(new ContextItem(label, [this, i](const std::wstring&)
			{
				if (_skeleton) _skeleton->ClearPoseOverrides();
				_doc->SetClipIndex(i);
				_selectedKeys.clear();
				_playing = false;
				_time = 0.0f;
				SetStatus(L"Editing clip: " + s2ws(_doc->GetClip()->name));
			}));
		}
	}

	void AnimationEditor::RefreshClipFields()
	{
		const Animation* clip = _doc->GetClip();
		if (clip == nullptr)
			return;

		const uint64_t revision = _doc->GetData()->_revision;
		if (_syncedClip == _doc->GetClipIndex() && _syncedRevision == revision)
			return;

		_syncedClip = _doc->GetClipIndex();
		_syncedRevision = revision;
		_lengthField = std::max(1.0f, clip->duration);
		_fpsField = AnimationUtils::GetTicksPerSecond(*clip);
		if (!_clipName->IsInputFocus())
			_clipName->SetValue(s2ws(clip->name));
	}

	void AnimationEditor::SyncPropertyFields()
	{
		NodePose pose;
		if (_selectedNode.empty() || !GetDisplayedPose(_selectedNode, pose))
			return;

		const math::Vector3 euler = pose.rotation.ToEuler();
		const float values[9] = {
			pose.translation.x, pose.translation.y, pose.translation.z,
			ToDegree(euler.x), ToDegree(euler.y), ToDegree(euler.z),
			pose.scale.x, pose.scale.y, pose.scale.z };
		std::copy(std::begin(values), std::end(values), _poseFields);
	}

	void AnimationEditor::ApplyPropertyFields()
	{
		if (_selectedNode.empty())
			return;

		NodePose pose;
		pose.translation = math::Vector3(_poseFields[0], _poseFields[1], _poseFields[2]);
		pose.rotation = math::Quaternion::CreateFromYawPitchRoll(math::Vector3(ToRadian(_poseFields[3]), ToRadian(_poseFields[4]), ToRadian(_poseFields[5])));
		pose.scale = math::Vector3(_poseFields[6], _poseFields[7], _poseFields[8]);
		SetPlaying(false);
		SetPoseOverride(_selectedNode, pose);
	}

	// ---- per-frame UI ------------------------------------------------------------------------------

	void AnimationEditor::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto abs = GetAbsolutePosition();
		auto* font = renderer->_style.font.get();
		const uint8_t tiny = (uint8_t)Style::FontSize::Tiny;

		RefreshClipFields();
		SyncPropertyFields();

		const auto highlight = [](Button* button, bool on)
		{
			if (button == nullptr) return;
			if (on) button->SetHighlightOverride(AnimEditorColours::Selection);
			else button->RemoveHighlightOverride();
		};
		highlight(_playButton, _playing);
		highlight(_moveButton, _viewport->GetGizmoMode() == AnimationViewport::GizmoMode::Translate);
		highlight(_rotateButton, _viewport->GetGizmoMode() == AnimationViewport::GizmoMode::Rotate);
		highlight(_dopeButton, _timeline->GetMode() == AnimationTimeline::Mode::Dopesheet);
		highlight(_curveButton, _timeline->GetMode() == AnimationTimeline::Mode::Curves);

		renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, AnimEditorColours::Background);

		// Transport strip
		const int32_t topH = _size.y - kBottomHeight;
		renderer->FillQuad(abs.x, abs.y + topH, _size.x, kTransportHeight, AnimEditorColours::Panel);
		renderer->Frame(abs.x, abs.y + topH, _size.x, kTransportHeight, 1, AnimEditorColours::PanelBorder);
		renderer->PrintText(font, tiny, abs.x + _size.x - 12, abs.y + topH + kTransportHeight / 2, AnimEditorColours::Text,
			FontAlign::CentreUD | FontAlign::Right, DescribeTime());

		// Properties panel
		const int32_t px = abs.x + _propsPos.x;
		renderer->FillQuad(px, abs.y, _propsWidth, topH, AnimEditorColours::Panel);
		renderer->Frame(px, abs.y, _propsWidth, topH, 1, AnimEditorColours::PanelBorder);

		const Animation* clip = _doc->GetClip();
		const std::wstring clipTitle = clip ? std::format(L"Clip {}/{}: {}", _doc->GetClipIndex() + 1, _doc->GetClipCount(), s2ws(clip->name)) : L"No clip";
		renderer->PrintText(font, tiny, px + 10, abs.y + 19, AnimEditorColours::Text, FontAlign::CentreUD, clipTitle);

		renderer->FillQuad(px + 10, abs.y + 150, _propsWidth - 20, 1, AnimEditorColours::PanelBorder);
		renderer->PrintText(font, tiny, px + 10, abs.y + 166, AnimEditorColours::Text, FontAlign::CentreUD,
			_selectedNode.empty() ? std::wstring(L"No bone selected") : L"Bone: " + s2ws(_selectedNode) + (HasPoseOverride(_selectedNode) ? L"  (unkeyed)" : L""));
		const wchar_t* rowNames[3] = { L"Position", L"Rotation", L"Scale" };
		for (int32_t row = 0; row < 3; ++row)
			renderer->PrintText(font, tiny, px + 10, abs.y + 194 + row * 26, AnimEditorColours::TextDim, FontAlign::CentreUD, rowNames[row]);

		renderer->FillQuad(px + 10, abs.y + 322, _propsWidth - 20, 1, AnimEditorColours::PanelBorder);
		renderer->FillQuad(px + 10, abs.y + 360, _propsWidth - 20, 1, AnimEditorColours::PanelBorder);

		// File state + status
		renderer->PrintText(font, tiny, px + 10, abs.y + 406, _doc->IsDirty() ? AnimEditorColours::Unkeyed : AnimEditorColours::TextDim, FontAlign::CentreUD,
			_doc->IsDirty() ? L"Unsaved changes (Ctrl+S)" : L"Saved");
		renderer->PrintText(font, tiny, px + 10, abs.y + 424, _statusIsError ? AnimEditorColours::Playhead : AnimEditorColours::TextDim, FontAlign::CentreUD, _status);

		renderer->PrintText(font, tiny, px + 10, abs.y + topH - 54, AnimEditorColours::TextDim, FontAlign::CentreUD, L"Space play   K key   Del delete keys");
		renderer->PrintText(font, tiny, px + 10, abs.y + topH - 38, AnimEditorColours::TextDim, FontAlign::CentreUD, L"Left/Right step   Ctrl+Z/Y undo/redo");
		renderer->PrintText(font, tiny, px + 10, abs.y + topH - 22, AnimEditorColours::TextDim, FontAlign::CentreUD, L"Ctrl+C/V copy/paste keys   Ctrl+S save");
	}

	bool AnimationEditor::HandleShortcut(int32_t key)
	{
		const bool ctrl = g_pEnv->_inputSystem->IsCtrlDown();
		if (ctrl)
		{
			switch (key)
			{
			case 'Z': Undo(); return true;
			case 'Y': Redo(); return true;
			case 'C': CopySelectedKeys(); return true;
			case 'V': PasteKeys(); return true;
			case 'S': Save(); return true;
			default: return false;
			}
		}

		switch (key)
		{
		case VK_SPACE: SetPlaying(!_playing); return true;
		case 'K': KeySelectedNode(); return true;
		case VK_DELETE: DeleteSelectedKeys(); return true;
		case VK_LEFT: StepFrames(-1); return true;
		case VK_RIGHT: StepFrames(1); return true;
		case VK_HOME: SetPlaying(false); SetTime(0.0f); return true;
		case VK_END: SetPlaying(false); SetTime(GetClipLength()); return true;
		default: return false;
		}
	}

	bool AnimationEditor::OnInputEvent(InputEvent event, InputData* data)
	{
		if (Element::OnInputEvent(event, data))
			return true;

		if (!_tabVisible)
			return false;

		const auto abs = GetAbsolutePosition();
		const bool over = Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y);

		if (event == InputEvent::KeyDown && over && !IsTypingInField())
			return HandleShortcut(data->KeyDown.key);

		// Keep clicks inside the editor from falling through to the hidden scene view.
		if ((event == InputEvent::MouseDown || event == InputEvent::MouseUp || event == InputEvent::MouseWheel) && over)
			return true;

		return false;
	}
}
