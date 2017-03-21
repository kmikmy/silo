#pragma once

#include <cinttypes>
#include <atomic>

namespace hpcs {
    class Epoch {
	public:
		static bool epoch_flag;
		static uint64_t epoch_us;
		static std::atomic<uint32_t> globalEpoch;
		static std::atomic<uint32_t> durableEpoch;
    }; // class Epoch

	class FOID{
	public:
		static uint32_t gapFillInterval;
	};
}
