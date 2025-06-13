#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <array>
#include <vector>
#include <set>


// 상수 정의부
constexpr int MAX_THREADS{ 16 };
constexpr int NUM_TEST{ 40000 };
constexpr int KEY_RANGE{ 1000 };
constexpr int INF{ std::numeric_limits<int>::max() };

thread_local int thread_id;


class HISTORY {
public:
	int op;
	int i_value;
	bool o_value;
	HISTORY(int o, int i, bool re) : op(o), i_value(i), o_value(re) {}
};


// 기존의 Universal Construct Lock-Free 구현 (Set)
struct RESPONSE {
	bool m_bool;
};
enum METHOD { M_ADD, M_REMOVE, M_CONTAINS, M_CLEAR, M_PRINT20 };
struct INVOCATION {
	METHOD m_method;
	int x;
};
class SEQOBJECT {
	std::set<int> m_set;

public:
	SEQOBJECT()
	{

	}
	void Clear()
	{
		m_set.clear();
	}
	RESPONSE apply(INVOCATION& inv)
	{
		RESPONSE r{ true };
		switch (inv.m_method)
		{
		case M_ADD:
			r.m_bool = m_set.insert(inv.x).second;
			break;
		case M_REMOVE:
			r.m_bool = (0 != m_set.count(inv.x));
			if (r.m_bool == true)
				m_set.erase(inv.x);
			break;
		case M_CONTAINS:
			r.m_bool = (0 != m_set.count(inv.x));
			break;
		case M_CLEAR:
			m_set.clear();
			break;
		case M_PRINT20:
			int count = 20;
			for (auto x : m_set) {
				if (count-- == 0) break;
				std::cout << x << ", ";
			}
			std::cout << '\n';
			break;
		}
	}
};
class STD_SEQ_SET {
	SEQOBJECT m_set;

public:
	STD_SEQ_SET()
	{

	}
	void clear()
	{
		INVOCATION inv{ M_CLEAR, 0 };
		m_set.apply(inv);
	}
	bool Add(int x)
	{
		INVOCATION inv{ M_ADD,x };
		return m_set.apply(inv).m_bool;
	}
	bool Remove(int x)
	{
		INVOCATION inv{ M_REMOVE, x };
		return m_set.apply(inv).m_bool;
	}
	bool Contains(int x)
	{
		INVOCATION inv{ M_CONTAINS, x };
		return m_set.apply(inv).m_bool;
	}
	void Print20()
	{
		INVOCATION inv{ M_PRINT20,0 };
		m_set.apply(inv);
	}

};
class U_NODE {
public:
	INVOCATION m_inv;
	int m_seq;
	U_NODE* volatile next;
};
class LFUNV_OBJECT {
	U_NODE* volatile m_head[MAX_THREADS];
	U_NODE tail;
	U_NODE* get_max_head()
	{
		U_NODE* h = m_head[0];
		for (int i = 1; i < MAX_THREADS; ++i) {
			if (h->m_seq < m_head[i]->m_seq)
				h = m_head[i];
		}
		return h;
	}

public:
	LFUNV_OBJECT()
	{
		tail.m_seq = 0;
		tail.next = nullptr;
		for (auto& h : m_head) h = &tail;
	}
	void Clear()
	{
		U_NODE* p = tail.next;
		while (nullptr != p) {
			U_NODE* old_p = p;
			p = p->next;
			delete old_p;
		}
		tail.next = nullptr;
		for (auto& h : m_head) h = &tail;
	}
	void Print20()
	{
		SEQOBJECT std_set;
		U_NODE* p = tail.next;
		while (p != nullptr) {
			std_set.apply(p->m_inv);
			p = p->next;
		}
		INVOCATION inv{ M_PRINT20, 0 };
		std_set.apply(inv);
	}
	RESPONSE apply(INVOCATION& inv)
	{
		U_NODE* prefer = new U_NODE{ inv,0,nullptr };
		while (0 == prefer->m_seq) {
			U_NODE* head = get_max_head();
			long long temp = 0;
			std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&head->next),
				&temp,
				reinterpret_cast<long long>(prefer));
			U_NODE* after = head->next;
			after->m_seq = head->m_seq + 1;
			m_head[thread_id] = after;
		}

		SEQOBJECT std_set;
		U_NODE* p = tail.next;
		while (p != prefer) {
			std_set.apply(p->m_inv);
			p = p->next;
		}
		return std_set.apply(inv);
	}
};
class STD_LF_SET {
	LFUNV_OBJECT m_set;

public:
	STD_LF_SET()
	{

	}
	void Clear()
	{
		m_set.Clear();
	}
	bool Add(int x)
	{
		INVOCATION inv{ M_ADD, x };
		return m_set.apply(inv).m_bool;
	}
	bool Remove(int x)
	{
		INVOCATION inv{ M_REMOVE, x };
		return m_set.apply(inv).m_bool;
	}
	bool Contains(int x)
	{
		INVOCATION inv{ M_CONTAINS, x };
		return m_set.apply(inv).m_bool;
	}
	void Print20()
	{
		m_set.Print20();
	}
};




// 함수 전방선언
void benchmark(int num_threads, int th_id);
void benchmark_check(int num_threads, int th_id);
void check_history(int num_threads);

// 전역변수들
STD_LF_SET g_set;
std::array<std::vector<HISTORY>, 16> history;	// 에러 체크용


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


int main()
{
	// 네임스페이스
	using namespace std::chrono;

	// 알고리즘 정확성 검사 (에러 체크)
	{
		for (int t = 1; t <= 16; t *= 2) {
			g_set.Clear();
			std::vector <std::thread> threads;
			threads.reserve(t);

			auto start_t = system_clock::now();

			for (int j = 0; j < t; ++j)
				threads.emplace_back(benchmark_check, t, j);

			for (auto& th : threads)
				th.join();

			auto end_t = system_clock::now();
			auto exec_t = end_t - start_t;
			auto exec_ms = duration_cast<milliseconds>(exec_t).count();

			std::cout << "Exec time = " << exec_ms << "ms. ";
			std::cout << t << " Threads : SET = ";
			g_set.Print20();
			check_history(t);
			for (auto& h : history) h.clear();
		}
	}

	// 멀티스레드 벤치마크
	{
		for (int t = 1; t <= 16; t *= 2) {
			g_set.Clear();
			std::vector<std::thread> threads;
			threads.reserve(t);

			auto start_t = system_clock::now();

			for (int i = 0; i < t; ++i) {
				threads.emplace_back(benchmark, t, i);
			}

			for (int i = 0; i < t; ++i) {
				threads[i].join();
			}

			auto end_t = system_clock::now();
			auto exec_t = end_t - start_t;
			auto exec_ms = duration_cast<milliseconds>(exec_t).count();
			std::cout << "Exec time = " << exec_ms << "ms. ";
			std::cout << t << " threads Set = ";
			g_set.Print20();
		}

	}
}


// 함수 구현부
void benchmark(int num_threads, int th_id)
{
	thread_id = th_id;

	int key;
	const int num_loop = NUM_TEST / num_threads;

	for (int i = 0; i < num_loop; i++) {
		switch (rand() % 3) {
		case 0:
			key = rand() % KEY_RANGE;
			g_set.Add(key);
			break;
		case 1:
			key = rand() % KEY_RANGE;
			g_set.Remove(key);
			break;
		case 2:
			key = rand() % KEY_RANGE;
			g_set.Contains(key);
			break;
		default:
			std::cout << "Error\n";
			exit(-1);
		}
	}
}
void benchmark_check(int num_threads, int th_id)
{
	thread_id = th_id;

	for (int i = 0; i < NUM_TEST / num_threads; ++i) {
		int op = rand() % 3;
		switch (op) {
		case 0: {
			int v = rand() % KEY_RANGE;
			history[th_id].emplace_back(0, v, g_set.Add(v));
			break;
		}
		case 1: {
			int v = rand() % KEY_RANGE;
			history[th_id].emplace_back(1, v, g_set.Remove(v));
			break;
		}
		case 2: {
			int v = rand() % KEY_RANGE;
			history[th_id].emplace_back(2, v, g_set.Contains(v));
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
			if (g_set.Contains(i)) {
				std::cout << "ERROR. The value " << i << " should not exists.\n";
				exit(-1);
			}
		}
		else if (val == 1) {
			if (false == g_set.Contains(i)) {
				std::cout << "ERROR. The value " << i << " shoud exists.\n";
				exit(-1);
			}
		}
	}
	std::cout << " OK\n";
}
