

#pragma once

#include "../../Required.hpp"

#undef DEFINE_HEX_GUID
#define DEFINE_HEX_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
		inline const GUID DECLSPEC_SELECTANY name \
		= { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

using ObjectId = uint32_t;

inline std::string GUID_toString(const GUID& guid)
{
	std::string str(64, '\0');

	sprintf_s(str.data(), 64, "{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return str;
}

#define DEFINE_OBJECT_GUID(classname) \
		static const ObjectId& GetObjectID(){\
			return classname##GUID;\
		}\
		virtual const ObjectId& GetGUID(){\
			return classname##GUID;\
		}
