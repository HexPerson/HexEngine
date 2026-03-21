#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	class IEditorTransaction
	{
	public:
		virtual ~IEditorTransaction() = default;
		virtual bool Undo() = 0;
		virtual bool Redo() = 0;
		virtual const char* GetLabel() const = 0;
	};

	class EditorTransactionStack
	{
	public:
		bool Push(std::unique_ptr<IEditorTransaction> transaction)
		{
			if (!transaction)
				return false;

			if (_undoStack.size() >= _maxTransactions)
			{
				_undoStack.erase(_undoStack.begin());
			}

			_redoStack.clear();
			_undoStack.push_back(std::move(transaction));
			return true;
		}

		bool Undo()
		{
			if (_undoStack.empty())
				return false;

			auto transaction = std::move(_undoStack.back());
			_undoStack.pop_back();

			if (!transaction->Undo())
			{
				_undoStack.push_back(std::move(transaction));
				return false;
			}

			_redoStack.push_back(std::move(transaction));
			return true;
		}

		bool Redo()
		{
			if (_redoStack.empty())
				return false;

			auto transaction = std::move(_redoStack.back());
			_redoStack.pop_back();

			if (!transaction->Redo())
			{
				_redoStack.push_back(std::move(transaction));
				return false;
			}

			_undoStack.push_back(std::move(transaction));
			return true;
		}

		void Clear()
		{
			_undoStack.clear();
			_redoStack.clear();
		}

	private:
		size_t _maxTransactions = 512;
		std::vector<std::unique_ptr<IEditorTransaction>> _undoStack;
		std::vector<std::unique_ptr<IEditorTransaction>> _redoStack;
	};

	namespace Detail
	{
		struct EntitySnapshot
		{
			std::string entityName;
			std::string parentEntityName;
			json entityData;
		};

		struct EntityHierarchySnapshot
		{
			std::wstring sceneName;
			std::string rootEntityName;
			std::vector<EntitySnapshot> entities;
		};

		inline std::shared_ptr<HexEngine::Scene> GetActiveScene()
		{
			if (!HexEngine::g_pEnv || !HexEngine::g_pEnv->_sceneManager)
				return {};

			return HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
		}

		inline std::wstring GetActiveSceneName()
		{
			const auto scene = GetActiveScene();
			return scene ? scene->GetName() : std::wstring();
		}

		inline HexEngine::Entity* ResolveEntityByName(const std::wstring& sceneName, const std::string& entityName)
		{
			const auto scene = GetActiveScene();
			if (!scene || scene->GetName() != sceneName)
				return nullptr;

			return scene->GetEntityByName(entityName);
		}

		inline bool CaptureEntitySnapshot(HexEngine::Entity* entity, EntitySnapshot& outSnapshot)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;

			outSnapshot.entityName = entity->GetName();
			outSnapshot.parentEntityName = entity->GetParent() ? entity->GetParent()->GetName() : "";

			json data = json::object();
			HexEngine::JsonFile serializer(fs::path(), std::ios::in);
			entity->Serialize(data, &serializer);

			auto it = data.find(outSnapshot.entityName);
			if (it == data.end())
				return false;

			outSnapshot.entityData = *it;
			return true;
		}

		inline bool CaptureEntityHierarchyRecursive(HexEngine::Entity* entity, EntityHierarchySnapshot& outSnapshot)
		{
			EntitySnapshot entitySnapshot;
			if (!CaptureEntitySnapshot(entity, entitySnapshot))
				return false;

			outSnapshot.entities.push_back(std::move(entitySnapshot));

			for (auto* child : entity->GetChildren())
			{
				if (!CaptureEntityHierarchyRecursive(child, outSnapshot))
					return false;
			}

			return true;
		}

		inline bool CaptureEntityHierarchy(HexEngine::Entity* entity, EntityHierarchySnapshot& outSnapshot)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;

			const auto scene = GetActiveScene();
			if (!scene || entity->GetScene() != scene.get())
				return false;

			outSnapshot.sceneName = scene->GetName();
			outSnapshot.rootEntityName = entity->GetName();
			outSnapshot.entities.clear();

			if (!CaptureEntityHierarchyRecursive(entity, outSnapshot))
				return false;

			return !outSnapshot.entities.empty();
		}

		inline HexEngine::Entity* RestoreEntityFromSnapshot(const EntityHierarchySnapshot& snapshot)
		{
			const auto scene = GetActiveScene();
			if (!scene || scene->GetName() != snapshot.sceneName)
				return nullptr;

			if (auto* existing = scene->GetEntityByName(snapshot.rootEntityName); existing != nullptr && !existing->IsPendingDeletion())
				return existing;

			HexEngine::JsonFile serializer(fs::path(), std::ios::in);
			std::vector<HexEngine::Entity*> createdEntities;

			auto rollback = [&]()
			{
				for (auto it = createdEntities.rbegin(); it != createdEntities.rend(); ++it)
				{
					auto* created = *it;
					if (created != nullptr && !created->IsPendingDeletion())
					{
						scene->DestroyEntity(created);
					}
				}
			};

			// Pass 1: create all entities.
			for (const auto& entitySnapshot : snapshot.entities)
			{
				if (scene->GetEntityByName(entitySnapshot.entityName) != nullptr)
				{
					rollback();
					return nullptr;
				}

				json entityData = entitySnapshot.entityData;
				auto* entity = HexEngine::Entity::LoadFromFile(entityData, entitySnapshot.entityName, scene.get(), &serializer);
				if (entity == nullptr)
				{
					rollback();
					return nullptr;
				}

				createdEntities.push_back(entity);
			}

			// Pass 2: deserialize transforms first.
			for (const auto& entitySnapshot : snapshot.entities)
			{
				auto* entity = scene->GetEntityByName(entitySnapshot.entityName);
				if (entity == nullptr)
				{
					rollback();
					return nullptr;
				}

				json entityData = entitySnapshot.entityData;
				entity->Deserialize(entityData, &serializer, 1 << HexEngine::Transform::_GetComponentId());
			}

			// Pass 3: deserialize all components.
			for (const auto& entitySnapshot : snapshot.entities)
			{
				auto* entity = scene->GetEntityByName(entitySnapshot.entityName);
				if (entity == nullptr)
				{
					rollback();
					return nullptr;
				}

				json entityData = entitySnapshot.entityData;
				entity->Deserialize(entityData, &serializer);
			}

			// Pass 4: restore hierarchy.
			for (const auto& entitySnapshot : snapshot.entities)
			{
				if (entitySnapshot.parentEntityName.empty())
					continue;

				auto* entity = scene->GetEntityByName(entitySnapshot.entityName);
				auto* parent = scene->GetEntityByName(entitySnapshot.parentEntityName);
				if (entity == nullptr || parent == nullptr)
				{
					rollback();
					return nullptr;
				}

				entity->SetParent(parent);
			}

			return scene->GetEntityByName(snapshot.rootEntityName);
		}

		inline bool RemoveEntityByName(const std::wstring& sceneName, const std::string& entityName)
		{
			const auto scene = GetActiveScene();
			if (!scene || scene->GetName() != sceneName)
				return false;

			auto* entity = scene->GetEntityByName(entityName);
			if (entity == nullptr)
				return false;

			scene->DestroyEntity(entity);
			return true;
		}

		inline bool RenameEntityByName(const std::wstring& sceneName, const std::string& currentName, const std::string& newName)
		{
			const auto scene = GetActiveScene();
			if (!scene || scene->GetName() != sceneName)
				return false;

			auto* entity = scene->GetEntityByName(currentName);
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;

			std::string finalName;
			if (!scene->RenameEntity(entity, newName, &finalName))
				return false;

			return finalName == newName;
		}
	}

	class PositionTransaction final : public IEditorTransaction
	{
	public:
		PositionTransaction(const std::string& entityName, const math::Vector3& before, const math::Vector3& after) :
			_sceneName(Detail::GetActiveSceneName()),
			_entityName(entityName),
			_before(before),
			_after(after)
		{
		}

		virtual bool Undo() override { return Apply(_before); }
		virtual bool Redo() override { return Apply(_after); }
		virtual const char* GetLabel() const override { return "Move Entity"; }

	private:
		bool Apply(const math::Vector3& position) const
		{
			auto* entity = Detail::ResolveEntityByName(_sceneName, _entityName);
			if (!entity || entity->IsPendingDeletion())
				return false;

			entity->ForcePosition(position);
			return true;
		}

	private:
		std::wstring _sceneName;
		std::string _entityName;
		math::Vector3 _before;
		math::Vector3 _after;
	};

	class ScaleTransaction final : public IEditorTransaction
	{
	public:
		ScaleTransaction(const std::string& entityName, const math::Vector3& before, const math::Vector3& after) :
			_sceneName(Detail::GetActiveSceneName()),
			_entityName(entityName),
			_before(before),
			_after(after)
		{
		}

		virtual bool Undo() override { return Apply(_before); }
		virtual bool Redo() override { return Apply(_after); }
		virtual const char* GetLabel() const override { return "Scale Entity"; }

	private:
		bool Apply(const math::Vector3& scale) const
		{
			auto* entity = Detail::ResolveEntityByName(_sceneName, _entityName);
			if (!entity || entity->IsPendingDeletion())
				return false;

			entity->SetScale(scale);
			return true;
		}

	private:
		std::wstring _sceneName;
		std::string _entityName;
		math::Vector3 _before;
		math::Vector3 _after;
	};

	class MaterialAssignmentTransaction final : public IEditorTransaction
	{
	public:
		MaterialAssignmentTransaction(const std::string& entityName, const fs::path& before, const fs::path& after) :
			_sceneName(Detail::GetActiveSceneName()),
			_entityName(entityName),
			_before(before),
			_after(after)
		{
		}

		virtual bool Undo() override { return Apply(_before); }
		virtual bool Redo() override { return Apply(_after); }
		virtual const char* GetLabel() const override { return "Assign Material"; }

	private:
		bool Apply(const fs::path& materialPath) const
		{
			auto* entity = Detail::ResolveEntityByName(_sceneName, _entityName);
			if (!entity || entity->IsPendingDeletion())
				return false;

			auto* smc = entity->GetComponent<HexEngine::StaticMeshComponent>();
			if (!smc)
				return false;

			std::shared_ptr<HexEngine::Material> material = materialPath.empty()
				? HexEngine::Material::GetDefaultMaterial()
				: HexEngine::Material::Create(materialPath);

			if (!material)
			{
				material = HexEngine::Material::GetDefaultMaterial();
			}

			if (!material)
				return false;

			smc->SetMaterial(material);
			return true;
		}

	private:
		std::wstring _sceneName;
		std::string _entityName;
		fs::path _before;
		fs::path _after;
	};

	class EntityLifecycleTransaction final : public IEditorTransaction
	{
	public:
		enum class SourceAction
		{
			Created,
			Deleted
		};

		static std::unique_ptr<EntityLifecycleTransaction> CreateForCreatedEntity(HexEngine::Entity* entity)
		{
			Detail::EntityHierarchySnapshot snapshot;
			if (!Detail::CaptureEntityHierarchy(entity, snapshot))
				return nullptr;

			return std::make_unique<EntityLifecycleTransaction>(snapshot, SourceAction::Created);
		}

		static std::unique_ptr<EntityLifecycleTransaction> CreateForDeletedEntity(HexEngine::Entity* entity)
		{
			Detail::EntityHierarchySnapshot snapshot;
			if (!Detail::CaptureEntityHierarchy(entity, snapshot))
				return nullptr;

			return std::make_unique<EntityLifecycleTransaction>(snapshot, SourceAction::Deleted);
		}

		EntityLifecycleTransaction(const Detail::EntityHierarchySnapshot& snapshot, SourceAction action) :
			_snapshot(snapshot),
			_sourceAction(action)
		{
		}

		virtual bool Undo() override
		{
			switch (_sourceAction)
			{
			case SourceAction::Created:
				return Detail::RemoveEntityByName(_snapshot.sceneName, _snapshot.rootEntityName);
			case SourceAction::Deleted:
				return Detail::RestoreEntityFromSnapshot(_snapshot) != nullptr;
			}

			return false;
		}

		virtual bool Redo() override
		{
			switch (_sourceAction)
			{
			case SourceAction::Created:
				return Detail::RestoreEntityFromSnapshot(_snapshot) != nullptr;
			case SourceAction::Deleted:
				return Detail::RemoveEntityByName(_snapshot.sceneName, _snapshot.rootEntityName);
			}

			return false;
		}

		virtual const char* GetLabel() const override
		{
			return _sourceAction == SourceAction::Created ? "Create Entity" : "Delete Entity";
		}

	private:
		Detail::EntityHierarchySnapshot _snapshot;
		SourceAction _sourceAction = SourceAction::Created;
	};

	class RenameTransaction final : public IEditorTransaction
	{
	public:
		RenameTransaction(const std::string& beforeName, const std::string& afterName) :
			_sceneName(Detail::GetActiveSceneName()),
			_before(beforeName),
			_after(afterName)
		{
		}

		virtual bool Undo() override { return Detail::RenameEntityByName(_sceneName, _after, _before); }
		virtual bool Redo() override { return Detail::RenameEntityByName(_sceneName, _before, _after); }
		virtual const char* GetLabel() const override { return "Rename Entity"; }

	private:
		std::wstring _sceneName;
		std::string _before;
		std::string _after;
	};

	class ReparentTransaction final : public IEditorTransaction
	{
	public:
		ReparentTransaction(const std::string& childName, const std::string& beforeParentName, const std::string& afterParentName) :
			_sceneName(Detail::GetActiveSceneName()),
			_childName(childName),
			_beforeParentName(beforeParentName),
			_afterParentName(afterParentName)
		{
		}

		virtual bool Undo() override { return Apply(_beforeParentName); }
		virtual bool Redo() override { return Apply(_afterParentName); }
		virtual const char* GetLabel() const override { return "Reparent Entity"; }

	private:
		bool Apply(const std::string& parentName) const
		{
			const auto scene = Detail::GetActiveScene();
			if (!scene || scene->GetName() != _sceneName)
				return false;

			auto* child = scene->GetEntityByName(_childName);
			if (child == nullptr || child->IsPendingDeletion())
				return false;

			HexEngine::Entity* parent = nullptr;
			if (!parentName.empty())
			{
				parent = scene->GetEntityByName(parentName);
				if (parent == nullptr || parent == child || parent->IsPendingDeletion())
					return false;

				for (auto* ancestor = parent; ancestor != nullptr; ancestor = ancestor->GetParent())
				{
					if (ancestor == child)
						return false;
				}
			}

			if (child->GetParent() == parent)
				return true;

			child->SetParent(parent);
			scene->ForceRebuildPVS();
			return true;
		}

	private:
		std::wstring _sceneName;
		std::string _childName;
		std::string _beforeParentName;
		std::string _afterParentName;
	};

	class VisibilityTransaction final : public IEditorTransaction
	{
	public:
		VisibilityTransaction(const std::string& entityName, bool beforeHidden, bool afterHidden) :
			_sceneName(Detail::GetActiveSceneName()),
			_entityName(entityName),
			_beforeHidden(beforeHidden),
			_afterHidden(afterHidden)
		{
		}

		virtual bool Undo() override { return Apply(_beforeHidden); }
		virtual bool Redo() override { return Apply(_afterHidden); }
		virtual const char* GetLabel() const override { return "Toggle Visibility"; }

	private:
		bool Apply(bool targetHidden) const
		{
			const auto scene = Detail::GetActiveScene();
			if (!scene || scene->GetName() != _sceneName)
				return false;

			auto* entity = scene->GetEntityByName(_entityName);
			if (entity == nullptr || entity->IsPendingDeletion())
				return false;

			const bool currentlyHidden = entity->HasFlag(HexEngine::EntityFlags::DoNotRender);
			if (currentlyHidden != targetHidden)
			{
				entity->ToggleVisibility();
				scene->ForceRebuildPVS();
			}

			return entity->HasFlag(HexEngine::EntityFlags::DoNotRender) == targetHidden;
		}

	private:
		std::wstring _sceneName;
		std::string _entityName;
		bool _beforeHidden = false;
		bool _afterHidden = false;
	};
}
