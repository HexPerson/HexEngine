#include "AnimationRetarget.hpp"
#include "AnimationUtils.hpp"

#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>

namespace HexEngine
{
	namespace
	{
		using AnimationUtils::NodePose;
		using AnimationUtils::ComposeLocal;
		using AnimationUtils::DecomposeLocal;

		// All matrices here use the evaluator's "composed" convention
		// (global = parentGlobal * local; Final = git * global * BoneOffset).
		math::Matrix RotationOnly(const math::Matrix& composed)
		{
			NodePose p = DecomposeLocal(composed);
			p.translation = math::Vector3::Zero;
			p.scale = math::Vector3::One;
			p.rotation.Normalize();
			return ComposeLocal(p);
		}

		// Bind rotation AND scale, translation removed. Bind matrices can carry
		// scale (unit conversion baked into a rig's bones); dropping it made every
		// child's rest offset land at the wrong distance and stretched the skin.
		math::Matrix RotationScaleOnly(const math::Matrix& composed)
		{
			NodePose p = DecomposeLocal(composed);
			p.translation = math::Vector3::Zero;
			p.rotation.Normalize();
			return ComposeLocal(p);
		}

		math::Vector3 PositionOf(const math::Matrix& composed)
		{
			return composed.Transpose().Translation();
		}

		math::Matrix TranslationMatrix(const math::Vector3& t)
		{
			NodePose p;
			p.translation = t;
			return ComposeLocal(p);
		}

		const math::Matrix& ClipGlobalInverse(const Animation& anim, const AnimationData& data)
		{
			return anim._globalInverseTransform != math::Matrix::Identity ? anim._globalInverseTransform : data._globalInverseTransform;
		}

		// Model-space transform of every node of `anim` at `ticks`.
		void EvaluateModelSpace(const Animation& anim, const math::Matrix& git, float ticks, std::unordered_map<std::string, math::Matrix>& out)
		{
			out.clear();
			if (anim._rootNode == nullptr)
				return;
			std::vector<std::pair<const AnimChannel*, math::Matrix>> stack;
			stack.emplace_back(anim._rootNode, math::Matrix::Identity);
			while (!stack.empty())
			{
				auto [channel, parentGlobal] = stack.back();
				stack.pop_back();
				const math::Matrix global = parentGlobal * ComposeLocal(AnimationUtils::SampleChannel(*channel, ticks));
				out[channel->nodeName] = git * global;
				for (const AnimChannel* child : channel->children)
				{
					if (child != nullptr)
						stack.emplace_back(child, global);
				}
			}
		}

		bool BindOf(const AnimatedMesh& mesh, const std::string& bone, math::Matrix& outBind)
		{
			const auto& map = mesh.GetBoneMap();
			auto it = map.find(bone);
			if (it == map.end() || it->second >= (uint32_t)MAX_BONES)
				return false;
			outBind = mesh.GetAllBoneInfo()[it->second].BoneOffset.Invert();
			return true;
		}
	}

	RetargetMap RetargetMap::MixamoToSynty()
	{
		RetargetMap m;
		m.name = "mixamo_to_synty";
		m.targetRoot = "spine";
		m.sourceRoot = "mixamorig:Hips";
		m.sourceLateral = "mixamorig:LeftUpLeg";
		m.targetLateral = "thigh.L";
		auto add = [&](const char* target, const char* parent, const char* source)
		{
			m.bones.push_back({ target, parent, source ? std::string("mixamorig:") + source : std::string() });
		};
		add("spine", "", "Hips");
		add("spine.01", "spine", "Spine");
		add("spine.02", "spine.01", "Spine1");
		add("spine.03", "spine.02", "Spine2");
		add("neck", "spine.03", "Neck");
		add("head", "neck", "Head");
		for (int side = 0; side < 2; ++side)
		{
			const std::string s = side == 0 ? ".L" : ".R";
			const std::string src = side == 0 ? "Left" : "Right";
			auto t = [&](const char* base) { return std::string(base) + s; };
			auto srcName = [&](const char* tail) { return src + tail; };
			const std::string shoulder = t("shoulder"), upper = t("upperarm"), fore = t("forearm"), hand = t("hand");
			m.bones.push_back({ shoulder, "spine.03", "mixamorig:" + srcName("Shoulder") });
			m.bones.push_back({ upper, shoulder, "mixamorig:" + srcName("Arm") });
			m.bones.push_back({ fore, upper, "mixamorig:" + srcName("ForeArm") });
			m.bones.push_back({ hand, fore, "mixamorig:" + srcName("Hand") });
			m.bones.push_back({ t("thumb.01"), hand, "mixamorig:" + srcName("HandThumb1") });
			m.bones.push_back({ t("thumb.02"), t("thumb.01"), "mixamorig:" + srcName("HandThumb2") });
			m.bones.push_back({ t("fingerleft.01"), hand, "mixamorig:" + srcName("HandIndex1") });
			m.bones.push_back({ t("fingerleft.02"), t("fingerleft.01"), "mixamorig:" + srcName("HandIndex2") });
			m.bones.push_back({ t("fingerright.01"), hand, "mixamorig:" + srcName("HandMiddle1") });
			m.bones.push_back({ t("fingerright.02"), t("fingerright.01"), "mixamorig:" + srcName("HandMiddle2") });
			const std::string pelvis = t("pelvis"), thigh = t("thigh"), shin = t("shin"), foot = t("foot");
			m.bones.push_back({ pelvis, "spine", "" });
			m.bones.push_back({ thigh, pelvis, "mixamorig:" + srcName("UpLeg") });
			m.bones.push_back({ shin, thigh, "mixamorig:" + srcName("Leg") });
			m.bones.push_back({ foot, shin, "mixamorig:" + srcName("Foot") });
			m.bones.push_back({ t("toe"), foot, "mixamorig:" + srcName("ToeBase") });
		}
		return m;
	}

	bool RetargetMap::FromPreset(const std::string& preset, RetargetMap& out)
	{
		if (preset == "mixamo_to_synty")
		{
			out = MixamoToSynty();
			return true;
		}
		return false;
	}

	bool RetargetAnimations(const AnimatedMesh& source, AnimatedMesh& target, const RetargetMap& map,
		float sampleRate, const std::vector<uint32_t>& clipIndices, RetargetReport& report, std::string& error)
	{
		report = RetargetReport{};
		const auto srcData = source.GetAnimationData();
		if (srcData == nullptr || srcData->_animations.empty())
		{
			error = "source mesh has no animation clips";
			return false;
		}
		auto tgtData = target.GetAnimationData();
		if (tgtData == nullptr)
		{
			tgtData = target.CreateAnimationData();
			target.SetAnimationData(tgtData);
		}
		if (tgtData == nullptr)
		{
			error = "target mesh cannot hold animation data";
			return false;
		}
		sampleRate = std::clamp(sampleRate, 1.0f, 240.0f);

		// --- Target hierarchy, bind poses and rest offsets -------------------------
		std::unordered_map<std::string, math::Matrix> tgtBind;
		std::unordered_set<std::string> mapped;
		for (const auto& b : map.bones)
		{
			math::Matrix bind;
			if (!BindOf(target, b.target, bind))
			{
				report.missingTargetBones.push_back(b.target);
				continue;
			}
			tgtBind[b.target] = bind;
			mapped.insert(b.target);
		}
		if (tgtBind.find(map.targetRoot) == tgtBind.end())
		{
			error = "target mesh has no '" + map.targetRoot + "' bone - is it skinned with the expected rig?";
			return false;
		}
		// Skin bones the map doesn't know: keep them rigid under the root.
		std::vector<RetargetBone> bones;
		for (const auto& b : map.bones)
			if (tgtBind.count(b.target))
				bones.push_back(b);
		for (const auto& [name, index] : target.GetBoneMap())
		{
			if (mapped.count(name) || index >= (uint32_t)MAX_BONES)
				continue;
			report.unmappedTargetBones.push_back(name);
			bones.push_back({ name, map.targetRoot, "" });
			tgtBind[name] = target.GetAllBoneInfo()[index].BoneOffset.Invert();
		}
		// Order parents before children (the map lists them that way; extras hang off the root).
		std::unordered_map<std::string, math::Matrix> restLocal;
		for (const auto& b : bones)
		{
			const auto parentIt = b.targetParent.empty() ? tgtBind.end() : tgtBind.find(b.targetParent);
			restLocal[b.target] = parentIt != tgtBind.end() ? parentIt->second.Invert() * tgtBind[b.target] : tgtBind[b.target];
		}

		// --- Source binds --------------------------------------------------------
		std::unordered_map<std::string, math::Matrix> srcBindRot;
		for (const auto& b : bones)
		{
			if (b.source.empty())
				continue;
			math::Matrix bind;
			if (!BindOf(source, b.source, bind))
			{
				report.missingSourceBones.push_back(b.source);
				continue;
			}
			srcBindRot[b.source] = RotationOnly(bind);
			++report.mappedBones;
		}
		math::Matrix srcRootBind;
		const bool haveSrcRoot = BindOf(source, map.sourceRoot, srcRootBind);
		const math::Vector3 srcRootBindPos = haveSrcRoot ? PositionOf(srcRootBind) : math::Vector3::Zero;
		const math::Vector3 tgtRootBindPos = PositionOf(tgtBind[map.targetRoot]);
		// Root translation scale: ratio of root heights (falls back to 1).
		float rootScale = 1.0f;
		if (haveSrcRoot && std::abs(srcRootBindPos.y) > 1e-4f && std::abs(tgtRootBindPos.y) > 1e-4f)
			rootScale = tgtRootBindPos.y / srcRootBindPos.y;

		const math::Matrix tgtGitInv = tgtData->_globalInverseTransform.Invert();
		// Bind-pose landmarks (root + lateral bone of each rig), reported so a caller
		// can sanity-check that both rigs share a model space.
		auto addLandmark = [&](const char* side, const AnimatedMesh& mesh, const std::string& bone)
		{
			math::Matrix m;
			if (!bone.empty() && BindOf(mesh, bone, m))
				report.landmarks.emplace_back(side + bone, PositionOf(m));
		};
		addLandmark("src ", source, map.sourceRoot);
		addLandmark("src ", source, map.sourceLateral);
		addLandmark("tgt ", target, map.targetRoot);
		addLandmark("tgt ", target, map.targetLateral);

		// Yaw between the two rigs' model spaces: align source root->left-lateral
		// with target root->left-lateral in the XZ plane. W maps source-space
		// rotations/vectors into target space (composed convention, v' = W v).
		math::Matrix spaceW = math::Matrix::Identity;
		report.spaceYawDegrees = 0.0f;
		{
			math::Matrix srcLat, tgtLat;
			const auto tgtRootIt = tgtBind.find(map.targetRoot);
			if (haveSrcRoot && !map.sourceLateral.empty() && !map.targetLateral.empty() &&
				BindOf(source, map.sourceLateral, srcLat) && BindOf(target, map.targetLateral, tgtLat) && tgtRootIt != tgtBind.end())
			{
				const math::Vector3 a = PositionOf(srcLat) - srcRootBindPos;
				const math::Vector3 b = PositionOf(tgtLat) - PositionOf(tgtRootIt->second);
				const float lenA = std::sqrt(a.x * a.x + a.z * a.z), lenB = std::sqrt(b.x * b.x + b.z * b.z);
				if (lenA > 1e-5f && lenB > 1e-5f)
				{
					// Try both rotation senses and keep the one that maps a onto b
					// (avoids depending on the handedness of the composed convention).
					const float angle = std::atan2(b.z, b.x) - std::atan2(a.z, a.x);
					float best = -2.0f;
					for (float yaw : { angle, -angle })
					{
						NodePose p;
						p.rotation = math::Quaternion::CreateFromAxisAngle(math::Vector3::UnitY, yaw);
						const math::Matrix w = ComposeLocal(p);
						const math::Vector3 mapped = math::Vector3::Transform(math::Vector3(a.x, 0.0f, a.z), w.Transpose());
						const float score = (mapped.x * b.x + mapped.z * b.z) / (lenA * lenB);
						if (score > best)
						{
							best = score;
							spaceW = w;
							report.spaceYawDegrees = yaw * 57.2957795f;
						}
					}
				}
			}
		}
		// Rigs authored in the same convention give a few degrees of "yaw" from
		// stance noise (thigh offsets); only a real facing difference matters.
		if (std::abs(report.spaceYawDegrees) < 45.0f)
		{
			spaceW = math::Matrix::Identity;
			report.spaceYawDegrees = 0.0f;
		}
		const math::Matrix spaceWInv = spaceW.Invert();

		// --- Clips ---------------------------------------------------------------
		std::vector<uint32_t> clips = clipIndices;
		if (clips.empty())
			for (uint32_t i = 0; i < (uint32_t)srcData->_animations.size(); ++i)
				clips.push_back(i);

		std::vector<Animation> outClips;
		outClips.reserve(clips.size());
		std::unordered_map<std::string, math::Matrix> srcModel;
		std::unordered_map<std::string, math::Matrix> tgtModel;

		for (uint32_t clipIndex : clips)
		{
			if (clipIndex >= srcData->_animations.size())
				continue;
			const Animation& srcAnim = srcData->_animations[clipIndex];
			const math::Matrix& srcGit = ClipGlobalInverse(srcAnim, *srcData);
			const float tps = AnimationUtils::GetTicksPerSecond(srcAnim);
			const float duration = std::max(0.0f, srcAnim.duration);
			const float step = std::max(1e-4f, tps / sampleRate);
			const uint32_t samples = std::max<uint32_t>(2, (uint32_t)std::ceil(duration / step) + 1);
			report.samplesPerClipMax = std::max(report.samplesPerClipMax, samples);

			Animation out;
			out.name = srcAnim.name;
			out.ticksPerSecond = srcAnim.ticksPerSecond;
			out.duration = srcAnim.duration;
			out.events = srcAnim.events;   // footstep etc. events carry over (same timeline)
			out._globalInverseTransform = math::Matrix::Identity;	// evaluator falls back to the target's
			out.channels.reserve(bones.size());
			std::unordered_map<std::string, size_t> channelIndex;
			for (const auto& b : bones)
			{
				AnimChannel ch;
				ch.nodeName = b.target;
				ch.nodeTransform = b.target == map.targetRoot ? tgtGitInv * tgtBind[b.target] : restLocal[b.target];
				channelIndex[b.target] = out.channels.size();
				out.channels.push_back(std::move(ch));
			}

			std::unordered_map<std::string, math::Quaternion> lastRotation;
			for (uint32_t s = 0; s < samples; ++s)
			{
				const float ticks = std::min(duration, s * step);
				EvaluateModelSpace(srcAnim, srcGit, ticks, srcModel);
				tgtModel.clear();

				for (const auto& b : bones)
				{
					const bool isRoot = b.target == map.targetRoot;
					const math::Matrix parentModel = isRoot ? math::Matrix::Identity : tgtModel[b.targetParent];
					const math::Matrix restChain = isRoot ? tgtBind[b.target] : parentModel * restLocal[b.target];

					math::Matrix model = restChain;
					auto srcIt = b.source.empty() ? srcModel.end() : srcModel.find(b.source);
					auto bindIt = b.source.empty() ? srcBindRot.end() : srcBindRot.find(b.source);
					if (srcIt != srcModel.end() && bindIt != srcBindRot.end())
					{
						// Source-space delta, conjugated into the target's model space.
						const math::Matrix delta = spaceW * (RotationOnly(srcIt->second) * bindIt->second.Invert()) * spaceWInv;
						math::Vector3 position = PositionOf(restChain);
						if (isRoot && haveSrcRoot)
						{
							const math::Vector3 srcOffset = (PositionOf(srcIt->second) - srcRootBindPos) * rootScale;
							position = tgtRootBindPos + math::Vector3::Transform(srcOffset, spaceW.Transpose());
						}
						model = TranslationMatrix(position) * delta * RotationScaleOnly(tgtBind[b.target]);
					}
					tgtModel[b.target] = model;

					const math::Matrix local = isRoot ? tgtGitInv * model : parentModel.Invert() * model;
					NodePose pose = DecomposeLocal(local);
					pose.rotation.Normalize();
					// Keep consecutive quaternions in the same hemisphere so key
					// interpolation never takes the long way round.
					if (auto it = lastRotation.find(b.target); it != lastRotation.end() && it->second.Dot(pose.rotation) < 0.0f)
						pose.rotation = -pose.rotation;
					lastRotation[b.target] = pose.rotation;

					AnimChannel& ch = out.channels[channelIndex[b.target]];
					ch.positionKeys.emplace_back(ticks, pose.translation);
					ch.rotationKeys.emplace_back(ticks, pose.rotation);
					ch.scaleKeys.emplace_back(ticks, math::Vector3::One);
				}
			}

			// Link the channel tree (addresses are stable: channels is fully built).
			for (const auto& b : bones)
			{
				AnimChannel* ch = &out.channels[channelIndex[b.target]];
				if (b.target == map.targetRoot)
					out._rootNode = ch;
				else if (auto parent = channelIndex.find(b.targetParent); parent != channelIndex.end())
					out.channels[parent->second].children.push_back(ch);
			}
			outClips.push_back(std::move(out));
		}

		if (outClips.empty())
		{
			error = "no valid clip indices";
			return false;
		}

		// Moving the vector keeps element addresses, so the AnimChannel* links stay valid.
		tgtData->_animations = std::move(outClips);
		++tgtData->_revision;
		report.clips = (uint32_t)tgtData->_animations.size();
		return true;
	}
}
