#pragma once

#include <cinttypes>
#include <atomic>

namespace silo {
    constexpr size_t CACHE_LINE_SIZE = 64;
    constexpr size_t MAX_WORKER_NUM = 256;

    class SiloTid
    {
		alignas(CACHE_LINE_SIZE)

		uint64_t nworker_;
		// atomic is unnecessary ?
		std::atomic<uint64_t> localTid_[MAX_WORKER_NUM];
		std::atomic<uint64_t> durableTid_[MAX_WORKER_NUM];
		std::atomic<uint64_t> maxTid_;
		std::atomic<uint64_t> minDTid_;

    public:
		SiloTid(){ init(0); }
		SiloTid(uint64_t nworker) { init(nworker); }
		void init(uint64_t nworker){
			nworker_ = nworker;
			for (size_t i = 0; i < MAX_WORKER_NUM; i++) {
				localTid_[i].store(0);
			}
			maxTid_.store(0);
			minDTid_.store(0);
		}
		void setDTid(uint64_t threadId, uint64_t dTid){
			durableTid_[threadId].store(dTid);
		}

		void setMaxTid(){
			uint64_t maxTid;
			maxTid = localTid_[0].load();
			for(size_t i = 1; i < nworker_; i++){
				uint64_t iTid = localTid_[i].load();
				if(maxTid < iTid){
					maxTid = iTid;
				}
			}
			maxTid_.store(maxTid);
		}
		void setMinDTid(){
			uint64_t minDTid;
			minDTid = localTid_[0].load();
			for(size_t i = 1; i < nworker_; i++){
				uint64_t iTid = localTid_[i].load();
				if(minDTid < iTid){
					minDTid = iTid;
				}
			}
			minDTid_.store(minDTid);
		}
		uint64_t getMaxTid(){
			return maxTid_.load();
		}
		uint64_t getMinDTid(){
			return minDTid_.load();
		}
    }; // SiloTid
}
