#include <thread>
#include <iostream>
#include <concurrent_unordered_set.h>
#include <concurrent_vector.h>
#include <cmath>
#include <vector>
#include <chrono>

concurrency::concurrent_unordered_set <int> erased_list;
concurrency::concurrent_unordered_set <int> done_threads;
long long g_n;


void worker(int th_id, int p)
{
	if (p * p > g_n) {
		done_threads.insert(th_id);
		return;
	}

	while ((g_n % p) == 0) {
		g_n = g_n / p;
	}

	long long sqrt_n = sqrtl(g_n);
	int duration = 1000;
	int count = 0;
	for (int i = p * p; i < sqrt_n; i = i + p) {
		erased_list.insert(i);
		if ((count++ % duration) == 0) {
			sqrt_n = sqrtl(g_n);
			if (0 != erased_list.count(p))
				break;
		}
	}
	done_threads.insert(th_id);
}

int main()
{
	using namespace std::chrono;

	while (true) {
		std::cout << "Enter a number : ";
		std::cin >> g_n;
		long long original = g_n;

		auto start_t = system_clock::now();
		long long sqrt_n = sqrtl(g_n);
		long long orig_sqrt = sqrt_n;

		std::vector <std::thread> workers;
		int index = 0;
		worker(0, 2);
		for (int p = 3; p < sqrt_n; p = p + 2) {
			if (erased_list.count(p) != 0)
				continue;
			workers.emplace_back(worker, index, p);
			index++;

			while (index - done_threads.size() >= std::thread::hardware_concurrency()) {
				//std::this_thread::sleep_for(1ms);
				std::this_thread::yield();
				sqrt_n = sqrtl(g_n);
			}

		}

		for (auto& th : workers)
			th.join();
		auto end_t = system_clock::now();
		auto exec_t = end_t - start_t;
		auto ms = duration_cast<milliseconds>(exec_t).count();

		std::cout << "Exec Time : " << ms << "ms.  Result = ";
		for (int i = 2; i <= orig_sqrt; ++i) {
			if (0 == erased_list.count(i)) {
				while (original % i == 0) {
					std::cout << i << ", ";
					original = original / i;
					orig_sqrt = sqrtl(original);
				}
			}
		}
	     
		if (1 != original)
			std::cout << original;
		std::cout << std::endl;
		erased_list.clear(); done_threads.clear();
	}

}

/*--------------------------------------------------

  위 코드를 엘릭서에서 스레드 개수 제한 없이 처리하도록

--------------------------------------------------*/