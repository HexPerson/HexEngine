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
}
