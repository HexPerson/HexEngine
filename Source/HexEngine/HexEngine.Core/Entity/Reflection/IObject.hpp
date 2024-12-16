

#pragma once

#include "GUID.hpp"

namespace HexEngine
{
	class JsonFile;

	namespace Reflection
	{
		class IObject
		{
		public:
			virtual void Serialize(json& data, JsonFile* file) {
				assert(false && "Should never be executing an unimplemnted IOBject::Save!");
			}

			virtual void Deserialize(json& data, JsonFile* file, uint32_t mask=0) {
				assert(false && "Should never be executing an unimplemented IObject::Load!");
			}
		};

		template <typename T>
		class ITypedObject : public IObject
		{
		public: 
			T GetType()
			{
				return std::remove_reference<decltype(T)>::type;
			}
		};
	}
}
