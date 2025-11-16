#include "HS_Leak.h"
#ifdef HS_ENABLE_TRACING

#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <dlfcn.h>
#include <cxxabi.h>
#endif

namespace HSLL
{
	namespace Utils
	{

		class StackTraceInfo
		{
		public:
			static constexpr int MAX_STACK_DEPTH = 62;

			uint64_t m_uHash;
			uint32_t m_uCount;
			void* m_aFrames[MAX_STACK_DEPTH];

			StackTraceInfo() : m_uCount(0), m_uHash(0)
			{
				std::fill_n(m_aFrames, MAX_STACK_DEPTH, nullptr);
			}

			bool operator==(const StackTraceInfo& oOther) const
			{
				if (m_uHash != oOther.m_uHash)
				{
					return false;
				}

				if (m_uCount != oOther.m_uCount)
				{
					return false;
				}

				for (uint32_t i = 0; i < m_uCount; ++i)
				{
					if (m_aFrames[i] != oOther.m_aFrames[i])
					{
						return false;
					}
				}

				return true;
			}

			bool operator<(const StackTraceInfo& oOther) const
			{
				if (m_uHash != oOther.m_uHash)
				{
					return m_uHash < oOther.m_uHash;
				}

				if (m_uCount != oOther.m_uCount)
				{
					return m_uCount < oOther.m_uCount;
				}

				for (uint32_t i = 0; i < m_uCount; ++i)
				{
					if (m_aFrames[i] != oOther.m_aFrames[i])
					{
						return reinterpret_cast<uintptr_t>(m_aFrames[i]) < reinterpret_cast<uintptr_t>(oOther.m_aFrames[i]);
					}
				}

				return false;
			}
		};

		class StackTraceCapturer
		{
			static std::atomic<bool> m_bInited;
#ifdef _WIN32
			static std::mutex m_oMutexSymbols;
#endif

		public:

			static bool InitializeSymbols()
			{
				if (!m_bInited.load(std::memory_order_acquire))
				{
#ifdef _WIN32
					std::lock_guard<std::mutex> oLock(m_oMutexSymbols);
					if (!m_bInited.load(std::memory_order_relaxed))
					{
						SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
						bool bResult = SymInitialize(GetCurrentProcess(), NULL, TRUE);
						m_bInited.store(bResult, std::memory_order_release);
						return bResult;
					}
#else
					m_bInited.store(true, std::memory_order_release);
#endif
				}
				return true;
			}

			static uint64_t CalculateHash(void** aFrames, uint32_t uCount)
			{
				uint64_t uHash = 14695981039346656037ULL;

				for (uint32_t i = 0; i < uCount; ++i)
				{
					uintptr_t frameValue = reinterpret_cast<uintptr_t>(aFrames[i]);
					uHash ^= (frameValue >> 32) & 0xFFFFFFFF;
					uHash *= 1099511628211ULL;
					uHash ^= frameValue & 0xFFFFFFFF;
					uHash *= 1099511628211ULL;
				}
				return uHash;
			}

			static StackTraceInfo Capture()
			{
				StackTraceInfo oInfo;

#ifdef _WIN32
				oInfo.m_uCount = CaptureStackBackTrace(0, StackTraceInfo::MAX_STACK_DEPTH, oInfo.m_aFrames, NULL);
#else
				oInfo.m_uCount = backtrace(oInfo.m_aFrames, StackTraceInfo::MAX_STACK_DEPTH);
#endif

				oInfo.m_uHash = CalculateHash(oInfo.m_aFrames, oInfo.m_uCount);
				return oInfo;
			}

			static std::string DemangleSymbol(const char* pszSymbol)
			{
#if defined(__GNUC__) && !defined(_WIN32)
				char* pDemangled = nullptr;
				int nStatus = -1;

				if (pszSymbol != nullptr)
				{
					pDemangled = abi::__cxa_demangle(pszSymbol, nullptr, nullptr, &nStatus);
				}

				if (nStatus == 0 && pDemangled != nullptr)
				{
					std::string strResult(pDemangled);
					std::free(pDemangled);
					return strResult;
				}

				return pszSymbol ? pszSymbol : "Unknown";
#else
				return pszSymbol ? pszSymbol : "Unknown";
#endif
			}

			static std::vector<std::string> ResolveSymbols(const StackTraceInfo& stStackTrace)
			{
				std::vector<std::string> vecSymbols;

				if (stStackTrace.m_uCount == 0)
				{
					return vecSymbols;
				}

#ifdef _WIN32
				InitializeSymbols();
				std::lock_guard<std::mutex> oLock(m_oMutexSymbols);
				HANDLE pHandle = GetCurrentProcess();

				for (uint32_t i = 0; i < stStackTrace.m_uCount; ++i)
				{
					char aBuffer[sizeof(SYMBOL_INFO) + 256 * sizeof(char)] = { 0 };
					PSYMBOL_INFO pSymbol = reinterpret_cast<PSYMBOL_INFO>(aBuffer);
					pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
					pSymbol->MaxNameLen = 255;
					DWORD64 ullDisplacement = 0;

					if (SymFromAddr(pHandle, reinterpret_cast<DWORD64>(stStackTrace.m_aFrames[i]), &ullDisplacement, pSymbol))
					{
						vecSymbols.push_back(std::string(pSymbol->Name));
					}
					else
					{
						std::stringstream ss;
						ss << "0x" << std::hex << stStackTrace.m_aFrames[i] << std::dec;
						vecSymbols.push_back(ss.str());
					}
				}
#else
				char** ppszSymbols = backtrace_symbols(stStackTrace.m_aFrames, stStackTrace.m_uCount);
				if (ppszSymbols)
				{
					for (uint32_t i = 0; i < stStackTrace.m_uCount; ++i)
					{
						Dl_info stInfo;
						if (dladdr(stStackTrace.m_aFrames[i], &stInfo) && stInfo.dli_sname)
						{
							std::string strDemangled = DemangleSymbol(stInfo.dli_sname);

							std::stringstream ss;
							ss << strDemangled << " [0x" << std::hex << stStackTrace.m_aFrames[i] << std::dec << "]";
							if (stInfo.dli_fname)
							{
								ss << " in " << stInfo.dli_fname;
							}
							vecSymbols.push_back(ss.str());
						}
						else
						{
							vecSymbols.push_back(ppszSymbols[i] ? ppszSymbols[i] : "Unknown");
						}
					}
					std::free(ppszSymbols);
				}
				else
				{
					for (uint32_t i = 0; i < stStackTrace.m_uCount; ++i)
					{
						std::stringstream ss;
						ss << "0x" << std::hex << stStackTrace.m_aFrames[i] << std::dec;
						vecSymbols.push_back(ss.str());
					}
				}
#endif

				return vecSymbols;
			}
		};

		struct AllocationInfo
		{
			size_t m_uSize;
			StackTraceInfo m_stStackTrace;

			AllocationInfo()
				: m_uSize(0)
			{
			}

			AllocationInfo(size_t uSize, const StackTraceInfo& stStackTrace)
				: m_uSize(uSize)
				, m_stStackTrace(stStackTrace)
			{
			}
		};

		struct LeakGroup
		{
			size_t m_uCount = 0;
			size_t m_uTotalSize = 0;
			StackTraceInfo m_stStackTrace;
		};

		class ScopedTraceDisabler
		{
		private:
			bool& m_bTraceFlag;

		public:
			ScopedTraceDisabler(bool& bFlag)
				: m_bTraceFlag(bFlag)
			{
				m_bTraceFlag = false;
			}

			~ScopedTraceDisabler()
			{
				m_bTraceFlag = true;
			}

			ScopedTraceDisabler(const ScopedTraceDisabler&) = delete;
			ScopedTraceDisabler& operator=(const ScopedTraceDisabler&) = delete;
		};

		MemoryTracer  MemoryTracer::m_oInstance;
		thread_local bool MemoryTracer::m_bTracingEnabled = true;

		Utils::MemoryTracer::MemoryTracer()
			: m_bTracingActive(false)
		{
		}

		void Utils::MemoryTracer::Start()
		{
			ScopedTraceDisabler oDisabler(m_bTracingEnabled);
			std::lock_guard<std::mutex> oLock(m_oMutexAlloc);

			if (IsTracingActive())
			{
				return;
			}

			m_bTracingActive.store(false, std::memory_order_release);
			m_mapAllocations.clear();
			m_bTracingActive.store(true, std::memory_order_release);
		}

		std::string Utils::MemoryTracer::End()
		{
			ScopedTraceDisabler oDisabler(m_bTracingEnabled);
			std::map<void*, AllocationInfo> mapResult;

			{
				std::lock_guard<std::mutex> oLock(m_oMutexAlloc);

				if (!IsTracingActive())
				{
					return GenerateLeakReport(mapResult);
				}

				mapResult = std::move(m_mapAllocations);
				m_mapAllocations.clear();
				m_bTracingActive.store(false, std::memory_order_release);
			}

			return GenerateLeakReport(mapResult);
		}

		Utils::MemoryTracer::~MemoryTracer()
		{
			m_bTracingEnabled = false;
		}

		void MemoryTracer::StartTracing()
		{
			m_oInstance.Start();
		}

		std::string MemoryTracer::EndTracing()
		{
			return m_oInstance.End();
		}

		bool MemoryTracer::IsTracingActive()
		{
			return m_bTracingActive.load(std::memory_order_acquire);
		}

		void MemoryTracer::RecordAllocation(void* pPtr, size_t uSize)
		{
			if (!IsTracingActive() || !m_bTracingEnabled)
			{
				return;
			}

			ScopedTraceDisabler oDisabler(m_bTracingEnabled);
			StackTraceInfo stTraceInfo = StackTraceCapturer::Capture();

			std::lock_guard<std::mutex> oLock(m_oMutexAlloc);
			m_mapAllocations[pPtr] = AllocationInfo(uSize, stTraceInfo);
		}

		void MemoryTracer::RecordDeallocation(void* pPtr)
		{
			ScopedTraceDisabler oDisabler(m_bTracingEnabled);
			std::lock_guard<std::mutex> oLock(m_oMutexAlloc);

			auto it = m_mapAllocations.find(pPtr);
			if (it != m_mapAllocations.end())
			{
				m_mapAllocations.erase(it);
			}
		}

		std::string MemoryTracer::GenerateLeakReport(const std::map<void*, AllocationInfo>& mapAllocations)
		{
			std::stringstream ssReport;

			if (mapAllocations.empty())
			{
				ssReport << "No memory leaks detected.\n";
				return ssReport.str();
			}

			std::map<StackTraceInfo, LeakGroup> mapLeakGroups;

			for (std::map<void*, AllocationInfo>::const_iterator it = mapAllocations.begin();
				it != mapAllocations.end(); ++it)
			{
				const void* pAddress = it->first;
				const AllocationInfo& oInfo = it->second;

				LeakGroup& oGroup = mapLeakGroups[oInfo.m_stStackTrace];
				oGroup.m_uCount++;
				oGroup.m_uTotalSize += oInfo.m_uSize;
				oGroup.m_stStackTrace = oInfo.m_stStackTrace;
			}

			std::vector<std::pair<StackTraceInfo, LeakGroup> > vecSortedGroups(
				mapLeakGroups.begin(), mapLeakGroups.end()
			);

			std::sort(vecSortedGroups.begin(), vecSortedGroups.end(),
				[](const std::pair<StackTraceInfo, LeakGroup>& oA,
					const std::pair<StackTraceInfo, LeakGroup>& oB)
				{
					return oA.second.m_uTotalSize > oB.second.m_uTotalSize;
				});

			ssReport << "================================================================================\n";
			ssReport << "                    MEMORY LEAK REPORT (GROUPED BY STACK TRACE)\n";
			ssReport << "================================================================================\n\n";

			size_t uTotalLeakedBytes = 0;
			size_t uTotalLeakGroups = vecSortedGroups.size();

			for (size_t uGroupIdx = 0; uGroupIdx < vecSortedGroups.size(); ++uGroupIdx)
			{
				const std::pair<StackTraceInfo, LeakGroup>& pair = vecSortedGroups[uGroupIdx];
				const StackTraceInfo& stTrace = pair.first;
				const LeakGroup& oGroup = pair.second;

				ssReport << "LEAK GROUP " << (uGroupIdx + 1) << ":\n";
				ssReport << "  Leak Count: " << oGroup.m_uCount << " allocations\n";
				ssReport << "  Total Size: " << oGroup.m_uTotalSize << " bytes\n";
				ssReport << "  Average Size: " << (oGroup.m_uTotalSize / oGroup.m_uCount) << " bytes\n";
				ssReport << "  Stack Hash: 0x" << std::hex << stTrace.m_uHash << std::dec << "\n";

				std::vector<std::string> vecSymbols = StackTraceCapturer::ResolveSymbols(oGroup.m_stStackTrace);

				ssReport << "  Call Stack:\n";

				for (size_t uFrameIdx = 0; uFrameIdx < vecSymbols.size(); ++uFrameIdx)
				{
					ssReport << "    #" << std::setw(2) << uFrameIdx << " " << vecSymbols[uFrameIdx] << "\n";
				}

				ssReport << "\n--------------------------------------------------------------------------------\n";
				uTotalLeakedBytes += oGroup.m_uTotalSize;
			}

			ssReport << "SUMMARY:\n";
			ssReport << "  Total leak groups: " << uTotalLeakGroups << "\n";
			ssReport << "  Total allocations: " << mapAllocations.size() << "\n";
			ssReport << "  Total leaked memory: " << uTotalLeakedBytes << " bytes\n";
			ssReport << "================================================================================\n";
			return ssReport.str();
		}

		std::atomic<bool> StackTraceCapturer::m_bInited(false);
#ifdef _WIN32
		std::mutex StackTraceCapturer::m_oMutexSymbols;
#endif
	}
}

void* operator new(size_t uSize)
{
	void* pPtr = std::malloc(uSize);

	if (!pPtr)
	{
		throw std::bad_alloc();
	}

	if (HSLL::Utils::MemoryTracer::m_bTracingEnabled &&
		HSLL::Utils::MemoryTracer::m_oInstance.IsTracingActive())
	{
		HSLL::Utils::MemoryTracer::m_oInstance.RecordAllocation(pPtr, uSize);
	}

	return pPtr;
}

void operator delete(void* pMem) noexcept
{
	if (!pMem)
	{
		return;
	}

	if (HSLL::Utils::MemoryTracer::m_bTracingEnabled &&
		HSLL::Utils::MemoryTracer::m_oInstance.IsTracingActive())
	{
		HSLL::Utils::MemoryTracer::m_oInstance.RecordDeallocation(pMem);
	}

	std::free(pMem);
}

void* operator new[](size_t uSize)
{
	return operator new(uSize);
}

void operator delete[](void* pMem) noexcept
{
	operator delete(pMem);
}

#endif