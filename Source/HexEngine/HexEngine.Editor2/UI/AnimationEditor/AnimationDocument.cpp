#include "AnimationDocument.hpp"

namespace HexEditor
{
	using namespace HexEngine;

	namespace
	{
		std::shared_ptr<Animation> CloneShared(const Animation& clip)
		{
			return std::make_shared<Animation>(AnimationUtils::CloneAnimation(clip));
		}

		// Byte-level key comparison is enough to skip no-op undo entries (e.g. a click
		// on a gizmo that never moved).
		bool SameKeys(const Animation& a, const Animation& b)
		{
			if (a.name != b.name || a.duration != b.duration || a.ticksPerSecond != b.ticksPerSecond || a.channels.size() != b.channels.size())
				return false;

			for (size_t i = 0; i < a.channels.size(); ++i)
			{
				const auto& ca = a.channels[i];
				const auto& cb = b.channels[i];
				if (ca.positionKeys.size() != cb.positionKeys.size() || ca.rotationKeys.size() != cb.rotationKeys.size() || ca.scaleKeys.size() != cb.scaleKeys.size())
					return false;
				if (!ca.positionKeys.empty() && memcmp(ca.positionKeys.data(), cb.positionKeys.data(), ca.positionKeys.size() * sizeof(ca.positionKeys[0])) != 0)
					return false;
				if (!ca.rotationKeys.empty() && memcmp(ca.rotationKeys.data(), cb.rotationKeys.data(), ca.rotationKeys.size() * sizeof(ca.rotationKeys[0])) != 0)
					return false;
				if (!ca.scaleKeys.empty() && memcmp(ca.scaleKeys.data(), cb.scaleKeys.data(), ca.scaleKeys.size() * sizeof(ca.scaleKeys[0])) != 0)
					return false;
			}
			return true;
		}
	}

	bool AnimationDocument::Open(const fs::path& meshPath, std::wstring& error)
	{
		auto mesh = std::dynamic_pointer_cast<AnimatedMesh>(Mesh::Create(meshPath));
		if (mesh == nullptr)
		{
			error = L"Not an animated mesh: " + meshPath.filename().wstring();
			return false;
		}

		auto data = mesh->GetAnimationData();
		if (data == nullptr || data->_animations.empty())
		{
			error = L"The mesh has no animation clips: " + meshPath.filename().wstring();
			return false;
		}

		_path = meshPath;
		_mesh = mesh;
		_data = data;
		_clipIndex = 0;
		_undo.clear();
		_redo.clear();
		_dirty = false;

		_saved.clear();
		for (const auto& clip : _data->_animations)
			_saved.push_back(AnimationUtils::CloneAnimation(clip));

		return true;
	}

	uint32_t AnimationDocument::GetClipCount() const
	{
		return _data ? (uint32_t)_data->_animations.size() : 0u;
	}

	void AnimationDocument::SetClipIndex(int32_t index)
	{
		if (_data == nullptr || _data->_animations.empty())
			return;
		_clipIndex = std::clamp(index, 0, (int32_t)_data->_animations.size() - 1);
	}

	Animation* AnimationDocument::GetClip() const
	{
		if (_data == nullptr || _data->_animations.empty())
			return nullptr;
		return &_data->_animations[std::clamp(_clipIndex, 0, (int32_t)_data->_animations.size() - 1)];
	}

	void AnimationDocument::Changed()
	{
		++_data->_revision;
		_dirty = true;
	}

	void AnimationDocument::Touch()
	{
		if (_data)
			Changed();
	}

	void AnimationDocument::BeginEdit(const std::wstring& label, bool coalesce)
	{
		if (IsEditing())
			EndEdit();

		if (auto* clip = GetClip(); clip != nullptr)
		{
			_editBefore = CloneShared(*clip);
			_editLabel = label;
			_editCoalesce = coalesce;
		}
	}

	void AnimationDocument::EndEdit()
	{
		if (!IsEditing())
			return;

		auto before = std::move(_editBefore);
		_editBefore = nullptr;

		Animation* clip = GetClip();
		if (clip == nullptr || SameKeys(*before, *clip))
			return;

		UndoEntry entry;
		entry.kind = UndoEntry::Kind::Modify;
		entry.index = _clipIndex;
		entry.before = before;
		entry.after = CloneShared(*clip);
		entry.label = _editLabel;

		const auto now = std::chrono::steady_clock::now();
		if (_editCoalesce && !_undo.empty() && _redo.empty())
		{
			UndoEntry& last = _undo.back();
			if (last.kind == UndoEntry::Kind::Modify && last.index == entry.index && last.label == entry.label &&
				now - _lastPush < std::chrono::milliseconds(1500))
			{
				last.after = entry.after;	// keep the original 'before'
				_lastPush = now;
				Changed();
				return;
			}
		}

		Push(std::move(entry));
		_lastPush = now;
		Changed();
	}

	void AnimationDocument::CancelEdit()
	{
		if (!IsEditing())
			return;

		if (Animation* clip = GetClip(); clip != nullptr)
		{
			*clip = AnimationUtils::CloneAnimation(*_editBefore);
			Changed();
		}
		_editBefore = nullptr;
	}

	void AnimationDocument::Push(UndoEntry entry)
	{
		_undo.push_back(std::move(entry));
		if (_undo.size() > kMaxUndo)
			_undo.erase(_undo.begin());
		_redo.clear();
	}

	void AnimationDocument::InsertClip(int32_t index, const Animation& clip)
	{
		index = std::clamp(index, 0, (int32_t)_data->_animations.size());
		// Moving Animations around in the vector is safe: their channel vectors move
		// with them, so the tree pointers stay valid.
		_data->_animations.insert(_data->_animations.begin() + index, AnimationUtils::CloneAnimation(clip));
		_clipIndex = index;
		Changed();
	}

	void AnimationDocument::RemoveClip(int32_t index)
	{
		if (index < 0 || index >= (int32_t)_data->_animations.size())
			return;
		_data->_animations.erase(_data->_animations.begin() + index);
		_clipIndex = std::clamp(index - 1, 0, std::max(0, (int32_t)_data->_animations.size() - 1));
		Changed();
	}

	void AnimationDocument::NewClip(const std::string& name)
	{
		Animation* current = GetClip();
		if (current == nullptr)
			return;

		Animation clip = AnimationUtils::CloneAnimationStructure(*current);
		clip.name = name;

		UndoEntry entry;
		entry.kind = UndoEntry::Kind::Insert;
		entry.index = (int32_t)_data->_animations.size();
		entry.after = CloneShared(clip);
		entry.label = L"New clip";
		InsertClip(entry.index, clip);
		Push(std::move(entry));
	}

	void AnimationDocument::DuplicateClip()
	{
		Animation* current = GetClip();
		if (current == nullptr)
			return;

		Animation clip = AnimationUtils::CloneAnimation(*current);
		clip.name += " Copy";

		UndoEntry entry;
		entry.kind = UndoEntry::Kind::Insert;
		entry.index = _clipIndex + 1;
		entry.after = CloneShared(clip);
		entry.label = L"Duplicate clip";
		InsertClip(entry.index, clip);
		Push(std::move(entry));
	}

	bool AnimationDocument::DeleteClip()
	{
		if (GetClipCount() <= 1)
			return false;

		UndoEntry entry;
		entry.kind = UndoEntry::Kind::Remove;
		entry.index = _clipIndex;
		entry.before = CloneShared(*GetClip());
		entry.label = L"Delete clip";
		RemoveClip(entry.index);
		Push(std::move(entry));
		return true;
	}

	void AnimationDocument::RenameClip(const std::string& name)
	{
		Animation* clip = GetClip();
		if (clip == nullptr || clip->name == name)
			return;

		BeginEdit(L"Rename clip");
		clip->name = name;
		EndEdit();
	}

	void AnimationDocument::SetClipTiming(float durationTicks, float ticksPerSecond)
	{
		Animation* clip = GetClip();
		if (clip == nullptr)
			return;

		durationTicks = std::max(1.0f, durationTicks);
		ticksPerSecond = std::max(1.0f, ticksPerSecond);
		if (clip->duration == durationTicks && clip->ticksPerSecond == ticksPerSecond)
			return;

		BeginEdit(L"Clip timing", true);
		clip->duration = durationTicks;
		clip->ticksPerSecond = ticksPerSecond;
		EndEdit();
	}

	void AnimationDocument::Apply(const UndoEntry& entry, bool undo)
	{
		switch (entry.kind)
		{
		case UndoEntry::Kind::Modify:
			if (entry.index >= 0 && entry.index < (int32_t)_data->_animations.size())
			{
				_data->_animations[entry.index] = AnimationUtils::CloneAnimation(undo ? *entry.before : *entry.after);
				_clipIndex = entry.index;
				Changed();
			}
			break;

		case UndoEntry::Kind::Insert:
			if (undo) RemoveClip(entry.index);
			else InsertClip(entry.index, *entry.after);
			break;

		case UndoEntry::Kind::Remove:
			if (undo) InsertClip(entry.index, *entry.before);
			else RemoveClip(entry.index);
			break;
		}
	}

	bool AnimationDocument::Undo()
	{
		if (IsEditing())
			CancelEdit();
		if (_undo.empty())
			return false;

		UndoEntry entry = std::move(_undo.back());
		_undo.pop_back();
		Apply(entry, true);
		_redo.push_back(std::move(entry));
		return true;
	}

	bool AnimationDocument::Redo()
	{
		if (_redo.empty())
			return false;

		UndoEntry entry = std::move(_redo.back());
		_redo.pop_back();
		Apply(entry, false);
		_undo.push_back(std::move(entry));
		return true;
	}

	const std::wstring& AnimationDocument::GetUndoLabel() const
	{
		static const std::wstring empty;
		return _undo.empty() ? empty : _undo.back().label;
	}

	bool AnimationDocument::Save(std::wstring& error)
	{
		if (_mesh == nullptr)
		{
			error = L"Nothing to save";
			return false;
		}

		if (IsEditing())
			EndEdit();

		// Clips extend to cover their last key so a save never truncates animation.
		for (auto& clip : _data->_animations)
			clip.duration = std::max(clip.duration, AnimationUtils::GetLastKeyTime(clip));

		_mesh->Save();

		_saved.clear();
		for (const auto& clip : _data->_animations)
			_saved.push_back(AnimationUtils::CloneAnimation(clip));
		_dirty = false;
		return true;
	}

	void AnimationDocument::DiscardChanges()
	{
		if (_data == nullptr)
			return;

		_editBefore = nullptr;
		_data->_animations.clear();
		for (const auto& clip : _saved)
			_data->_animations.push_back(AnimationUtils::CloneAnimation(clip));
		_clipIndex = std::clamp(_clipIndex, 0, std::max(0, (int32_t)_data->_animations.size() - 1));
		_undo.clear();
		_redo.clear();
		++_data->_revision;
		_dirty = false;
	}
}
