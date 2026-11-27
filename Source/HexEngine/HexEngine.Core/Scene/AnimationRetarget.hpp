#pragma once

#include "AnimatedMesh.hpp"

namespace HexEngine
{
	// Bakes animation clips from one skinned mesh's skeleton onto another's.
	//
	// Method (world/model-space delta, per mapped bone, per sample):
	//   srcModel  = clip-evaluated model-space transform of the source bone
	//   srcBind   = inverse(source BoneOffset)   - the bone's bind pose in model space
	//   delta     = Rot(srcModel) * inverse(Rot(srcBind))
	//   tgtModel  = Translate(chainPos) * delta * Rot(tgtBind)
	// so each target bone turns away from ITS OWN bind pose exactly as much as the
	// source bone turned away from its bind pose. Positions come from the target's
	// own rest chain (bone lengths are the target's); only the root also carries the
	// source root's translation delta, scaled by the ratio of the two root heights.
	// Unmapped target bones keep their rest offset under their (posed) parent.
	//
	// The result REPLACES the target's AnimationData clips (same clip order as the
	// source, so clip indices - idle, walk - stay interchangeable between rigs).
	struct RetargetBone
	{
		std::string target;        // target skeleton bone / node name
		std::string targetParent;  // its parent in the target hierarchy ("" = root)
		std::string source;        // source bone it follows ("" = keep rest pose)
	};

	struct RetargetMap
	{
		std::string name;
		std::string targetRoot;    // root bone of the target hierarchy (carries translation)
		std::string sourceRoot;    // source bone whose translation drives targetRoot
		// A bone on the character's LEFT side in each rig (e.g. the left thigh). The
		// two rigs' model spaces can face different ways (Mixamo vs Synty differ by
		// 180 degrees about +Y); the yaw between root->lateral in each bind pose is
		// solved and every source delta is conjugated into the target's space.
		std::string sourceLateral;
		std::string targetLateral;
		std::vector<RetargetBone> bones;

		// Mixamo rig ("mixamorig:*", 65 bones) -> Synty POLYGON 36-bone rig
		// (spine, spine.01.., upperarm.L, thigh.R, ...).
		HEX_API static RetargetMap MixamoToSynty();
		HEX_API static bool FromPreset(const std::string& preset, RetargetMap& out);
	};

	struct RetargetReport
	{
		uint32_t clips = 0;
		uint32_t mappedBones = 0;          // map entries whose source AND target bones exist
		uint32_t samplesPerClipMax = 0;
		float spaceYawDegrees = 0.0f;                   // solved rotation between the rigs' model spaces
		std::vector<std::string> missingSourceBones;
		std::vector<std::string> missingTargetBones;
		std::vector<std::string> unmappedTargetBones;   // target skin bones not in the map
		// Bind-pose model-space positions of each rig's root + lateral bone (name -> xyz).
		std::vector<std::pair<std::string, math::Vector3>> landmarks;
	};

	// clipIndices empty = every source clip. sampleRate in samples per second.
	HEX_API bool RetargetAnimations(const AnimatedMesh& source, AnimatedMesh& target, const RetargetMap& map,
		float sampleRate, const std::vector<uint32_t>& clipIndices, RetargetReport& report, std::string& error);
}
