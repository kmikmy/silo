#pragma once
#include <cassert>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <cstdio>

namespace hpcs {
	constexpr size_t CACHE_LINE_SIZE = 64;
	//	constexpr size_t DB_SIZE = 4294967296; // 2 ^ 32
	constexpr size_t DB_SIZE = 0xFFFF; // = 65536 = 2 ^ 16

	class Record
	{
	public:
		/*
		  val_ -> tid_ の順で書き込み，
		  tid_ -> val_ の順で読み込む．
		  val_ の値の読み込みは，後の tid_ による更新を読んでしまうかもしれないが，
		  val_ の書き込み時は tid_ に lock bit が付与されており，
		  付与されなくなったら，tid_ は新しい値に更新されたということなので，
		  どちらにしろ validation phase で abort が発生する．
		*/

		uint64_t key_;
		uint64_t val_;
		std::atomic<uint64_t> tid_;

		Record() : key_(), val_() {
			tid_.store(0);
		}
		Record(const Record &rec) {
			key_ = rec.key_;
			val_ = rec.val_;
			tid_.store(rec.tid_);
		}
		Record& operator= (const Record &rec) {
			key_ = rec.key_;
			val_ = rec.val_;
			tid_.store(rec.tid_);

			return(*this);
		}

		/* return lock status */
		bool locked(){
			return tid_.load() & 1;
		}

		void unlock(){
			uint64_t newTid = tid_.load();
			fflush(stdout);
			assert( (newTid & 1) ); // lock status
			newTid = newTid ^ 1;
			tid_.store(newTid);
		}

		/* return true if acquire lock; otherwise return false */
		bool trylock(){
			uint64_t expected = tid_.load();
			if (locked()){
				return false;
			}
			uint64_t desired;
			desired = expected | 1 ; // lock-bit turn on

			return tid_.compare_exchange_weak(expected, desired);
		}


	}; // class Record

	class DB
	{
		alignas(CACHE_LINE_SIZE)
		Record record_[DB_SIZE];
	public:
		DB() { init(); }
		void init(){
			for(size_t i = 0; i < DB_SIZE; ++i){
				record_[i].key_ = i;
				record_[i].val_ = 0;
				record_[i].tid_.store(0);
			}
		}
		Record getRecord(uint64_t recId){
			return record_[recId];
		}

		Record& getRecordRef(uint64_t recId){
			return record_[recId];
		}

		bool trylock(uint64_t recId){
			return record_[recId].trylock();
		}

		void unlock(uint64_t recId){
			record_[recId].unlock();
		}

		void putRecord(Record &r){
		//uint64_t recId, uint64_t newVal, uint64_t commitTid){
			assert(!(r.tid_.load() & 1));
			record_[r.key_].val_ = r.val_;
			record_[r.key_].tid_.store(r.tid_.load());
		}
	}; // class DB
}
