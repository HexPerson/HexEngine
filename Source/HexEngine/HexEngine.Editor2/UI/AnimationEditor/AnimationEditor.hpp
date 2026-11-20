#pragma once

#include "AnimationDocument.hpp"
#include <map>
#include <set>

namespace HexEngine
{
	class TabItem;
	class Button;
	class Checkbox;
	class DragFloat;
	class LineEdit;
	class ContextMenu;
}

namespace HexEditor
{
	class AnimationViewport;
	class AnimationTimeline;
	class BoneHierarchyView;

	namespace AnimEditorColours
	{
		inline const math::Color Background		= math::Color(HEX_RGBA_TO_FLOAT4(30, 32, 36, 255));
		inline const math::Color Panel			= math::Color(HEX_RGBA_TO_FLOAT4(38, 41, 46, 255));
		inline const math::Color PanelBorder	= math::Color(HEX_RGBA_TO_FLOAT4(60, 64, 72, 255));
		inline const math::Color Row			= math::Color(HEX_RGBA_TO_FLOAT4(44, 47, 53, 255));
		inline const math::Color RowAlt			= math::Color(HEX_RGBA_TO_FLOAT4(40, 43, 48, 255));
		inline const math::Color Selection		= math::Color(HEX_RGBA_TO_FLOAT4(70, 110, 170, 255));
		inline const math::Color Text			= math::Color(HEX_RGBA_TO_FLOAT4(210, 212, 216, 255));
		inline const math::Color TextDim		= math::Color(HEX_RGBA_TO_FLOAT4(130, 134, 140, 255));
		inline const math::Color Key			= math::Color(HEX_RGBA_TO_FLOAT4(220, 200, 120, 255));
		inline const math::Color KeySelected	= math::Color(HEX_RGBA_TO_FLOAT4(255, 140, 40, 255));
		inline const math::Color Playhead		= math::Color(HEX_RGBA_TO_FLOAT4(230, 70, 60, 255));
		inline const math::Color Bone			= math::Color(HEX_RGBA_TO_FLOAT4(200, 200, 205, 255));
		inline const math::Color BoneHover		= math::Color(HEX_RGBA_TO_FLOAT4(150, 210, 255, 255));
		inline const math::Color BoneSelected	= math::Color(HEX_RGBA_TO_FLOAT4(255, 170, 40, 255));
		inline const math::Color AxisX			= math::Color(HEX_RGBA_TO_FLOAT4(230, 70, 70, 255));
		inline const math::Color AxisY			= math::Color(HEX_RGBA_TO_FLOAT4(90, 210, 90, 255));
		inline const math::Color AxisZ			= math::Color(HEX_RGBA_TO_FLOAT4(80, 130, 240, 255));
		inline const math::Color Highlight		= math::Color(HEX_RGBA_TO_FLOAT4(255, 230, 90, 255));
		inline const math::Color Unkeyed		= math::Color(HEX_RGBA_TO_FLOAT4(240, 120, 60, 255));
	}

	// Skeletal animation editor, hosted in a closable workspace tab next to the Scene
	// tab (opened by double-clicking an animated .hmesh).
	//
	// Layout: bone hierarchy (left) | 3D preview with skeleton + gizmo (centre) |
	// clip and bone properties (right); transport bar and dopesheet / curve timeline
	// along the bottom.
	//
	// The preview is a private scene (never registered with SceneManager) rendered
	// through OffscreenRenderHooks only while the tab is visible. Its character's
	// SkeletalAnimationComponent runs in editor mode: it shows the clip at exactly the
	// playhead time, and posing writes pose overrides that become keys when keyed.
	class AnimationEditor : public HexEngine::Element
	{
	public:
		// Opens `meshPath` in a new tab, or activates the tab already editing it.
		static bool Open(const fs::path& meshPath);

		AnimationEditor(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size,
			HexEngine::TabItem* tab, std::unique_ptr<AnimationDocument> document);
		~AnimationEditor() override;

		void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		// ---- state shared with the child views ------------------------------------
		struct NodeView
		{
			std::string name;
			int32_t parent = -1;		// index into Nodes()
			int32_t boneParent = -1;	// nearest ancestor that is a skinning bone
			int32_t bone = -1;			// skinning bone index, -1 for plain scene nodes
			int32_t depth = 0;
			bool keyed = false;			// the channel has keys in this clip
			math::Matrix world;			// node -> world (row-vector form)
			math::Matrix parentWorld;	// parent -> world
		};

		AnimationDocument& Doc() { return *_doc; }
		HexEngine::Camera* GetPreviewCamera() const { return _camera; }
		const std::vector<NodeView>& Nodes() const { return _nodes; }
		int32_t FindNode(const std::string& name) const;

		float GetTime() const { return _time; }
		void SetTime(float ticks);				// user scrub: drops unkeyed pose changes
		bool IsPlaying() const { return _playing; }
		void SetPlaying(bool playing);
		void StepFrames(int32_t frames);
		void JumpToKey(bool next);
		float GetClipLength() const;			// ticks

		const std::string& GetSelectedNode() const { return _selectedNode; }
		void SelectNode(const std::string& name);

		std::set<AnimKeyRef>& SelectedKeys() { return _selectedKeys; }

		// Displayed local pose of a node: its pose override if it has one, else the clip.
		bool GetDisplayedPose(const std::string& node, HexEngine::AnimationUtils::NodePose& out) const;
		void SetPoseOverride(const std::string& node, const HexEngine::AnimationUtils::NodePose& pose);
		bool HasPoseOverride(const std::string& node) const;
		void ClearPoseOverride(const std::string& node);
		bool IsAutoKey() const { return _autoKey; }
		bool ShowMesh() const { return _showMesh; }
		bool ShowSkeleton() const { return _showSkeleton; }

		// ---- edits (each one undo step) -------------------------------------------
		void KeyNodes(const std::vector<std::string>& nodes, bool position, bool rotation, bool scale, const std::wstring& label);
		void KeySelectedNode();
		void KeyAllBones();
		void DeleteSelectedKeys();
		void MoveSelectedKeys(float deltaTicks);
		void CopySelectedKeys();
		void PasteKeys();
		void SetKeyValue(const AnimKeyRef& key, const math::Vector4& value, bool liveDrag);	// curve editing
		void ResetPose();				// drop all unkeyed pose changes
		void CopyPose();
		void PastePose();

		void Undo();
		void Redo();
		bool Save();

		void SetStatus(const std::wstring& text, bool isError = false);
		void FrameCharacter();

		// Called by the tab's close button; false keeps the tab open.
		bool RequestClose();

	private:
		void BuildPreviewScene();
		void DestroyPreviewScene();
		void RenderPreview();
		void RebuildNodes();
		void SyncSkeletonToEditor();
		void BuildUi();
		void RefreshClipFields();
		void SyncPropertyFields();
		void ApplyPropertyFields();
		void OpenClipMenu();
		void ShowClosePrompt(bool show);
		// Which tracks of a node had keys before an edit. A keyed channel with an empty
		// track snaps that track to identity at runtime, so after deletes/pastes a track
		// that was emptied (or never existed on a previously unkeyed channel) gets a
		// single bind-pose key. Tracks the importer left empty on purpose stay empty.
		struct TrackState { bool position = false, rotation = false, scale = false; };
		std::map<std::string, TrackState> CaptureTracks(const std::set<std::string>& nodes) const;
		void FixupPartialChannels(const std::map<std::string, TrackState>& before);
		void ExtendClipToKeys();
		bool HandleShortcut(int32_t key);
		std::wstring DescribeTime() const;

		std::unique_ptr<AnimationDocument> _doc;
		HexEngine::TabItem* _tab = nullptr;
		bool _tabVisible = true;
		uint32_t _renderHook = 0;

		// Preview scene
		std::shared_ptr<HexEngine::Scene> _scene;
		HexEngine::Entity* _character = nullptr;
		HexEngine::SkeletalAnimationComponent* _skeleton = nullptr;
		HexEngine::Camera* _camera = nullptr;
		std::vector<NodeView> _nodes;
		uint64_t _nodesRevision = ~0ull;

		// Playback
		float _time = 0.0f;
		bool _playing = false;
		bool _loop = true;
		float _speed = 1.0f;

		// Selection / editing
		std::string _selectedNode;
		std::set<AnimKeyRef> _selectedKeys;
		bool _autoKey = true;
		bool _showMesh = true;		// draw the lit, skinned character in the viewport
		bool _showSkeleton = true;	// draw the bone overlay

		struct ClipboardKey
		{
			std::string node;
			HexEngine::AnimationUtils::Track track;
			float relativeTime;
			math::Vector4 value;	// xyz for position/scale, xyzw quaternion for rotation
		};
		std::vector<ClipboardKey> _keyClipboard;
		std::unordered_map<std::string, HexEngine::AnimationUtils::NodePose> _poseClipboard;

		// Child views
		AnimationViewport* _viewport = nullptr;
		AnimationTimeline* _timeline = nullptr;
		BoneHierarchyView* _bones = nullptr;

		// Right-hand panel
		HexEngine::LineEdit* _clipName = nullptr;
		HexEngine::DragFloat* _clipLength = nullptr;
		HexEngine::DragFloat* _clipFps = nullptr;
		HexEngine::Checkbox* _autoKeyBox = nullptr;
		HexEngine::Checkbox* _loopBox = nullptr;
		HexEngine::Button* _playButton = nullptr;
		HexEngine::Button* _moveButton = nullptr;
		HexEngine::Button* _rotateButton = nullptr;
		HexEngine::Button* _dopeButton = nullptr;
		HexEngine::Button* _curveButton = nullptr;
		HexEngine::ContextMenu* _menu = nullptr;
		float _lengthField = 0.0f;
		float _fpsField = 0.0f;
		float _poseFields[9] = {};	// position xyz, rotation xyz (degrees), scale xyz
		int32_t _syncedClip = -1;
		uint64_t _syncedRevision = ~0ull;

		// Close prompt (unsaved changes)
		HexEngine::Element* _closePrompt = nullptr;

		std::wstring _status;
		bool _statusIsError = false;

		HexEngine::Point _viewportPos, _viewportSize;
		HexEngine::Point _propsPos;
		int32_t _propsWidth = 0;
	};
}
