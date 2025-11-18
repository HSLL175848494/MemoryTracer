#pragma once
#ifdef HS_ENABLE_TRACING

#include <map>
#include <mutex>
#include <string>
#include <atomic>

namespace HSLL
{
	namespace Utils
	{
		/// Forward declaration for allocation tracking information
		struct AllocationInfo;

		/**
		 * @brief Memory tracer for detecting and reporting memory leaks
		 *
		 * This class provides static methods to start and stop memory tracing.
		 * When active, it tracks all dynamic memory allocations and captures
		 * stack traces to help identify the source of memory leaks.
		 *
		 * Note: What is tracked are all memory allocations that
		 * occur from the call to StartTracing to the call to EndTracing,
		 * rather than within a specific code region.
		 * All new/delete operations during the tracing period will be recorded.
		 */
		class MemoryTracer final
		{
		public:
			/**
			 * @brief Starts the memory tracing session
			 *
			 * Clears any previous allocation records and begins tracking
			 * all subsequent memory allocations.
			 */
			static void StartTracing();

			/**
			 * @brief Ends the memory tracing session and generates leak report
			 * @return std::string Detailed leak report in formatted text
			 *
			 * Stops tracing and analyzes all outstanding allocations to
			 * generate a comprehensive leak report grouped by stack traces.
			 */
			static std::string EndTracing();

			/**
			 * @brief Constructor - creates the memory tracer instance
			 */
			MemoryTracer();

			/**
			 * @brief Destructor that disables tracing
			 */
			~MemoryTracer();

		private:
			/**
			 * @brief Internal implementation of starting tracing session
			 */
			void Start();

			/**
			 * @brief Internal implementation of ending tracing session
			 * @return std::string Formatted leak report
			 */
			std::string End();

			/**
			 * @brief Checks if tracing is currently active
			 * @return bool True if tracing is enabled and active
			 */
			bool IsTracingActive();

			/**
			 * @brief Records a memory allocation with stack trace
			 * @param pMem Pointer to the allocated memory
			 * @param uSize Size of the allocation in bytes
			 */
			void RecordAllocation(void* pMem, size_t uSize);

			/**
			 * @brief Records a memory deallocation
			 * @param pMem Pointer to the memory being deallocated
			 */
			void RecordDeallocation(void* pMem);

			/**
			 * @brief Generates a formatted leak report from allocation data
			 * @param mapAlloc Map of active allocations to analyze
			 * @return std::string Formatted leak report
			 */
			std::string GenerateLeakReport(const std::map<void*, AllocationInfo>& mapAlloc);

			std::mutex m_oMutexAlloc;                          ///< Mutex for thread-safe access to allocation records
			std::atomic<bool> m_bTracingActive;                ///< Atomic flag indicating if tracing is active
			std::map<void*, AllocationInfo> m_mapAllocations;  ///< Map tracking all active allocations

			static MemoryTracer m_oInstance;                   ///< Singleton instance

			/**
			 * @brief Thread-local flag to prevent recursive tracing during stack capture
			 *
			 * This flag is used to avoid infinite recursion when memory allocations
			 * occur within the tracing code itself (e.g., during stack trace capture).
			 * When set to false, memory operations are not recorded.
			 */
			static thread_local bool m_bTracingEnabled;

			// Friend declarations for global operator overrides
			friend void* ::operator new(size_t uSize);
			friend void ::operator delete(void* pMem) noexcept;
			friend void* ::operator new[](size_t uSize);
			friend void ::operator delete[](void* pMem) noexcept;
		};
	}
}

#ifdef _MSC_VER
#pragma warning(disable:28251)
#endif

/**
 * @brief Overloaded global new operator with memory tracking
 *
 * Tracks memory allocations when HS_ENABLE_TRACING is defined and
 * tracing is active. Captures stack trace for each allocation.
 */
void* operator new(size_t uSize);

/**
 * @brief Overloaded global delete operator with memory tracking
 *
 * Records memory deallocations when HS_ENABLE_TRACING is defined
 * and tracing is active.
 */
void operator delete(void* pMem) noexcept;

/**
 * @brief Overloaded global array new operator with memory tracking
 */
void* operator new[](size_t uSize);

/**
 * @brief Overloaded global array delete operator with memory tracking
 */
void operator delete[](void* pMem) noexcept;

#endif