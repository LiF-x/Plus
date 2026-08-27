#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

#include "cm_platform.h"

namespace Lifx
{
	//template <typename _Ty>
	//class CMGlobalVarMap
	//{
	//	std::unordered_map<const char* TKey, _Ty TVal> map_;

	//public:

	//	template <typename _Ty>
	//	void Set(const char* key, _Ty val)
	//	{
	//		//
	//	}

	//	template <typename _Ty>
	//	_Ty Get(const char* key)
	//	{
	//		//
	//	}
	//};

	// ------------------------------------------------------------------

	class AuxGManager
	{
	private:

		static AuxGManager* instance_;
		static std::mutex instance_guard_;

	protected:

		AuxGManager() : gScriptingInit(false)
		{}

		~AuxGManager()
		{}

		struct PointersMap
		{
		private:

			std::unordered_map<std::string, LPVOID> pointers_;

		public:

			void Set(std::string const& key, LPVOID val)
			{
				auto iter = pointers_.find(key);
				if (iter != pointers_.end())
				{
					iter->second = val;
				}
				else
				{
					pointers_.insert(std::make_pair(key, val));
				}
			}

			LPVOID Get(std::string const& key)
			{
				auto result = pointers_.find(key);
				if (result != pointers_.end())
				{
					return result->second;
				}
				else
				{
					return nullptr;
				}
			}

		};

		PointersMap pointers_;

	public:

		AuxGManager(AuxGManager&) = delete;
		void operator=(const AuxGManager&) = delete;

		static AuxGManager& GetInstance()
		{
			std::lock_guard<std::mutex> pg(instance_guard_); if (instance_ == nullptr) { instance_ = new AuxGManager(); } return *instance_;
		}

		// /////////////////////////////////////////////

		// global access raw pointers (todo: make it more.. reliable)

		PointersMap& Pointers() { return pointers_; }

		// global access variables

		S32 gWorldId;
		S32 gYeldTimeout;

		bool gScriptingInit;
	};
}

#define gSpace Lifx::AuxGManager::GetInstance()
