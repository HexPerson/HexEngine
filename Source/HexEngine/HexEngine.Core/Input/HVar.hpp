
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class HEX_API HVar
	{
	public:
		enum class Type
		{
			None,
			Bool,
			Int8,
			UInt8,
			Int16,
			UInt16,
			Int32,
			UInt32,
			Float32,
			Float64,
			Int64,
			UInt64,
			Vector3,
			//String
		};

		HVar(const HVar& other)
		{

		}

		template <typename T>
		HVar(const char* name, const char* description, T val, T min, T max) :
			_name(name),
			_description(description),
			_type(Type::None)
		{
			Create();
		}

		template <>
		HVar(const char* name, const char* description, bool val, bool min, bool max) :
			_name(name),
			_description(description),
			_type(Type::Bool)
		{
			_val.b = val;
			_min.b = min;
			_max.b = max;

			Create();
		}

		template <>
		HVar(const char* name, const char* description, float val, float min, float max) :
			_name(name),
			_description(description),
			_type(Type::Float32)
		{
			_val.f32 = val;
			_min.f32 = min;
			_max.f32 = max;

			Create();
		}

		template <>
		HVar(const char* name, const char* description, int32_t val, int32_t min, int32_t max) :
			_name(name),
			_description(description),
			_type(Type::Int32)
		{
			_val.i32 = val;
			_min.i32 = min;
			_max.i32 = max;

			Create();
		}

		template <>
		HVar(const char* name, const char* description, uint32_t val, uint32_t min, uint32_t max) :
			_name(name),
			_description(description),
			_type(Type::UInt32)
		{
			_val.ui32 = val;
			_min.ui32 = min;
			_max.ui32 = max;

			Create();
		}

		template <>
		HVar(const char* name, const char* description, math::Vector3 val, math::Vector3 min, math::Vector3 max) :
			_name(name),
			_description(description),
			_type(Type::Vector3)
		{
			_val.v3 = val;
			_min.v3 = min;
			_max.v3 = max;

			Create();
		}

		/*template <>
		HVar(const char* name, const char* description, const char* val, uint32_t min, uint32_t max) :
			_name(name),
			_description(description),
			_type(Type::String)
		{
			_val.ui32 = val;
			_min.ui32 = min;
			_max.ui32 = max;

			Create();
		}*/

		void Create();

		Type GetType();

		void Clamp()
		{
			switch (GetType())
			{
			case HVar::Type::Bool: _val.b = std::clamp(_val.b, _min.b, _max.b); break;
			case HVar::Type::Float32: _val.f32 = std::clamp(_val.f32, _min.f32, _max.f32); break;
			case HVar::Type::Float64: _val.f64 = std::clamp(_val.f64, _min.f64, _max.f64); break;
			case HVar::Type::Int8: _val.i8 = std::clamp(_val.i8, _min.i8, _max.i8); break;
			case HVar::Type::Int16: _val.i16 = std::clamp(_val.i16, _min.i16, _max.i16); break;
			case HVar::Type::Int32:_val.i32 = std::clamp(_val.i32, _min.i32, _max.i32); break;
			case HVar::Type::Int64:_val.i64 = std::clamp(_val.i64, _min.i64, _max.i64); break;
			case HVar::Type::UInt8:_val.ui8 = std::clamp(_val.ui8, _min.ui8, _max.ui8); break;
			case HVar::Type::UInt16:_val.ui16 = std::clamp(_val.ui16, _min.ui16, _max.ui16); break;
			case HVar::Type::UInt32:_val.ui32 = std::clamp(_val.ui32, _min.ui32, _max.ui32); break;
			case HVar::Type::UInt64:_val.ui64 = std::clamp(_val.ui64, _min.ui64, _max.ui64); break;
			case HVar::Type::Vector3: 
			{
				_val.v3.x = std::clamp(_val.v3.x, _min.v3.x, _max.v3.x);
				_val.v3.y = std::clamp(_val.v3.y, _min.v3.y, _max.v3.y);
				_val.v3.z = std::clamp(_val.v3.z, _min.v3.z, _max.v3.z);
				break;
			}
			}
		}

		std::string ValToString()
		{
			switch (GetType())
			{
			case HVar::Type::Bool:		return _val.b ? "1" : "0";
			case HVar::Type::Float32:	return std::to_string(_val.f32);
			case HVar::Type::Float64:	return std::to_string(_val.f64);
			case HVar::Type::Int8:		return std::to_string(_val.i8);
			case HVar::Type::Int16:		return std::to_string(_val.i16);
			case HVar::Type::Int32:		return std::to_string(_val.i32);
			case HVar::Type::Int64:		return std::to_string(_val.i64);
			case HVar::Type::UInt8:		return std::to_string(_val.ui8);
			case HVar::Type::UInt16:	return std::to_string(_val.ui16);
			case HVar::Type::UInt32:	return std::to_string(_val.ui32);
			case HVar::Type::UInt64:	return std::to_string(_val.ui64);
			case HVar::Type::Vector3:	return std::to_string(_val.v3.x) + " " + std::to_string(_val.v3.y) + " " + std::to_string(_val.v3.z);
			default: return "";
			}
		}

	public:
		union Val
		{
			Val() {}
			~Val() {}
			bool b;

			int8_t i8;
			uint8_t ui8;

			int16_t i16;
			uint16_t ui16;

			int32_t i32;
			uint32_t ui32;

			float f32;
			double f64;

			int64_t i64;
			uint64_t ui64;

			math::Vector3 v3;

			//char s[16];
		} _val, _min, _max;

		Type _type;

		std::string _name;
		std::string _description;

		HVar* _next = nullptr;
	};

	extern HEX_API HVar* g_hvars;
	extern HEX_API int32_t g_numVars;
}
