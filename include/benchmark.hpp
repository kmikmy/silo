#pragma once

#include <cstdlib>
#include <vector>
#include <algorithm>
#include <iostream>
#include "db.hpp"
#include "worker.hpp"
#include "random.hpp"
#include "epoch.hpp"

namespace hpcs {
	class Benchmark
	{
	public:
		static uint64_t run(){ return 0; }
	};

	class U1 : public Benchmark
	{
	public:
		/* return tid if commit, return 0 else if abort */
		static uint64_t run(hpcs::DB& database,
							hpcs::util::RandGenerator& randGen,
							uint64_t localTid)
		{
			uint64_t commitTid = 0;
			std::vector<hpcs::Record> readSet(1), writeSet(1);

			for (size_t i = 0; i < 1; ++i) {
				uint64_t recId = randGen.next() & DB_SIZE; // 2^16
				readSet[i] = writeSet[i] = database.getRecord(recId);
				++writeSet[i].val_;
			}

			// Phase 1
			//			std::cout << "Phase1" << std::endl;
			std::vector<uint64_t> wsIds(writeSet.size());
			for (size_t i = 0; i < writeSet.size(); ++i) {
				wsIds[i] = writeSet[i].key_;
			}
			std::sort(wsIds.begin(), wsIds.end());
			size_t lock_num = 0;

			for(uint64_t wrid : wsIds){
				if ( !database.trylock(wrid) ){
					break;
				}
				++lock_num;
			}

			if( lock_num != wsIds.size() ) {
				for(size_t i = 0; i < lock_num; ++i) {
					database.unlock(wsIds[i]);
				}

				//				std::cout << "write lock can't qcquire" << std::endl;
				return 0; // abort
			}

			uint32_t commitEpoch;
			if( hpcs::Epoch::epoch_flag ){
				commitEpoch = hpcs::Epoch::globalEpoch.load();
			}

			// Phase 2
			if ( !validation(readSet, wsIds, database) ){
				//				std::cout << "validation failed" << std::endl;
				return 0; // abort
			}
			commitTid = generateTid(readSet, writeSet, localTid, commitEpoch);
			//			std::cout << "commitTid: " << (commitTid >> 3) << std::endl;
			//			std::cout << "commitTid: " << commitTid << std::endl;

			// Phase 3
			for (hpcs::Record r : writeSet) {
				r.tid_.store(commitTid);
				database.putRecord(r); // TIDを書き込むと同時にunlock が同時に行われる
				// database.unlock(r.key_); // putRecordで既にロックが解放されている．
			}

			return commitTid;
		}

		/* return true if success, otherwise return false */
		/* Silo commit phase 2 */
		static bool validation(std::vector<hpcs::Record>& rs,
							   std::vector<uint64_t>& wsIds,
							   hpcs::DB& database)
		{
			for (hpcs::Record r : rs) {
				uint64_t rtid = r.tid_.load();
				hpcs::Record& record = database.getRecordRef(r.key_);
				std::vector<uint64_t>::iterator cIter = find( wsIds.begin(), wsIds.end() , r.key_);
				if(record.tid_.load() != rtid || (record.locked() && cIter != wsIds.end())) {
					return false; // abort
				}
			}

			return true;
		}


		static uint64_t generateTid(std::vector<hpcs::Record>& rs,
									std::vector<hpcs::Record>& ws,
									uint64_t localTid,
									uint32_t commitEpoch)
		{
			uint64_t commitTid = localTid;
			for (hpcs::Record r : rs) {
				uint64_t rtid = r.tid_.load() >> 3;
				if(commitTid < rtid){
					commitTid = rtid;
				}
			}
			for (hpcs::Record r : ws) {
				uint64_t rtid = r.tid_.load() >> 3;
				if(commitTid < rtid){
					commitTid = rtid;
				}
			}
			if( hpcs::Epoch::epoch_flag ) {
				if( commitTid < ((uint64_t)commitEpoch << 32) ) {
					commitTid = (uint64_t)commitEpoch << 32;
					return commitTid | (1 << 3); // 3-bit status bit
				} else {
					return (commitTid + 1) << 3; // 3-bit status bit
				}
			} else {
				return (commitTid + 1) << 3; // 3-bit status bit
			}
		}
	};

}
