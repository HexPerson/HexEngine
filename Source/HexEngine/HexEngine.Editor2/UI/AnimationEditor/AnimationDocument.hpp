#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <chrono>

namespace HexEditor
{
	// One key on one track of one node (channels are addressed by name, which is
	// stable across undo and clip copies; channel pointers are not).
	struct AnimKeyRef
	{
		std::string node;
		HexEngine::AnimationUtils::Track track = HexEngine::AnimationUtils::Track::Position;
		float time = 0.0f;

		bool operator<(const AnimKeyRef& other) const
		{
			if (node != other.node) return node < other.node;
			if (track != other.track) return track < other.track;
			return time < other.time;
		}
		bool operator==(const AnimKeyRef& other) const
		{
			return node == other.node && track == other.track && std::abs(time - other.time) <= HexEngine::AnimationUtils::kKeyTimeEpsilon;
		}
	};

	// The animation set of one animated .hmesh, being edited.
	//
	// Edits go straight into the mesh's shared AnimationData (so every instance in
	// the level updates live) and are recorded for undo as before/after copies of
	// the touched clip. Every change bumps AnimationData::_revision so evaluators
	// rebuild their cached layouts. Discard restores the state from the last save.
	class AnimationDocument
	{
	public:
		bool Open(const fs::path& meshPath, std::wstring& error);

		const fs::path& GetPath() const { return _path; }
		const std::shared_ptr<HexEngine::AnimatedMesh>& GetMesh() const { return _mesh; }
		HexEngine::AnimationData* GetData() const { return _data.get(); }

		uint32_t GetClipCount() const;
		int32_t GetClipIndex() const { return _clipIndex; }
		void SetClipIndex(int32_t index);
		HexEngine::Animation* GetClip() const;

		// ---- edits on the current clip --------------------------------------------
		// Begin snapshots the clip; changes made until End are one undo step. Touch()
		// after each in-between change (e.g. while dragging) so the preview updates.
		// `coalesce`: merge into the previous undo step if it has the same label and was
		// made moments ago (continuous field drags would otherwise flood the history).
		void BeginEdit(const std::wstring& label, bool coalesce = false);
		void EndEdit();
		void CancelEdit();
		bool IsEditing() const { return _editBefore != nullptr; }
		void Touch();

		// ---- clip list -----------------------------------------------------------
		void NewClip(const std::string& name);		// bind pose, same channel tree
		void DuplicateClip();
		bool DeleteClip();							// refuses to delete the last clip
		void RenameClip(const std::string& name);
		void SetClipTiming(float durationTicks, float ticksPerSecond);

		// ---- history / persistence ------------------------------------------------
		bool CanUndo() const { return !_undo.empty(); }
		bool CanRedo() const { return !_redo.empty(); }
		bool Undo();
		bool Redo();
		const std::wstring& GetUndoLabel() const;

		bool IsDirty() const { return _dirty; }
		bool Save(std::wstring& error);
		void DiscardChanges();	// back to the last saved state

	private:
		struct UndoEntry
		{
			enum class Kind { Modify, Insert, Remove };
			Kind kind = Kind::Modify;
			int32_t index = 0;
			std::shared_ptr<HexEngine::Animation> before;	// Modify, Remove
			std::shared_ptr<HexEngine::Animation> after;	// Modify, Insert
			std::wstring label;
		};

		void Push(UndoEntry entry);
		void Apply(const UndoEntry& entry, bool undo);
		void InsertClip(int32_t index, const HexEngine::Animation& clip);
		void RemoveClip(int32_t index);
		void Changed();

		static constexpr size_t kMaxUndo = 100;

		fs::path _path;
		std::shared_ptr<HexEngine::AnimatedMesh> _mesh;
		std::shared_ptr<HexEngine::AnimationData> _data;
		int32_t _clipIndex = 0;

		std::shared_ptr<HexEngine::Animation> _editBefore;
		std::wstring _editLabel;
		bool _editCoalesce = false;
		std::chrono::steady_clock::time_point _lastPush;

		std::vector<UndoEntry> _undo;
		std::vector<UndoEntry> _redo;
		std::vector<HexEngine::Animation> _saved;	// clips as of the last save, for Discard
		bool _dirty = false;
	};
}
