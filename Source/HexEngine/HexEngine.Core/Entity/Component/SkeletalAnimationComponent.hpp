
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/AnimatedMesh.hpp"
#include "../../Scene/AnimationUtils.hpp"
#include <bitset>

namespace HexEngine
{
	class HEX_API SkeletalAnimationComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(SkeletalAnimationComponent);
		DEFINE_COMPONENT_CTOR(SkeletalAnimationComponent);
		virtual ~SkeletalAnimationComponent();

		virtual void Update(float deltaTime) override;

		void		SetAnimationData(std::shared_ptr<AnimatedMesh> mesh, std::shared_ptr<AnimationData> animData);
		BoneInfo*	GetBoneInfoByName(const std::string& name);

		// Pull the AnimatedMesh + AnimationData off the owning entity's
		// StaticMeshComponent if we don't already have them. Editor-driven
		// flows (mesh dragged in, asset-explorer assignment) call
		// SetAnimationData directly, but prefab deserialization does NOT -
		// it just constructs the component and runs Deserialize. Without
		// this auto-bind, prefab-instantiated characters render the right
		// mesh (because the StaticMeshComponent still loaded it) but the
		// SkeletalAnimationComponent stays detached: animations don't
		// populate in the editor dropdown and the rig doesn't animate at
		// runtime. Returns true if the bind succeeded or was already in
		// effect.
		bool		TryAutoBindFromEntityMesh();

		// Accessor for the underlying animation set; used by the editor widget
		// (and any gameplay code) to enumerate available animation names.
		std::shared_ptr<AnimationData> GetAnimationData() const { return _animData; }

		// One node of a clip's channel tree, flattened parent-before-child so a
		// pose is a single forward loop: no recursion and no per-frame bone-name
		// lookups (the bone index is resolved once, when the clip is compiled).
		struct PoseNode
		{
			const AnimChannel*	channel = nullptr;
			int32_t				parent = -1;	// index into CompiledClip::nodes, -1 = root
			int32_t				bone = -1;		// index into _boneInfo, -1 = not a skinning bone
			bool				hasKeys = false;
		};

		// ---- Animation editor hooks ----------------------------------------------
		// Editor mode: the clip is not advanced by wall time. The pose is evaluated at
		// exactly SetEditorTime() (no crossfade, no root-motion stripping, no random
		// start offset) with any pose overrides layered on top - that is how the
		// animation editor scrubs the timeline and poses bones before keying them.
		void				SetEditorMode(bool enabled);
		bool				IsEditorMode() const { return _editorMode; }
		void				SetEditorTime(float ticks) { _editorTicks = ticks; }	// clip time in key units (ticks)
		float				GetEditorTime() const { return _editorTicks; }

		// Overrides replace a node's sampled local pose (editor mode only). Keyed by
		// node name so they survive clip edits and undo, which rebuild the channels.
		void				SetPoseOverride(const std::string& nodeName, const AnimationUtils::NodePose& pose);
		void				ClearPoseOverride(const std::string& nodeName);
		void				ClearPoseOverrides();
		const AnimationUtils::NodePose* GetPoseOverride(const std::string& nodeName) const;
		const std::unordered_map<std::string, AnimationUtils::NodePose>& GetPoseOverrides() const { return _poseOverrides; }

		// Evaluates the editor pose now instead of waiting for the next Update().
		void				EvaluateNow();

		// The last editor-mode evaluation, for drawing and picking the skeleton: the
		// clip's nodes (parent before child), each node's global matrix, and the clip
		// root transform. A node's mesh-space matrix is clipRoot * global (composed
		// convention). Empty until EvaluateNow()/Update() ran in editor mode.
		const std::vector<PoseNode>&		GetEvaluatedNodes() const { return _evaluatedNodes; }
		const std::vector<math::Matrix>&	GetEvaluatedNodeGlobals() const { return _nodeGlobals; }
		const math::Matrix&					GetEvaluatedClipRoot() const { return _evaluatedClipRoot; }

	private:
		// Last key index used per channel. Playback moves forward a little each
		// frame, so starting the key search from here makes it O(1) instead of a
		// scan from key 0.
		struct KeyCursor
		{
			uint32_t pos = 0;
			uint32_t rot = 0;
			uint32_t scl = 0;
		};

		struct CompiledClip
		{
			const Animation*		anim = nullptr;	// what this was compiled from (rebuilt if it changes)
			std::vector<PoseNode>	nodes;
			std::vector<KeyCursor>	cursors;
			int32_t					rootMotionNode = -2;	// -2 = not resolved yet, -1 = none
		};

		// Evaluates clip `clipIndex` at clip time `ticks` (already wrapped into the clip).
		void				EvaluateClip(uint32_t clipIndex, float ticks, std::array<math::Matrix, MAX_BONES>& transforms, bool editorPose);
		void				SnapshotPrevPose();
		CompiledClip&		GetCompiledClip(uint32_t clipIndex);
		void				CompileClip(CompiledClip& clip, const Animation* anim);
		int32_t				ResolveRootMotionNode(CompiledClip& clip);



	public:
		void				StopAnimating();
		void				SetAnimationIndex(uint32_t idx);
		void				BlendToAnimationIndex(uint32_t idx);
		uint32_t			GetAnimationIndex() const { return _animIndex; }

		// Root motion: when enabled, ReadNodeHierarchy strips the root bone's
		// translation from the skeleton each frame and accumulates the per-
		// frame translation delta in _rootMotionDelta (entity-local space).
		// Game code applies the delta to the owning Entity's transform via
		// ConsumeRootMotionDelta() so the mesh stays planted on the move
		// instead of sliding around relative to the entity origin.
		bool				IsRootMotionEnabled() const { return _rootMotion; }
		void				SetRootMotionEnabled(bool enabled) { _rootMotion = enabled; }

		// Returns the accumulated entity-local translation since the last
		// call, then clears the accumulator. Game/script code should call
		// this once per frame and add the value to the entity's Transform.
		math::Vector3		ConsumeRootMotionDelta();

		const std::array<math::Matrix, MAX_BONES>& GetBoneTransformArray() const;

		// Previous frame's pose, for skinned motion vectors. Snapshotted once per rendered
		// frame in Update(); equals the current pose before the first advance, and whenever
		// the animation didn't move, which correctly yields zero deformation velocity.
		const std::array<math::Matrix, MAX_BONES>& GetBoneTransformArrayPrev() const;

		// anim_stats telemetry: AnimatedMesh reports its per-draw bone uploads here so
		// pose cost and upload cost are logged side by side.
		static bool			IsTelemetryEnabled();
		static void			RecordBoneUpload(float milliseconds);

		// GPU skinning for `mesh`'s next draw: skins it in a compute pass if the pose
		// changed since the last dispatch, and returns the output to draw from. Null
		// means "skin in the vertex shader instead" (unsupported backend, r_gpuSkinning 0,
		// no pose evaluated yet, or a mesh this component isn't driving).
		GpuSkinInstance*	PrepareGpuSkin(AnimatedMesh* mesh);

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

	private:
		std::shared_ptr<AnimatedMesh> _mesh;
		std::shared_ptr<AnimationData> _animData;
		uint32_t _animIndex = 0;
		uint32_t _nextAnimIndex = -1;
		float _blendFactor = 0.0f;
		float _animationStartTime = 0.0f;

		std::array<BoneInfo, MAX_BONES> _boneInfo;
		// Bones handed out through GetBoneInfoByName (attachment points). Only these
		// get BoneInfo::Position / Rotation refreshed each pose - nothing else reads them.
		std::bitset<MAX_BONES> _attachmentBones;
		std::vector<CompiledClip> _compiledClips;	// indexed like _animData->_animations
		uint64_t _compiledRevision = 0;				// _animData->_revision they were built at
		std::vector<math::Matrix> _nodeGlobals;		// per-node scratch for the current pose
		std::array<math::Matrix, MAX_BONES> _transforms;
		bool _editorMode = false;
		float _editorTicks = 0.0f;
		std::unordered_map<std::string, AnimationUtils::NodePose> _poseOverrides;
		std::vector<PoseNode> _evaluatedNodes;		// copy of the evaluated clip's nodes (editor mode)
		math::Matrix _evaluatedClipRoot;

		uint64_t _poseVersion = 0;					// bumped whenever _transforms is re-evaluated
		std::unique_ptr<GpuSkinInstance> _gpuSkin;	// compute-skinned vertices for this entity
		std::array<math::Matrix, MAX_BONES> _transformsPrev;
		// Frame the prev-pose snapshot was last taken on. Update() can be entered more than
		// once per rendered frame; snapshotting unconditionally would then leave prev ==
		// current and silently zero the deformation velocity again.
		uint32_t _prevPoseFrame = 0;
		bool _prevPoseValid = false;

		int32_t _previousTickRate = 1;

		// Root motion state. _rootMotion is the user-facing toggle, the rest
		// is per-frame bookkeeping. _rootBoneName caches the first bone whose
		// node hierarchy we visit so we only strip translation from the
		// genuine skeletal root (not arbitrary scene-graph parents).
		// _rootBoneBindPos is the FIRST sample of the root bone's animated
		// translation, treated as the bind-pose anchor; we re-stamp the
		// bone's local translation back to this every frame so the
		// skeleton's hips stay at hip-height while the per-frame delta is
		// handed off to the owning Entity via ConsumeRootMotionDelta().
		bool			_rootMotion = false;
		math::Vector3	_lastRootBonePosition = math::Vector3::Zero;
		math::Vector3	_rootBoneBindPos = math::Vector3::Zero;
		bool			_hasLastRootBonePosition = false;
		math::Vector3	_rootMotionDelta = math::Vector3::Zero;
		std::string		_rootBoneName;
	};
}
