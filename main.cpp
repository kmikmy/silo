#include <iostream>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <cassert>
#include <sys/time.h>
#include <sched.h>
#include <omp.h>
#include "include/cmdline.hpp"
//#include "include/siloTid.hpp"
#include "include/worker.hpp"
#include "include/benchmark.hpp"
#include "include/epoch.hpp"

std::vector<hpcs::Worker> workers;
std::vector<size_t> txCount;
std::vector<size_t> abortCount;
std::vector<uint64_t> entireLatency;
std::vector<uint64_t> precommitLatency;
std::vector<uint64_t> commitTxCount;

//silo::SiloTid siloTid;


//std::atomic<size_t> epoch_g(0);
//std::vector<size_t> epoch_l;

volatile bool start;
volatile bool quit;

void init(int n){
	start = false;
	quit = false;
	txCount.resize(n);
	abortCount.resize(n);
	entireLatency.resize(n);
	precommitLatency.resize(n);
	workers.resize(n);
	commitTxCount.resize(n);
	hpcs::Worker::database.init();
	hpcs::Worker::entireDurableTid.store(0);
	hpcs::Worker::entireMaxLocalTid.store(0);
	hpcs::Epoch::globalEpoch.store(0);
	hpcs::Epoch::durableEpoch.store(0);

	//  epoch_g.store(0);
	//  epoch_l.resize(n);

	//  siloTid.init(n);
}


void worker_thread(uint64_t threadId) {
	uint64_t tid;
	uint64_t txCounter = 0;
	uint64_t abortCounter = 0;

	workers[threadId].init(threadId);

	while(!start) hpcs::util::_mm_pause();
	while(!quit){
		hpcs::util::_mm_pause();
		// tid = worker.run();
		tid = workers[threadId].run();
		if( tid != 0 ){
			++txCounter;
		} else {
			++abortCounter;
		}
		workers[threadId].reply();
	}
	entireLatency[threadId] = workers[threadId].entireLatency_;
	precommitLatency[threadId] = workers[threadId].precommitLatency_;
	commitTxCount[threadId] = workers[threadId].commitTxNum_;
	txCount[threadId] = txCounter;
	abortCount[threadId] = abortCounter;
}

void epoch_advancing_thread(uint64_t n){
	// epoch advancing thread
	// local epoch e_w を読んで， E - e_w <= 1 の条件を常に満たすように global epoch E を進める．
	// すなわち，全ての e_w が Eであることを確認してから，E を次に進める．

	while(!start) hpcs::util::_mm_pause();
	while(!quit){
		hpcs::util::_mm_pause();
		hpcs::util::TS::microSleep(hpcs::Epoch::epoch_us);

		uint32_t globalEpoch = hpcs::Epoch::globalEpoch.load();
		//		std::cout << "[" << 0 << "] " << maxTid << std::endl;
		for(size_t i = 0; i < n; ++i){
			//			while( !quit && (workers[i].getLocalTid() >> (32 - 3)) != globalEpoch ){
			while( !quit && (workers[i].getLocalEpoch() != globalEpoch ) ){
				//				assert((workers[i].getLocalTid() >> (32 - 3)) <= globalEpoch);
				//				std::cout << "[" << i << "] " << (workers[i].getLocalTid() >> 29) << " != " << globalEpoch << std::endl;
				hpcs::util::_mm_pause();
			}
		}

		if(quit){ break; }

		hpcs::Epoch::globalEpoch.store(globalEpoch+1);
	}
}

void tid_sync_thread(uint64_t n){
	// max tid を求める．
	while(!start) hpcs::util::_mm_pause();
	while(!quit){
		hpcs::util::TS::microSleep(10);

		hpcs::util::_mm_pause();

		uint64_t maxTid = workers[0].getLocalTid();
		//		std::cout << "[" << 0 << "] " << maxTid << std::endl;
		for(size_t i = 1; i < n; ++i){
			uint64_t lTid = workers[i].getLocalTid();
			//			std::cout << "[" << i << "] " << lTid << std::endl;
			if ( lTid > maxTid ) {
				maxTid = lTid;
			}
		}
		//		std::cout << "[max] " << maxTid << std::endl << std::endl;

		hpcs::Worker::entireMaxLocalTid.store(maxTid, std::memory_order_release);
	}
}
void durable_tid_calc_thread(uint64_t n){
	// min dtid を求める

	while(!start) hpcs::util::_mm_pause();
	while(!quit){
		hpcs::util::_mm_pause();

		uint64_t durableTid = workers[0].getDurableTid();
		//		std::cout << "[" << 0 << "] " << durableTid << std::endl;
		for(size_t i = 1; i < n; ++i){
			uint64_t dTid = workers[i].getDurableTid();
			//			std::cout << "[" << i << "] " << dTid << std::endl;
			if ( dTid < durableTid ) {
				durableTid = dTid;
			}
		}
		//		std::cout << "[min] " << durableTid << std::endl << std::endl;
		//		std::cout << durableTid << std::endl;
		hpcs::Worker::entireDurableTid.store(durableTid);
	}
}

void durable_epoch_calc_thread(uint64_t n){
	// durable epoch calc thread
	while(!start) hpcs::util::_mm_pause();

	while(!quit){
		hpcs::util::_mm_pause();

		uint32_t durableEpoch = workers[0].getDurableTid() >> (32 - 3);
		for(size_t i = 1; i < n; ++i){
			uint32_t dE = workers[i].getDurableTid() >> (32 - 3);
			if( dE < durableEpoch ) {
				durableEpoch = dE;
			}
		}

		hpcs::Epoch::durableEpoch.store( durableEpoch - 1 );
	}
}

static double
getDiffTimeSec(struct timeval begin, struct timeval end){
	double Diff = (end.tv_sec*1000*1000+end.tv_usec) - (begin.tv_sec*1000*1000+begin.tv_usec);
	return Diff / 1000. / 1000. ;
}

int main(int argc, char *argv[]) {
	struct timeval begin, end;
	cmdline::parser p;

	// **************************************************
	// * thanks to http://github.com/tanakh/cmdline
	// * (http://d.hatena.ne.jp/tanakh/20091028)
	// *
	// * template <class T> void parser::add(const std::string &name,
	// *     char short_name=0, const std::string &desc="", bool need=true, const T def=T())
	// **************************************************

	/* max epoch = 40 ms */
	p.add<int>("epoch", 'e', "( 0 - 40 * 1000 [us])", false, 0, cmdline::range(0, 40000));

	p.parse_check(argc, argv);

	if(p.exist("epoch")){
		hpcs::Epoch::epoch_us = p.get<int>("epoch");
		hpcs::Epoch::epoch_flag = true; // flag for epoch
	} else {
		hpcs::Epoch::epoch_us = 0; // [default] for vanishing compiler message
		hpcs::Epoch::epoch_flag = false; // flag for silo-tid
	}
	//  std::cout << "epoch_flag: " << epoch_flag << ", epoch: " << epoch_duration << std::endl;

	hpcs::util::TS::setDiffTs();

	std::cout << "#thread #throughput(tx/sec) #totalPrecommitTx #totalReplyTx #totalAbortTx #entireLatency(us) #precommitLatency(us)" << std::endl;

	for(uint64_t n = 1; n <= 237; ++n){
		init(n);
		omp_set_num_threads(n+3);

#pragma omp parallel
		{
			int thread_num = omp_get_thread_num();
			if(thread_num == 0){ // for measurement thread
				sleep(1);
				gettimeofday(&begin, NULL);
				start = true;
				sleep(3);
				quit = true;
				gettimeofday(&end, NULL);
			} else if (thread_num == 1) {
				if(hpcs::Epoch::epoch_flag) {
					epoch_advancing_thread(n);
				} else {
					tid_sync_thread(n);
				}
			} else if (thread_num == 2){
				if(hpcs::Epoch::epoch_flag) {
					durable_epoch_calc_thread(n);
				} else {
					durable_tid_calc_thread(n);
				}
			} else { // worker
				worker_thread(thread_num - 3);
			}
		}

		//		std::cout << n << std::endl;
		uint64_t total = 0;
		for (uint64_t c : txCount){
			total += c;
		}
		uint64_t totalAbort = 0;
		for (uint64_t c : abortCount){
			totalAbort += c;
		}

		uint64_t totalEntireLatency = 0;
		uint64_t totalPrecommitLatency = 0;
		for (uint64_t l : entireLatency) {
			totalEntireLatency += l;
		}
		for (uint64_t l : precommitLatency) {
			totalPrecommitLatency += l;
		}
		// * 下記は何故かレイテンシが取れない
		// for(hpcs::Worker w : workers){
		// 	totalEntireLatency += w.entireLatency_;
		// 	totalPrecommitLatency += w.precommitLatency_;
		// }
		//		std::cout << totalEntireLatency << std::endl;
		//		std::cout << totalPrecommitLatency << std::endl;
		uint64_t totalCommit = 0;
		for (uint64_t c : commitTxCount) {
			totalCommit += c;
		}

		std::cout << n << " " <<
			total / getDiffTimeSec(begin, end) << " " <<
			total << " " <<
			totalCommit << " " <<
			totalAbort << " " <<
			totalEntireLatency / totalCommit << " " <<
			totalPrecommitLatency / totalCommit << " " << std::endl;
	} // for

}

