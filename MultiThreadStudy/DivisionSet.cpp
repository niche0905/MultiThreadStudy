/*-------------------------------------------------
	수업 중 진행한 Set을 이용
-------------------------------------------------*/

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <array>
#include <atomic>
#include <queue>
#include <set>

constexpr int MAX_THREADS = 16;
thread_local int thread_id;

class LFNODE;
class SPTR
{
	std::atomic<long long> sptr;
public:
	SPTR() : sptr(0) {}
	void set_ptr(LFNODE* ptr)
	{
		sptr = reinterpret_cast<long long>(ptr);
	}
	LFNODE* get_ptr()
	{
		return reinterpret_cast<LFNODE*>(sptr & 0xFFFFFFFFFFFFFFFE);
	}
	LFNODE* get_ptr(bool* removed)
	{
		long long p = sptr;
		*removed = (1 == (p & 1));
		return reinterpret_cast<LFNODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}
	bool get_removed()
	{
		return (1 == (sptr & 1));
	}
	bool CAS(LFNODE* old_p, LFNODE* new_p, bool old_m, bool new_m)
	{
		long long old_v = reinterpret_cast<long long>(old_p);
		if (true == old_m) old_v = old_v | 1;
		else
			old_v = old_v & 0xFFFFFFFFFFFFFFFE;
		long long new_v = reinterpret_cast<long long>(new_p);
		if (true == new_m) new_v = new_v | 1;
		else
			new_v = new_v & 0xFFFFFFFFFFFFFFFE;
		return std::atomic_compare_exchange_strong(&sptr, &old_v, new_v);
	}
};

class LFNODE
{
public:
	int key;
	SPTR next;
	LFNODE(int v) : key(v) {}
};

class LF_SET {
	LFNODE head{ (int)(0x80000000) }, tail{ (int)(0x7FFFFFFF) };
public:
	LF_SET()
	{
		head.next.set_ptr(&tail);
	}
	void clear()
	{
		while (head.next.get_ptr() != &tail) {
			auto p = head.next.get_ptr();
			head.next.set_ptr(p->next.get_ptr());
			delete p;
		}
	}

	void Find(int x, LFNODE*& prev, LFNODE*& curr)
	{
		while (true) {
		retry:
			prev = &head;
			curr = prev->next.get_ptr();

			while (true) {
				// 청소
				bool removed = false;
				do {
					LFNODE* succ = curr->next.get_ptr(&removed);
					if (removed == true) {
						if (false == prev->next.CAS(curr, succ, false, false)) goto retry;
						curr = succ;
					}
				} while (removed == true);

				while (curr->key >= x) return;
				prev = curr;
				curr = curr->next.get_ptr();
			}
		}
	}

	bool Add(int x)
	{
		auto p = new LFNODE{ x };
		while (true) {
			// 검색 Phase
			LFNODE* prev, * curr;
			Find(x, prev, curr);

			if (curr->key == x) {
				delete p;
				return false;
			}
			else {
				p->next.set_ptr(curr);
				if (true == prev->next.CAS(curr, p, false, false))
					return true;
			}
		}
	}
	bool Remove(int x)
	{
		while (true) {
			LFNODE* prev, * curr;
			Find(x, prev, curr);
			if (curr->key != x) {
				return false;
			}
			else {
				LFNODE* succ = curr->next.get_ptr();
				if (false == curr->next.CAS(succ, succ, false, true))
					continue;
				prev->next.CAS(curr, succ, false, false);
				return true;
			}
		}
	}
	bool Contains(int x)
	{
		LFNODE* curr = head.next.get_ptr();
		while (curr->key < x) {
			curr = curr->next.get_ptr();
		}
		return (false == curr->next.get_removed()) && (curr->key == x);
	}
	int print20(int x = 0)
	{
		LFNODE* p = head.next.get_ptr();
		int cnt{x};
		for (int i = x; i < 20; ++i) {
			if (p == &tail) break;
			std::cout << p->key << ", ";
			++cnt;
			p = p->next.get_ptr();
		}
		std::cout << std::endl;
		return cnt;
	}
};

class DIV_SET
{
	LF_SET sets[10];
public:
	DIV_SET()
	{
		for (int i = 0; i < 10; ++i) {
			sets[i];
		}
	}
	~DIV_SET()
	{
		clear();
	}
	void clear()
	{
		for (int i = 0; i < 10; ++i) {
			sets[i].clear();
		}
	}
	bool Add(int x)
	{
		return sets[x / 100].Add(x);
	}
	bool Remove(int x)
	{
		return sets[x / 100].Remove(x);
	}
	bool Contains(int x)
	{
		return sets[x / 100].Contains(x);
	}
	void print20()
	{
		int cnt{};
		for (int i = 0; i < 10; ++i) {
			cnt += sets[i].print20(cnt);
			if (cnt >= 20)
				break;
		}
	}
};

#define MY_SET DIV_SET
MY_SET my_set;

const int NUM_TEST = 4000000;
const int KEY_RANGE = 1000;

class HISTORY {
public:
	int op;
	int i_value;
	bool o_value;
	HISTORY(int o, int i, bool re) : op(o), i_value(i), o_value(re) {}
};

std::array<std::vector<HISTORY>, 16> history;

void worker_check(int num_threads, int th_id)
{
	thread_id = th_id;
	for (int i = 0; i < NUM_TEST / num_threads; ++i) {
		int op = rand() % 3;
		switch (op) {
		case 0: {
			int v = rand() % KEY_RANGE;
			history[thread_id].emplace_back(0, v, my_set.Add(v));
			break;
		}
		case 1: {
			int v = rand() % KEY_RANGE;
			history[thread_id].emplace_back(1, v, my_set.Remove(v));
			break;
		}
		case 2: {
			int v = rand() % KEY_RANGE;
			history[thread_id].emplace_back(2, v, my_set.Contains(v));
			break;
		}
		}
	}
}

void check_history(int num_threads)
{
	std::array <int, KEY_RANGE> survive = {};
	std::cout << "Checking Consistency : ";
	if (history[0].size() == 0) {
		std::cout << "No history.\n";
		return;
	}
	for (int i = 0; i < num_threads; ++i) {
		for (auto& op : history[i]) {
			if (false == op.o_value) continue;
			if (op.op == 3) continue;
			if (op.op == 0) survive[op.i_value]++;
			if (op.op == 1) survive[op.i_value]--;
		}
	}
	for (int i = 0; i < KEY_RANGE; ++i) {
		int val = survive[i];
		if (val < 0) {
			std::cout << "ERROR. The value " << i << " removed while it is not in the set.\n";
			exit(-1);
		}
		else if (val > 1) {
			std::cout << "ERROR. The value " << i << " is added while the set already have it.\n";
			exit(-1);
		}
		else if (val == 0) {
			if (my_set.Contains(i)) {
				std::cout << "ERROR. The value " << i << " should not exists.\n";
				exit(-1);
			}
		}
		else if (val == 1) {
			if (false == my_set.Contains(i)) {
				std::cout << "ERROR. The value " << i << " shoud exists.\n";
				exit(-1);
			}
		}
	}
	std::cout << " OK\n";
}

void benchmark(const int th_id, const int num_thread)
{
	int key;

	thread_id = th_id;

	for (int i = 0; i < NUM_TEST / num_thread; i++) {
		switch (rand() % 3) {
		case 0: key = rand() % KEY_RANGE;
			my_set.Add(key);
			break;
		case 1: key = rand() % KEY_RANGE;
			my_set.Remove(key);
			break;
		case 2: key = rand() % KEY_RANGE;
			my_set.Contains(key);
			break;
		default: std::cout << "Error\n";
			exit(-1);
		}
	}
}

int main()
{
	using namespace std::chrono;

	/*std::cout << "Erro Check...\n";
	{
		for (int n = 1; n <= MAX_THREADS; n = n * 2) {
			my_set.clear();
			for (auto& v : history)
				v.clear();
			std::vector<std::thread> tv;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < n; ++i) {
				tv.emplace_back(worker_check, n, i);
			}
			for (auto& th : tv)
				th.join();
			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			size_t ms = duration_cast<milliseconds>(exec_t).count();
			std::cout << n << " Threads,  " << ms << "ms.";
			check_history(n);
			my_set.print20();
		}
	}*/

	std::cout << "Benchmark...\n\n";

	std::cout << "[Lock Free]\n";
	{
		for (int n = 1; n <= MAX_THREADS; n = n * 2) {
			my_set.clear();
			std::vector<std::thread> tv;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < n; ++i) {
				tv.emplace_back(benchmark, i, n);
			}
			for (auto& th : tv)
				th.join();
			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			size_t ms = duration_cast<milliseconds>(exec_t).count();
			std::cout << n << " Threads,  " << ms << "ms.";
			my_set.print20();
		}
	}
}