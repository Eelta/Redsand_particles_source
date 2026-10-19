#pragma once





#define DKU_C_VERSION_MAJOR 1
#define DKU_C_VERSION_MINOR 1
#define DKU_C_VERSION_REVISION 4


#pragma warning(push)
#pragma warning(disable: 4244)


#include "Impl/PCH.hpp"


#ifndef CONFIG_ENTRY

#	if defined(F4SEAPI)
#		define CONFIG_ENTRY "Data\\F4SE\\Plugins\\"
#	elif defined(SKSEAPI)
#		define CONFIG_ENTRY "Data\\SKSE\\Plugins\\"
#	else
#		define CONFIG_ENTRY ""
#	endif

#endif


namespace DKUtil
{
	constexpr auto DKU_C_VERSION = DKU_C_VERSION_MAJOR * 10000 + DKU_C_VERSION_MINOR * 100 + DKU_C_VERSION_REVISION;
}


#include "Impl/Config/Shared.hpp"

#include "Impl/Config/Data.hpp"

#include "Impl/Config/Ini.hpp"
#include "Impl/Config/Json.hpp"
#include "Impl/Config/Schema.hpp"
#include "Impl/Config/Toml.hpp"

#include "Impl/Config/Proxy.hpp"


namespace DKUtil::Alias
{
	using Boolean = DKUtil::Config::detail::AData<bool>;
	using Integer = DKUtil::Config::detail::AData<std::int64_t>;
	using Double = DKUtil::Config::detail::AData<double>;
	using String = DKUtil::Config::detail::AData<std::basic_string<char>>;

	using IniConfig = DKUtil::Config::Proxy<DKUtil::Config::FileType::kIni>;
	using JsonConfig = DKUtil::Config::Proxy<DKUtil::Config::FileType::kJson>;
	using TomlConfig = DKUtil::Config::Proxy<DKUtil::Config::FileType::kToml>;
	using SchemaConfig = DKUtil::Config::Proxy<DKUtil::Config::FileType::kSchema>;
	using DynamicConfig = DKUtil::Config::Proxy<DKUtil::Config::FileType::kDynamic>;
}


#pragma warning(pop)
