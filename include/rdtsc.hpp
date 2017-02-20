#pragma once

#include <cstdlib>
#include <cstdint>
#include <iostream>

namespace hpcs {
	namespace util {
		constexpr uint64_t CLOCKS_PER_USEC = 1105;
		constexpr uint64_t MAX_N_THREAD = 244;

#ifndef _mm_pause
		static void _mm_pause(){
			__asm__ volatile ("" ::: "memory");
		}
#else
		static void _mm_pause(){
			_mm_pause();
		}
#endif

		class TS {
		public:
			static volatile bool start;
			static uint64_t diff_ts[MAX_N_THREAD];

			static uint64_t rdtsc()
			{
				uint32_t a, d;
				__asm__ volatile ("cpuid" :: "a" (0) : "ebx", "ecx", "edx");
				__asm__ volatile ("rdtsc" : "=a" (a), "=d" (d));
				return uint64_t(a) | (uint64_t(d) << 32);
			}

			static void waitClocks(uint64_t clocks)
			{
				const uint64_t t0 = rdtsc();
				const uint64_t t2 = t0 + clocks;
				uint64_t t1 = t0;
				while (t1 < t2) t1 = rdtsc();
			}

			static void microSleep(size_t usec)
			{
				waitClocks(CLOCKS_PER_USEC * usec);
			}

			static void nanoSleep(size_t nsec)
			{
				waitClocks(CLOCKS_PER_USEC * nsec / 1000);
			}

			static double clockToUSec(uint64_t clocks)
			{
				return (double)clocks / CLOCKS_PER_USEC;
			}

			static void *
			getInitCPUClock(void *_cpu_id){
				int cpu_id = *((int *)_cpu_id);
				cpu_set_t cpu_set;

				/* initialize and set cpu flag */
				CPU_ZERO(&cpu_set);
				CPU_SET(cpu_id, &cpu_set);

				pthread_setaffinity_np(pthread_self(),
									   sizeof(cpu_set_t), &cpu_set);
				while(!start){ hpcs::util::_mm_pause(); }
				diff_ts[cpu_id] = rdtsc(); // 各スレッドで同時に rdtsc を呼び出して差を補正する

				return NULL;
			}

			static void setDiffTs(){
				pthread_t th[MAX_N_THREAD];
				int ncpu = sysconf(_SC_NPROCESSORS_CONF);
				if(ncpu < 0){ perror("sysconf"); exit(1);}

				for(int i = 0; i < ncpu; i++){
					int *cpu_id = (int *)malloc(sizeof(int));
					*cpu_id = i;

					if( pthread_create(&th[i], NULL, getInitCPUClock, cpu_id) != 0 ){
						perror("pthread_create()");
						exit(1);
					}
				}
				usleep(1000);
				start = true;

				for(int i = 0; i < ncpu; i++){
					pthread_join(th[i], NULL);
				}

				//				std::cout << ncpu << std::endl;
				//				for(size_t i = 0; i< ncpu; ++i){
				//					std::cout << "[" << i << "] " << diff_ts[i] << std::endl;
				//				}

			}

			static uint64_t normalizedRdtsc(){
				int cpuid = sched_getcpu();
				if(cpuid == -1){ perror("getcpuid"); exit(1);}
				return rdtsc() - diff_ts[cpuid];
			}

		}; // class TS
	}
}
