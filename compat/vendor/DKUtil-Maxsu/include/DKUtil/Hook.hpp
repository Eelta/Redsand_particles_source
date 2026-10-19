#pragma once





#define DKU_H_VERSION_MAJOR 2
#define DKU_H_VERSION_MINOR 5
#define DKU_H_VERSION_REVISION 2


#pragma warning(push)
#pragma warning(disable: 4244)


#include "Impl/PCH.hpp"


namespace DKUtil
{
	constexpr auto DKU_H_VERSION = DKU_H_VERSION_MAJOR * 10000 + DKU_H_VERSION_MINOR * 100 + DKU_H_VERSION_REVISION;
}


#include "Impl/Hook/Shared.hpp"

#include "Impl/Hook/API.hpp"


namespace DKUtil::Alias
{
	using Patch = DKUtil::Hook::Patch;
	using HookHandle = std::unique_ptr<DKUtil::Hook::HookHandle>;
	using CaveHandle = DKUtil::Hook::CaveHookHandle;
	using VMTHandle = DKUtil::Hook::VMTHookHandle;
	using IATHandle = DKUtil::Hook::IATHookHandle;

	using Reg = DKUtil::Hook::Assembly::Register;
	using HookFlag = DKUtil::Hook::HookFlag;
}


#pragma warning(pop)
