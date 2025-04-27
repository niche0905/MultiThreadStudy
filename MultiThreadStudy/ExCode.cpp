#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <vector>
#include <unordered_set>
#include <set>

constexpr int MAX_THREADS = 16;


struct HISTORY {
	std::vector <int> push_values, pop_values;
};
std::atomic_int stack_size;

class NODE {
public:
	int	key;
	NODE* volatile next;
	NODE(int x = 0) : key(x), next(nullptr)
	{

	}
};

class DUMMYMUTEX
{
public:
	void lock() {}
	void unlock() {}
};

class C_STACK {
	NODE* volatile m_top;
	std::mutex st_lock;
public:
	C_STACK()
	{
		m_top = nullptr;
	}
	void clear()
	{
		while (-2 != Pop());
	}
	void Push(int x)
	{
		NODE* new_node = new NODE(x);

		std::lock_guard safe_guard(st_lock);

		new_node->next = m_top;
		m_top = new_node;
	}
	int Pop()
	{
		std::unique_lock safe_guard(st_lock);

		if (nullptr == m_top)
			return -2;

		auto temp = m_top;
		int return_value = temp->key;
		m_top = m_top->next;

		safe_guard.unlock();

		delete temp;

		return return_value;
	}
	void print20()
	{
		NODE* p = m_top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

class LF_STACK {
	NODE* volatile m_top;
	bool CAS(NODE* old_p, NODE* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&m_top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	LF_STACK()
	{
		m_top = nullptr;
	}
	void clear()
	{
		while (-2 != Pop());
	}
	void Push(int x)
	{
		NODE* new_node = new NODE(x);

		while (true)
		{
			auto head = m_top;
			new_node->next = head;

			if (true == CAS(head, new_node))
				return;
		}
	}
	int Pop()
	{
		while (true) {
			auto head = m_top;
			if (nullptr == head)
				return -2;

			auto next = head->next;

			if (head != m_top) continue;
			int v = head->key;

			if (true == CAS(head, next)) {
				//delete head;

				return v;
			}
		}
	}
	void print20()
	{
		NODE* p = m_top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

class BackOff
{
	int minDelay, maxDelay;
	int limit;
public:
	BackOff(int min, int max)
		: minDelay(min), maxDelay(max), limit(min) {}
	void InterruptedException()
	{
		int delay = 0;
		if (limit != 0) delay = rand() % limit;
		limit *= 2;
		if (limit > maxDelay) limit = maxDelay;
		std::this_thread::sleep_for(std::chrono::microseconds(delay));
	}
};

class LF_BO_STACK {
	NODE* volatile m_top;
	bool CAS(NODE* old_p, NODE* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&m_top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	LF_BO_STACK()
	{
		m_top = nullptr;
	}
	void clear()
	{
		while (-2 != Pop());
	}
	void Push(int x)
	{
		BackOff bo{ 1, 16 };

		NODE* new_node = new NODE(x);

		while (true)
		{
			auto head = m_top;
			new_node->next = head;

			if (true == CAS(head, new_node))
				return;

			bo.InterruptedException();
		}
	}
	int Pop()
	{
		BackOff bo{ 1, 16 };

		while (true) {
			auto head = m_top;
			if (nullptr == head)
				return -2;

			auto next = head->next;

			if (head != m_top) continue;
			int v = head->key;

			if (true == CAS(head, next)) {
				//delete head;

				return v;
			}

			bo.InterruptedException();
		}
	}
	void print20()
	{
		NODE* p = m_top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

constexpr int MAX_EXCHANGER = MAX_THREADS / 2 - 1;
constexpr int RET_TIMEOUT = -2;
constexpr int RET_BUSYTIMEOUT = -3;
constexpr int RET_POP = -1;

constexpr int ST_EMPTY = 0;
constexpr int ST_WAIT = 1;
constexpr int ST_BUSY = 2;

constexpr int MAX_LOOP = 10;

class LockFreeExchanger {
	volatile unsigned int slot;
	unsigned int get_slot(unsigned int* v)
	{	// 2 비트를 상태를 표현하기위해 사용
		unsigned t = slot;
		*v = (t >> 30);
		unsigned ret = t & 0x3FFFFFFF;
		if (ret == 0x3FFFFFFF)
			return 0xFFFFFFFF;
		return ret;
	}
	unsigned int get_state()
	{
		return (slot >> 30);
	}
	bool CAS(unsigned int old_v, unsigned int new_v, unsigned int old_st, unsigned int new_st)
	{
		// 여기도 버그
		// 값 v 가 바뀌어야 한다
		unsigned int o_slot = (old_v & 0x3FFFFFFF) | (old_st << 30);
		unsigned int n_slot = (new_v & 0x3FFFFFFF) | (new_st << 30);

		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_uint*>(&slot),
			&o_slot, n_slot
		);
	}
public:
	int exchange(int v)
	{
		unsigned int st = 0;
		for (int i = 0; i < MAX_LOOP; ++i) {
			unsigned int old_v = get_slot(&st);

			switch (st)
			{
			case ST_EMPTY:
				if (true == CAS(old_v, v, ST_EMPTY, ST_WAIT)) {
					bool time_out = true;
					for (int j = 0; j < MAX_LOOP; ++j) {
						if (ST_WAIT == get_state())
							continue;
						time_out = false;
						break;
					}

					if (false == time_out) {
						int ret = get_slot(&st);
						slot = 0;
						return ret;
					}
					else {
						if (true == CAS(v, 0, ST_WAIT, ST_EMPTY))
							return RET_TIMEOUT;		// 기다려조 오지 않아서 리턴
						else {
							int ret = get_slot(&st);
							slot = 0;
							return ret;
						}
					}
				}
				break;
			case ST_WAIT:
				if (true == CAS(old_v, v, ST_WAIT, ST_BUSY))
					return old_v;
				break;
			case ST_BUSY:
				break;
			default:
				std::cout << "Invalid Exchange State Error\n";
				exit(-1);
			}
		}

		return RET_BUSYTIMEOUT;		// 너무 바빠서 (경쟁이 심해서)
	}
};

class EliminationArray {
	int range;
	LockFreeExchanger exchanger[MAX_EXCHANGER];
public:
	EliminationArray() { range = 1; }
	~EliminationArray() {}
	int Visit(int value) {
		int slot = rand() % range;
		int ret = exchanger[slot].exchange(value);

		int old_range = range;
		if (ret == RET_BUSYTIMEOUT) {	// TIME OUT
			if (old_range < MAX_EXCHANGER - 1) {	// 방이 적어서 늘림
				std::atomic_compare_exchange_strong(
					reinterpret_cast<std::atomic_int*>(&range),
					&old_range, old_range + 1);
			}
		}
		else if (ret == RET_TIMEOUT) {
			if (old_range > 1) {	// 방이 많아서 줄임
				std::atomic_compare_exchange_strong(
					reinterpret_cast<std::atomic_int*>(&range),
					&old_range, old_range - 1);
			}
		}
		else {
			// 소거 된거임
			//std::cout << "el";
		}

		return ret;
	}
};


class LF_EL_STACK {
	EliminationArray m_earr;
	NODE* volatile m_top;
	bool CAS(NODE* old_p, NODE* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&m_top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	LF_EL_STACK()
	{
		m_top = nullptr;
	}
	void clear()
	{
		while (-2 != Pop());
	}
	void Push(int x)
	{
		BackOff bo{ 1, 16 };

		NODE* new_node = new NODE(x);

		while (true)
		{
			auto head = m_top;
			new_node->next = head;

			if (true == CAS(head, new_node))
				return;

			//bo.InterruptedException();
			int ret = m_earr.Visit(x);
			if (ret == RET_POP)
				return;
		}
	}
	int Pop()
	{
		BackOff bo{ 1, 16 };

		while (true) {
			auto head = m_top;
			if (nullptr == head)
				return -2;

			auto next = head->next;

			if (head != m_top) continue;
			int v = head->key;

			if (true == CAS(head, next)) {
				//delete head;

				return v;
			}

			//bo.InterruptedException();
			int ret = m_earr.Visit(RET_POP);
			if (ret >= 0) return ret;
		}
	}
	void print20()
	{
		NODE* p = m_top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

const int NUM_TEST = 10000000;

LF_EL_STACK my_stack;
thread_local int thread_id;

void benchmark(const int th_id, const int num_thread)
{
	int key = 0;
	int loop_count = NUM_TEST / num_thread;
	thread_id = th_id;

	for (int i = 0; i < loop_count; i++) {
		if ((i < 32) || (rand() % 2 == 0)) {
			my_stack.Push(key++);
		}
		else {
			my_stack.Pop();
		}
	}
}

void benchmark_test(const int th_id, const int num_threads, HISTORY& h)
{
	thread_id = th_id;
	int loop_count = NUM_TEST / num_threads;
	for (int i = 0; i < loop_count; i++) {
		if ((rand() % 2) || i < 128 / num_threads) {
			h.push_values.push_back(i);
			stack_size++;
			my_stack.Push(i);
		}
		else {
			stack_size--;
			int res = my_stack.Pop();
			if (res == -2) {
				stack_size++;
				if (stack_size > (num_threads * 2 + 2)) {
					//std::cout << "ERROR Non_Empty Stack Returned NULL\n";
					//exit(-1);
				}
			}
			else h.pop_values.push_back(res);
		}
	}
}

void check_history(std::vector <HISTORY>& h)
{
	std::unordered_multiset <int> pushed, poped, in_stack;

	for (auto& v : h)
	{
		for (auto num : v.push_values) pushed.insert(num);
		for (auto num : v.pop_values) poped.insert(num);
		while (true) {
			int num = my_stack.Pop();
			if (num == -2) break;
			poped.insert(num);
		}
	}
	for (auto num : pushed) {
		if (poped.count(num) < pushed.count(num)) {
			std::cout << "Pushed Number " << num << " does not exists in the STACK.\n";
			exit(-1);
		}
		if (poped.count(num) > pushed.count(num)) {
			std::cout << "Pushed Number " << num << " is poped more than " << poped.count(num) - pushed.count(num) << " times.\n";
			exit(-1);
		}
	}
	for (auto num : poped)
		if (pushed.count(num) == 0) {
			std::multiset <int> sorted;
			for (auto num : poped)
				sorted.insert(num);
			std::cout << "There was elements in the STACK no one pushed : ";
			int count = 20;
			for (auto num : sorted)
				std::cout << num << ", ";
			std::cout << std::endl;
			exit(-1);

		}
	std::cout << "NO ERROR detectd.\n";
}


int main()
{
	using namespace std::chrono;

	// 오류 검사
	/*{
		for (int n = 1; n <= MAX_THREADS; n = n * 2) {
			my_stack.clear();
			std::vector<std::thread> tv;
			std::vector<HISTORY> history;
			history.resize(n);
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < n; ++i) {
				tv.emplace_back(benchmark_test, i, n, std::ref(history[i]));
			}
			for (auto& th : tv)
				th.join();
			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			size_t ms = duration_cast<milliseconds>(exec_t).count();
			std::cout << n << " Threads,  " << ms << "ms. ----";
			my_stack.print20();
			check_history(history);
		}
	}*/

	// 무잠금 큐 (Stamp Pointer 사용)
	{
		for (int n = 1; n <= MAX_THREADS; n = n * 2) {
			my_stack.clear();

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
			std::cout << n << " Threads,  " << ms << "ms. ----";
			my_stack.print20();
		}
	}
}
