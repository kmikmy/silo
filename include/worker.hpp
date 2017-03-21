#pragma once

#include <queue>
#include <atomic>
#include "random.hpp"
#include "benchmark.hpp"
#include "rdtsc.hpp"
#include "epoch.hpp"

namespace hpcs {
	constexpr size_t MAX_WORKER_NUM = 256;

	class U1;

	class Client
	{
	public:
		uint64_t ctid;
		uint64_t startTs;
		size_t startTsCpuId;
		uint64_t flushedTs;
		size_t flushedTsCpuId;
		uint64_t endTs;
		size_t endTsCpuId;
	};

	class Worker
	{
		hpcs::util::RandGenerator randGen;
		std::atomic<uint64_t> localEpoch_;
		std::atomic<uint64_t> localTid_;
		std::atomic<uint64_t> durableTid_;
		std::queue<Client> preflushQueue_;
		std::queue<Client> precommitQueue_;
		uint64_t before_ctid_count_;
		uint32_t txCountForGapFilling;

	public:
		static hpcs::DB database;
		static std::atomic<uint64_t> entireDurableTid;
		static std::atomic<uint64_t> entireMaxLocalTid;

		uint64_t entireLatency_;
		uint64_t precommitLatency_;
		uint64_t commitTxNum_;
		uint64_t threadId_;
		Worker(){ init(0); }
		Worker(const Worker& worker) { init(worker.threadId_); }
		Worker(uint64_t threadId){ init(threadId); }

		void init(uint64_t threadId) {
			threadId_ = threadId;
			commitTxNum_ = precommitLatency_ = entireLatency_ = durableTid_ = localTid_ = 0;
			localEpoch_ = before_ctid_count_ = txCountForGapFilling = 0;
			for(size_t i = 0; i < threadId; i++){
				randGen.jump();
			}
			std::queue<Client> empty1;
			std::swap( preflushQueue_, empty1 );
			std::queue<Client> empty2;
			std::swap( precommitQueue_, empty2 );
		}
		uint64_t getLocalTid() {
			return localTid_.load();
		}
		uint64_t getLocalEpoch() {
			return localEpoch_.load();
		}
		uint64_t getDurableTid() {
			return durableTid_.load();
		}
		uint64_t run(){
			Client client;
			client.startTs = hpcs::util::TS::normalizedRdtsc();
			client.startTsCpuId = sched_getcpu();

			hpcs::util::_mm_pause();
			if( hpcs::Epoch::epoch_flag ){
				// epoch
				localEpoch_.store(hpcs::Epoch::globalEpoch.load());
			} else if (hpcs::FOID::gapFillInterval) {
				// FOID with gap filling
				++txCountForGapFilling;
				if(txCountForGapFilling >= hpcs::FOID::gapFillInterval){
					txCountForGapFilling = 0;
					uint64_t maxTid = entireMaxLocalTid.load();
					if(localTid_ < maxTid){
						localTid_ = maxTid;
					}
				}
			} else {
				// FOID without gap filling
			}
			hpcs::util::_mm_pause();

			uint64_t commitTid = hpcs::U1::run(Worker::database, randGen, localTid_);
			if ( commitTid ){
				localTid_.store(commitTid >> 3);

				client.ctid = commitTid >> 3;
				// hpcs::util::TS::microSleep(5); // wal flush
				// client.flushedTs = hpcs::util::TS::normalizedRdtsc();

				preflushQueue_.push(client);
			}

			//			std::cout << "entireLatency: " << entireLatency_ << std::endl;

			return commitTid >> 3;
		}
		void durableCheck(){
			while(!preflushQueue_.empty()){
				Client client = preflushQueue_.front();
				uint64_t currentTs = hpcs::util::TS::normalizedRdtsc();
				if( hpcs::util::TS::clockToUSec(currentTs - client.startTs) < 5 ) {
					return;
				}

				// 40 usec を過ぎていたら durable とみなす
				preflushQueue_.pop();
				client.flushedTs = hpcs::util::TS::normalizedRdtsc();
				client.flushedTsCpuId =  sched_getcpu();
				precommitQueue_.push(client);

				durableTid_.store(client.ctid);
			}
		}
		void reply(){
			while(!precommitQueue_.empty()){
				Client client = precommitQueue_.front();
				//				std::cout << "client.ctid: " << client.ctid << std::endl;

				if( hpcs::Epoch::epoch_flag){
					if( (client.ctid >> (32 - 3)) > hpcs::Epoch::durableEpoch.load()){
						++before_ctid_count_;
						if(before_ctid_count_ > 10000){
							std::cout << (client.ctid >> 29) << " > " << hpcs::Epoch::durableEpoch.load() << std::endl;
						}
						return;
					}
				} else {
					if(client.ctid > entireDurableTid.load()){
						++before_ctid_count_;
						if(before_ctid_count_ > 10000){
							std::cout << client.ctid << " > " << entireDurableTid.load() << std::endl;
						}
						return;
					}
				}

				before_ctid_count_ = 0;

				// client reply
				precommitQueue_.pop();
				client.endTs = hpcs::util::TS::normalizedRdtsc();
				client.endTsCpuId = sched_getcpu();

				//				std::cout << "client.endTs - client.startTs: " << client.endTs - client.startTs << std::endl;
				if(client.endTs - client.startTs > 0 && client.endTs - client.flushedTs > 0){
					uint64_t tmp = hpcs::util::TS::clockToUSec(client.endTs - client.startTs);
					if(tmp > 100){ // 100 usec
						//						std::cout << tmp << "(us)" << " start("<< client.startTsCpuId << "):" << client.startTs << ", precommit:" << client.flushedTs << ", end(" << client.endTsCpuId << "):" << client.endTs << std::endl;
					}

					++commitTxNum_;
					entireLatency_ += hpcs::util::TS::clockToUSec(client.endTs - client.startTs);
					precommitLatency_ += hpcs::util::TS::clockToUSec(client.flushedTs - client.startTs);
				} else {
					std::cout << " timesamp is invalid. " << std::endl;
				}
				//				std::cout << "entireLatency: " << entireLatency_ << std::endl;
			}
		}
	};
}
