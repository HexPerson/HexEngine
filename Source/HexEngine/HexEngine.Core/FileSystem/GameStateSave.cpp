
#include "GameStateSave.hpp"
#include "SceneSaveFile.hpp"
#include "../HexEngine.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace HexEngine
{
	namespace
	{
		struct BaselineEntity
		{
			std::string head;     // the entity's json minus "components" (layer, flags, prefab link)
			std::string parent;   // parent entity name, "" at the root
			std::vector<std::pair<std::string, std::string>> components;   // component name -> compact json
		};

		struct Baseline
		{
			const Scene* scene = nullptr;
			std::unordered_map<std::string, BaselineEntity> entities;
			std::vector<std::string> order;   // capture order = recreate order
		};

		// Heap-allocated so nothing runs at static destruction (the scene is long gone).
		Baseline* g_baseline = nullptr;

		using Clock = std::chrono::steady_clock;
		double MsSince(Clock::time_point t)
		{
			return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
		}

		bool Saveable(Entity* e)
		{
			return e != nullptr && !e->HasFlag(EntityFlags::DoNotSave) && !e->IsPendingDeletion();
		}

		std::vector<Entity*> SaveableEntities(Scene* scene)
		{
			std::vector<Entity*> out;
			for (auto&& bucket : scene->GetEntities())
				for (auto* e : bucket.second)
					if (Saveable(e))
						out.push_back(e);
			return out;
		}

		// The codec's view of one entity, split into its head and per-component json.
		void SerializeEntity(Entity* e, JsonFile* file, json& head, std::vector<json>& comps)
		{
			json holder = json::object();
			e->Serialize(holder, file);
			head = std::move(holder[e->GetName()]);
			// Runtime bookkeeping bits flip during play without meaning anything to a save.
			if (auto f = head.find("flags"); f != head.end() && f->is_number_integer())
			{
				constexpr int32_t kVolatile = (int32_t)EntityFlags::PreviousTransformDirty |
					(int32_t)EntityFlags::SelectedInEditor | (int32_t)EntityFlags::IsPendingRemoval;
				*f = f->get<int32_t>() & ~kVolatile;
			}
			comps.clear();
			if (auto it = head.find("components"); it != head.end())
			{
				if (it->is_array())
					for (auto& c : *it)
						comps.push_back(std::move(c));
				head.erase(it);
			}
		}

		std::string CompName(const json& comp)
		{
			return comp.is_object() ? comp.value("name", std::string()) : std::string();
		}

		// An in-memory serialiser: Entity/Component::Serialize only use the JsonFile
		// helpers, never the stream. Flagged as a game-state save so components add
		// their runtime state.
		struct StateFile
		{
			static fs::path DefaultPath()
			{
				std::error_code ec;
				fs::path dir = fs::temp_directory_path(ec);
				return (ec ? fs::current_path(ec) : dir) / L"HexEngine_GameState.hsave";
			}
			SceneSaveFile file;
			// SceneSaveFile's constructor creates the parent directory, so the path
			// must be absolute (a bare file name threw from create_directories("")).
			explicit StateFile(const std::shared_ptr<Scene>& scene, const fs::path& path = DefaultPath()) :
				file(path.is_absolute() ? path : DefaultPath(), std::ios::in, scene)
			{
				file.SetGameStateSave(true);
			}
		};

		void SetError(std::string* error, const std::string& msg)
		{
			if (error != nullptr)
				*error = msg;
			LOG_WARN("GameStateSave: %s", msg.c_str());
		}

		// Bring the live scene to baseline + `save` (null = the plain baseline).
		bool ApplyTargetImpl(const std::shared_ptr<Scene>& scene, const json* save, std::string* error)
		{
			if (!scene)
			{
				SetError(error, "no scene");
				return false;
			}
			if (g_baseline == nullptr || g_baseline->scene != scene.get())
			{
				SetError(error, "no baseline captured for this scene");
				return false;
			}

			const auto start = Clock::now();
			StateFile sf(scene);
			JsonFile* file = &sf.file;

			// Hold physics while bodies are moved (as a scene load does).
			struct LoadHold
			{
				SceneManager* m;
				explicit LoadHold(SceneManager* sm) : m(sm) { if (m) m->BeginSceneLoad(); }
				~LoadHold() { if (m) m->EndSceneLoad(); }
			} hold(g_pEnv != nullptr ? g_pEnv->_sceneManager : nullptr);

			const json* deltas = nullptr;
			std::unordered_set<std::string> destroyed;
			if (save != nullptr)
			{
				if (auto it = save->find("entities"); it != save->end() && it->is_object())
					deltas = &*it;
				if (auto it = save->find("destroyed"); it != save->end() && it->is_array())
					for (const auto& n : *it)
						if (n.is_string())
							destroyed.insert(n.get<std::string>());
			}

			struct Work
			{
				Entity* entity;
				json data;
				bool moved;
			};
			std::vector<Work> work;
			std::vector<std::pair<Entity*, std::string>> reparent;
			size_t removed = 0, created = 0, changedComps = 0;

			json head;
			std::vector<json> liveComps;
			for (const std::string& name : g_baseline->order)
			{
				const BaselineEntity& be = g_baseline->entities.at(name);
				Entity* live = scene->GetEntityByName(name);
				if (live != nullptr && live->IsPendingDeletion())
					live = nullptr;

				if (destroyed.count(name) != 0)
				{
					if (live != nullptr)
					{
						scene->DestroyEntity(live);
						++removed;
					}
					continue;
				}

				const json* delta = nullptr;
				if (deltas != nullptr)
					if (auto it = deltas->find(name); it != deltas->end() && it->is_object())
						delta = &*it;
				const json* deltaComps = nullptr;
				if (delta != nullptr)
					if (auto it = delta->find("components"); it != delta->end() && it->is_array())
						deltaComps = &*it;
				auto findDeltaComp = [&](const std::string& compName) -> const json* {
					if (deltaComps != nullptr)
						for (const auto& c : *deltaComps)
							if (CompName(c) == compName)
								return &c;
					return nullptr;
				};

				bool isNew = false;
				if (live == nullptr)
				{
					json empty = json::object();
					scene->Lock();
					live = Entity::LoadFromFile(empty, name, scene.get(), file);
					scene->Unlock();
					if (live == nullptr)
						continue;
					isNew = true;
					++created;
					if (!be.parent.empty())
						reparent.emplace_back(live, be.parent);
				}

				std::unordered_map<std::string, std::string> liveDumps;
				std::string liveHead;
				if (!isNew)
				{
					SerializeEntity(live, file, head, liveComps);
					liveHead = head.dump();
					for (const auto& c : liveComps)
						liveDumps.emplace(CompName(c), c.dump());
				}

				json data = json::object();
				const json* deltaHead = nullptr;
				if (delta != nullptr)
					if (auto it = delta->find("head"); it != delta->end() && it->is_object())
						deltaHead = &*it;
				const std::string targetHead = deltaHead != nullptr ? deltaHead->dump() : be.head;
				if (isNew || liveHead != targetHead)
					data = deltaHead != nullptr ? *deltaHead : json::parse(be.head);

				json comps = json::array();
				bool moved = isNew;
				std::unordered_set<std::string> seen;
				for (const auto& [compName, baseDump] : be.components)
				{
					seen.insert(compName);
					const json* dc = findDeltaComp(compName);
					const std::string targetDump = dc != nullptr ? dc->dump() : baseDump;
					auto lit = liveDumps.find(compName);
					if (isNew || lit == liveDumps.end() || lit->second != targetDump)
					{
						comps.push_back(dc != nullptr ? *dc : json::parse(baseDump));
						if (compName == "Transform")
							moved = true;
					}
				}
				// Components added at runtime and saved (not in the baseline).
				if (deltaComps != nullptr)
					for (const auto& c : *deltaComps)
						if (const std::string cn = CompName(c); !cn.empty() && seen.count(cn) == 0)
						{
							auto lit = liveDumps.find(cn);
							if (lit == liveDumps.end() || lit->second != c.dump())
								comps.push_back(c);
						}

				if (!comps.empty())
				{
					changedComps += comps.size();
					data["components"] = std::move(comps);
				}
				if (!data.empty())
					work.push_back({ live, std::move(data), moved });
			}

			// Recreated entities hang off their authored parent (local transforms).
			for (auto& [child, parentName] : reparent)
				if (Entity* parent = scene->GetEntityByName(parentName); parent != nullptr && parent != child)
				{
					scene->Lock();
					child->SetParent(parent, false);
					scene->Unlock();
				}

			// Transforms first: other components may read them while deserialising.
			for (auto& w : work)
			{
				if (!w.moved)
					continue;
				scene->Lock();
				w.entity->Deserialize(w.data, file, 1 << Transform::_GetComponentId());
				scene->Unlock();
			}
			for (auto& w : work)
			{
				scene->Lock();
				w.entity->Deserialize(w.data, file);
				scene->Unlock();
			}

			// The deserialise path moves transforms without notifying physics: move
			// every body (and character capsule) to its restored transform.
			bool anyMoved = false;
			for (auto& w : work)
			{
				if (!w.moved)
					continue;
				anyMoved = true;
				auto* body = w.entity->GetComponent<RigidBody>();
				if (body == nullptr)
					continue;
				if (auto* irb = body->GetIRigidBody(); irb != nullptr && irb->IsCharacterController())
				{
					if (auto* tf = w.entity->GetComponent<Transform>(); tf != nullptr)
						tf->SnapInterpolation();
					irb->UpdatePosePosition(w.entity->GetWorldTM().Translation());
				}
				else
					body->TeleportToTransform();
			}
			if (anyMoved)
				scene->ForceRebuildPVS();

			LOG_INFO("GameStateSave: applied %s in %.0f ms - %zu entities touched (%zu components), %zu recreated, %zu removed",
				save != nullptr ? "save" : "baseline", MsSince(start), work.size(), changedComps, created, removed);
			return true;
		}
	}

	static void CaptureBaselineImpl(const std::shared_ptr<Scene>& scene)
	{
		GameStateSave::ClearBaseline();
		if (!scene)
			return;

		const auto start = Clock::now();
		auto* b = new Baseline;
		b->scene = scene.get();
		StateFile sf(scene);

		json head;
		std::vector<json> comps;
		for (Entity* e : SaveableEntities(scene.get()))
		{
			BaselineEntity be;
			SerializeEntity(e, &sf.file, head, comps);
			be.head = head.dump();
			be.parent = e->GetParent() != nullptr ? e->GetParent()->GetName() : std::string();
			be.components.reserve(comps.size());
			for (const auto& c : comps)
				be.components.emplace_back(CompName(c), c.dump());
			b->order.push_back(e->GetName());
			b->entities.emplace(e->GetName(), std::move(be));
		}
		g_baseline = b;
		LOG_INFO("GameStateSave: baseline of %zu entities captured in %.0f ms", b->order.size(), MsSince(start));
	}

	static bool SaveImpl(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error);

	// Never let a save/load throw into the game loop.
	namespace
	{
		bool ApplyTarget(const std::shared_ptr<Scene>& scene, const json* save, std::string* error)
		{
			// A sliced autosave in flight would finish with half old, half loaded state.
			GameStateSave::CancelSave();
			try
			{
				return ApplyTargetImpl(scene, save, error);
			}
			catch (const std::exception& e)
			{
				SetError(error, std::string("load failed: ") + e.what());
				return false;
			}
		}
	}

	void GameStateSave::CaptureBaseline(const std::shared_ptr<Scene>& scene)
	{
		try
		{
			CaptureBaselineImpl(scene);
		}
		catch (const std::exception& e)
		{
			ClearBaseline();
			LOG_WARN("GameStateSave: baseline capture failed: %s", e.what());
		}
	}

	bool GameStateSave::Save(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error)
	{
		try
		{
			return SaveImpl(scene, path, header, error);
		}
		catch (const std::exception& e)
		{
			SetError(error, std::string("save failed: ") + e.what());
			return false;
		}
	}

	void GameStateSave::ClearBaseline()
	{
		GameStateSave::CancelSave();
		delete g_baseline;
		g_baseline = nullptr;
	}

	bool GameStateSave::HasBaseline(const Scene* scene)
	{
		return g_baseline != nullptr && (scene == nullptr || g_baseline->scene == scene);
	}

	namespace
	{
		// A save in progress: the baseline entities are diffed a slice at a time so an
		// autosave never stalls a frame for the whole scene.
		struct PendingSave
		{
			std::weak_ptr<Scene> scene;
			fs::path path;
			json out;
			size_t next = 0;   // index into g_baseline->order
			std::unordered_set<std::string> live;
			size_t changedComps = 0;
			double workMs = 0.0;
			Clock::time_point started;
		};
		PendingSave* g_pending = nullptr;

		void DropPending()
		{
			delete g_pending;
			g_pending = nullptr;
		}

		bool BeginSaveImpl(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error)
		{
			DropPending();
			if (!scene || g_baseline == nullptr || g_baseline->scene != scene.get())
			{
				SetError(error, "no baseline captured for this scene");
				return false;
			}
			auto* p = new PendingSave;
			p->scene = scene;
			p->path = path;
			p->started = Clock::now();
			p->out = json::object();
			p->out["header"] = header.is_object() ? header : json::object();
			p->out["header"]["formatVersion"] = GameStateSave::Version;
			const std::wstring sceneName = scene->GetName();
			p->out["header"]["scene"] = std::string(sceneName.begin(), sceneName.end());
			p->out["header"]["savedAtUnix"] = (int64_t)std::time(nullptr);
			p->out["entities"] = json::object();
			g_pending = p;
			return true;
		}

		// Diff baseline entities until `budgetMs` is spent. True when every entity is done.
		bool StepSaveImpl(double budgetMs)
		{
			PendingSave* p = g_pending;
			auto scene = p->scene.lock();
			if (!scene || g_baseline == nullptr)
				return true;
			const auto t0 = Clock::now();
			StateFile sf(scene, p->path);
			json& entities = p->out["entities"];
			json head;
			std::vector<json> comps;
			const auto& order = g_baseline->order;
			while (p->next < order.size())
			{
				const std::string& name = order[p->next++];
				Entity* e = scene->GetEntityByName(name);
				if (!Saveable(e))
					continue;
				p->live.insert(name);
				const BaselineEntity& be = g_baseline->entities.at(name);

				SerializeEntity(e, &sf.file, head, comps);
				json delta = json::object();
				if (head.dump() != be.head)
					delta["head"] = std::move(head);

				json changed = json::array();
				for (auto& c : comps)
				{
					const std::string cn = CompName(c);
					const std::string* base = nullptr;
					for (const auto& [bn, bd] : be.components)
						if (bn == cn)
						{
							base = &bd;
							break;
						}
					if (base == nullptr || *base != c.dump())
						changed.push_back(std::move(c));
				}
				if (!changed.empty())
				{
					p->changedComps += changed.size();
					delta["components"] = std::move(changed);
				}
				if (!delta.empty())
					entities[name] = std::move(delta);

				if ((p->next & 31) == 0 && MsSince(t0) >= budgetMs)
					break;
			}
			p->workMs += MsSince(t0);
			return p->next >= order.size();
		}

		bool FinishSaveImpl(std::string* error)
		{
			std::unique_ptr<PendingSave> p(g_pending);
			g_pending = nullptr;
			if (p == nullptr)
				return false;
			if (p->scene.expired() || g_baseline == nullptr)
			{
				SetError(error, "the scene went away during the save");
				return false;
			}

			json& destroyed = p->out["destroyed"];
			destroyed = json::array();
			for (const std::string& name : g_baseline->order)
				if (p->live.count(name) == 0)
					destroyed.push_back(name);

			// Write to a temp file and swap it in, so a crash mid-write never eats the slot.
			const fs::path& path = p->path;
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			fs::path tmp = path;
			tmp += L".tmp";
			{
				std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
				if (!f)
				{
					SetError(error, "can't write " + tmp.string());
					return false;
				}
				const std::string text = p->out.dump();
				f.write(text.data(), (std::streamsize)text.size());
				if (!f)
				{
					SetError(error, "write failed for " + tmp.string());
					return false;
				}
			}
			fs::rename(tmp, path, ec);
			if (ec)
			{
				SetError(error, "can't replace " + path.string() + ": " + ec.message());
				return false;
			}

			LOG_INFO("GameStateSave: saved %zu entities (%zu components, %zu destroyed) to '%s' - %.0f ms of work over %.0f ms",
				p->out["entities"].size(), p->changedComps, destroyed.size(), path.string().c_str(), p->workMs, MsSince(p->started));
			return true;
		}
	}

	static bool SaveImpl(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error)
	{
		if (!BeginSaveImpl(scene, path, header, error))
			return false;
		while (!StepSaveImpl(1e9))
		{
		}
		return FinishSaveImpl(error);
	}

	bool GameStateSave::BeginSave(const std::shared_ptr<Scene>& scene, const fs::path& path, const json& header, std::string* error)
	{
		try
		{
			return BeginSaveImpl(scene, path, header, error);
		}
		catch (const std::exception& e)
		{
			DropPending();
			SetError(error, std::string("save failed: ") + e.what());
			return false;
		}
	}

	bool GameStateSave::StepSave(float budgetMs, bool* written, std::string* error)
	{
		if (written != nullptr)
			*written = false;
		if (g_pending == nullptr)
			return true;
		try
		{
			if (!StepSaveImpl((double)budgetMs))
				return false;
			const bool ok = FinishSaveImpl(error);
			if (written != nullptr)
				*written = ok;
			return true;
		}
		catch (const std::exception& e)
		{
			DropPending();
			SetError(error, std::string("save failed: ") + e.what());
			return true;
		}
	}

	bool GameStateSave::IsSaving()
	{
		return g_pending != nullptr;
	}

	void GameStateSave::CancelSave()
	{
		DropPending();
	}

	static bool ReadSaveJson(const fs::path& path, json& out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			return false;
		out = json::parse(f, nullptr, false);
		return !out.is_discarded() && out.is_object();
	}

	bool GameStateSave::Load(const std::shared_ptr<Scene>& scene, const fs::path& path, json* headerOut, std::string* error)
	{
		json save;
		if (!ReadSaveJson(path, save))
		{
			SetError(error, "can't read save '" + path.string() + "'");
			return false;
		}
		const json header = save.value("header", json::object());
		if (header.value("formatVersion", 0) != GameStateSave::Version)
		{
			SetError(error, "save '" + path.string() + "' has an incompatible format version");
			return false;
		}
		if (headerOut != nullptr)
			*headerOut = header;
		return ApplyTarget(scene, &save, error);
	}

	bool GameStateSave::ResetToBaseline(const std::shared_ptr<Scene>& scene, std::string* error)
	{
		return ApplyTarget(scene, nullptr, error);
	}

	bool GameStateSave::ReadHeader(const fs::path& path, json& header)
	{
		json save;
		if (!ReadSaveJson(path, save))
			return false;
		header = save.value("header", json::object());
		return header.is_object();
	}
}
