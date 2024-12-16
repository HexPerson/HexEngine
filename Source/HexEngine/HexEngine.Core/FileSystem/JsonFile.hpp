
#pragma once

#include "DiskFile.hpp"
#include "../Physics/IRigidBody.hpp"

namespace HexEngine
{
	class JsonFile : public DiskFile
	{
	public:
		JsonFile(const fs::path& absolutePath, std::ios_base::openmode openMode, DiskFileOptions options = DiskFileOptions::None) :
			DiskFile(absolutePath, openMode, options)
		{}

		JsonFile(const DiskFile& file) = delete;

		json& Serialize(json& container, const std::string& key, const json& value)
		{
			container[key] = value;
			return container;
		}

		template <typename T>
		json& Serialize(json& container, const std::string& key, const T& value)
		{
			container[key] = value;
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const math::Vector3& value)
		{
			container[key] = { value.x, value.y, value.z };
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const dx::XMFLOAT3& value)
		{
			container[key] = { value.x, value.y, value.z };
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const math::Vector4& value)
		{
			container[key] = { value.x, value.y, value.z, value.w };
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const math::Quaternion& value)
		{
			container[key] = { value.x, value.y, value.z, value.w };
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const IRigidBody::ColliderData::Box& value)
		{
			return Serialize(container[key], "aabb", value.aabb);
		}

		template <>
		json& Serialize(json& container, const std::string& key, const dx::BoundingBox& value)
		{
			Serialize(container, "center", value.Center);
			Serialize(container, "extents", value.Extents);
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const IRigidBody::ColliderData::Capsule& value)
		{
			Serialize(container[key], "radius", value.radius);
			Serialize(container[key], "height", value.height);
			return container;
		}

		template <>
		json& Serialize(json& container, const std::string& key, const IRigidBody::ColliderData::Sphere& value)
		{
			Serialize(container[key], "radius", value.radius);
			return container;
		}

		// Deserializers
		json& Deserialize(json& container, const std::string& key, json& value)
		{
			if (container.find(key) != container.end())
				value = container[key];
			return container;
		}
		
		template <typename T>
		json& Deserialize(json& container, const std::string& key, T& value)
		{
			if (container.find(key) != container.end())
				container[key].get_to<T>(value);
			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, math::Vector3& value)
		{
			if (container.find(key) != container.end())
			{
				auto values = container[key];

				values[0].get_to<float>(value.x);
				values[1].get_to<float>(value.y);
				values[2].get_to<float>(value.z);
			}

			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, dx::XMFLOAT3& value)
		{
			if (container.find(key) != container.end())
			{
				auto values = container[key];

				values[0].get_to<float>(value.x);
				values[1].get_to<float>(value.y);
				values[2].get_to<float>(value.z);
			}

			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, math::Vector4& value)
		{
			if (container.find(key) != container.end())
			{
				auto values = container[key];

				values[0].get_to<float>(value.x);
				values[1].get_to<float>(value.y);
				values[2].get_to<float>(value.z);
				values[3].get_to<float>(value.w);
			}

			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, math::Quaternion& value)
		{
			if (container.find(key) != container.end())
			{
				auto values = container[key];

				values[0].get_to<float>(value.x);
				values[1].get_to<float>(value.y);
				values[2].get_to<float>(value.z);
				values[3].get_to<float>(value.w);
			}

			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, IRigidBody::ColliderData::Box& value)
		{
			return Deserialize(container[key], "aabb", value.aabb);
		}

		template <>
		json& Deserialize(json& container, const std::string& key, dx::BoundingBox& value)
		{
			Deserialize(container, "center", value.Center);
			Deserialize(container, "extents", value.Extents);
			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, IRigidBody::ColliderData::Capsule& value)
		{
			Deserialize(container, "radius", value.radius);
			Deserialize(container, "height", value.height);
			return container;
		}

		template <>
		json& Deserialize(json& container, const std::string& key, IRigidBody::ColliderData::Sphere& value)
		{
			Deserialize(container, "radius", value.radius);
			return container;
		}
	};
}
