#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <array>
#include <numeric>
#include <atomic>


constexpr int MAX_TOP{ 9 };

class SK_LF_NODE;		// SkipList LockFree Node ���� ����

class SK_SPTR			// SkipList �ռ� ������ (LockFree(CAS)�� ����)
{
private:
	std::atomic<long long> sptr;

public:
	SK_SPTR() : sptr{ 0 } {}

	void set_ptr(SK_LF_NODE* ptr)
	{
		sptr = reinterpret_cast<long long>(ptr);
	}

	SK_LF_NODE* get_ptr()
	{
		long long p = sptr.load();
		return reinterpret_cast<SK_LF_NODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}

	SK_LF_NODE* get_ptr(bool* removed)
	{
		long long p = sptr.load();
		*removed = (1 == (p & 1));	// p�� ������ 1��Ʈ�� removed�� �Ǵ�
		return reinterpret_cast<SK_LF_NODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}

	bool CAS(SK_LF_NODE* old_p, SK_LF_NODE* new_p, bool old_m, bool new_m)
	{
		long long old_v = reinterpret_cast<long long>(old_p);
		if (true == old_m) old_v = old_v | 1;
		else old_v = old_v & 0xFFFFFFFFFFFFFFFE;	// ���� �ʿ��� �۾������� �� �𸣰ڽ��ϴ�

		long long new_v = reinterpret_cast<long long>(new_p);
		if (true == new_m) new_v = new_v | 1;
		else new_v = new_v & 0xFFFFFFFFFFFFFFFE;

		return std::atomic_compare_exchange_strong(&sptr, &old_v, new_v);
		// &sptr ��ġ�� �ִ� ���� old_v�� �� �� ���ٸ� new_v�� �޶� �ٲ��� �������� false�� ��ȯ �� &sptr�� �ִ� ���� old_v�� Ȯ�� ����
	}
};

class SK_LF_NODE		// SkipList LockFree Node 
{
public:
	int key;
	SK_SPTR* volatile next[MAX_TOP + 1] = {};	// 0������ 9��(MAX_TOP)���� ����
	int top_level;	// ���� ����� �ֻ��� (������ �����ϴ� ��)

public:
	SK_LF_NODE(int x, int top) : key{ x }, top_level{ top }
	{
		for (int i = 0; i <= top_level; ++i) {
			next[i] = new SK_SPTR();
		}
	}
	
	~SK_LF_NODE()
	{
		for (int i = 0; i <= top_level; ++i) {
			delete next[i];
		}
	}
};

class LF_SK_SET			// SkipList LockFree Set
{
private:
	SK_LF_NODE head{ std::numeric_limits<int>::min(), MAX_TOP };
	SK_LF_NODE tail{ std::numeric_limits<int>::max(), MAX_TOP };

public:
	LF_SK_SET()
	{
		Init();
	}

	void Init()
	{
		for (int i = 0; i <= MAX_TOP; ++i) {
			head.next[i]->set_ptr(&tail);
		}
	}

	void Clear()
	{
		// 0������ ��ȸ�ϸ鼭 ����
		while (head.next[0]->get_ptr() != &tail) {
			auto p = head.next[0]->get_ptr();
			head.next[0]->set_ptr(head.next[0]->get_ptr()->next[0]->get_ptr());
			delete p;
		}

		// ��� ���� �� head�� next�� tail�� ����
		Init();
	}

	bool Find(int x, SK_LF_NODE* prevs[], SK_LF_NODE* currs[])
	{
	retry:

		// �ֻ��� (������ ���� Ž��)
		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {		// �ֻ��� �� ��� �޾ƿ� ���� ���� ��� ���ǹ�
				prevs[i] = &head;
			}
			else {		// �ֻ����� �ƴϸ� ���� ���� ������ ���� Node ������
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i]->get_ptr();

			while (true) {
				bool removed = false;
				SK_LF_NODE* succ = currs[i]->next[i]->get_ptr(&removed);
				
				// ��ȸ �� �����Ǿ��ٸ� ���������� ���� �õ�
				while (true == removed) {
					// ���� �������� ���ߴٸ� retry
					if (false == prevs[i]->next[i]->CAS(currs[i], succ, false, false)) {
						goto retry;
					}

					// ���� �����ߴٸ� �̾ Ž��
					currs[i] = succ;
					succ = currs[i]->next[i]->get_ptr(&removed);
				}

				// Ž���� curr�� ã������ �� �̻��̶�� ������ �Ǵ� ����
				if (currs[i]->key >= x)
					break;

				// �ƴ϶�� �̾ ��ȸ
				prevs[i] = currs[i];
				currs[i] = succ;
			}
		}

		return (currs[0]->key == x);
	}

	bool Add(int x)
	{
		while (true) {
			SK_LF_NODE* prevs[MAX_TOP + 1];
			SK_LF_NODE* currs[MAX_TOP + 1];

			bool found = Find(x, prevs, currs);

			// �̹� �����Ѵٸ�
			if (true == found)
				return false;

			// ������ ����� ���� (���� �佺)
			int lv = 0;
			for (int i = 0; i < MAX_TOP; ++i) {	// �̰� ���ǹ��� <= ���� MAX_TOP ���� �ϴ� ���� �ƴѰ�?
				if (rand() % 2 == 0) break;
				++lv;
			}

			SK_LF_NODE* new_node = new SK_LF_NODE{ x, lv };

			for (int i = 0; i <= lv; ++i) {
				new_node->next[i]->set_ptr(currs[i]);
			}

			// �������� ���� �߰�������(CAS�� ���������� ���� �߰��� ����)
			if (false == prevs[0]->next[0]->CAS(currs[0], new_node, false, false)) {	// ��� ������ CAS�� �����Ͽ����Ƿ� Find ���� �ٽ�
				delete new_node;
				continue;
			}

			// ���� �߰������� ���� ���鵵 å������ ����
			for (int i = 1; i <= lv; ++i) {
				while (true) {
					if (true == prevs[i]->next[i]->CAS(currs[i], new_node, false, false))
						break;

					Find(x, prevs, currs);
					//new_node->next[i]->set_ptr(currs[i]);	// ���翡�� ���׷� ���� -> ������ EBR���� ���� // ������ ������ ������ �ȵ��ư��⿡ �ּ� ó��
				}
			}

			return true;
		}
	}

	bool Remove(int x)
	{
		while (true) {
			SK_LF_NODE* prevs[MAX_TOP + 1];
			SK_LF_NODE* currs[MAX_TOP + 1];

			bool found = Find(x, prevs, currs);

			// �������� �ʴ´ٸ�
			if (false == found)
				return false;

			// ã�����Ƿ� ���� ���� ����
			SK_LF_NODE* del_node = currs[0];
			int lv = del_node->top_level;

			// �ֻ��� ���� remove ����
			for (int i = lv; i > 0; --i) {
				bool removed = false;
				SK_LF_NODE* succ = del_node->next[i]->get_ptr(&removed);

				// ���� �Ǿ����� Ȯ���ϸ鼭 ���� �õ�
				while (false == removed) {	// ������ �ȵǾ��ٸ� ���� �õ� (CAS)
					del_node->next[i]->CAS(succ, succ, false, true);	// CAS�� ������ Ȯ������ �ʾƵ� ������ Removed ������ ���� �Ʒ� ������ �̵��ϱ� ����
					succ = del_node->next[i]->get_ptr(&removed);
				}
			}

			SK_LF_NODE* succ = del_node->next[0]->get_ptr();
			while (true) {
				bool marking = del_node->next[0]->CAS(succ, succ, false, true);

				// ���� ������ ��ŷ(����)�� �����ϸ� ���� ������ ��!
				if (marking) {
					//delete del_node;	// �޸� �� <- Find���� �������� ���� �� �ؾ������� �ű⼭�� ���� ����
					Find(x, prevs, currs);	// ���Ḯ��Ʈ ���� �۾���
					return true;
				}

				bool removed = false;
				succ = del_node->next[0]->get_ptr(&removed);
				// ���Ű� �Ǿ��µ� ���� CAS�� �����ߴ� (�ٸ����� ��ŷ�� ������ ��)
				if (true == removed)
					return false;

				// ������ Add �� ���Ḯ��Ʈ ������ �ٲ� CAS�� ������ ��... �ٽ� �õ�
			}
		}
	}

	bool Contains(int x)
	{
		// Find�� �ٸ��� CAS�� �õ��ϰ� goto�� �ٽ� �õ����� �ʴ´�, WaitFree..?
		SK_LF_NODE* prevs[MAX_TOP + 1];
		SK_LF_NODE* currs[MAX_TOP + 1];

		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {
				prevs[i] = &head;
			}
			else {
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i]->get_ptr();

			while (true)
			{
				bool removed = false;
				SK_LF_NODE* succ = currs[i]->next[i]->get_ptr(&removed);
				while (true == removed) {
					currs[i] = succ;
					succ = currs[i]->next[i]->get_ptr(&removed);
				}

				if (currs[i]->key >= x)
					break;

				prevs[i] = currs[i];
				currs[i] = succ;
			}
		}

		return (currs[0]->key == x);
	}

	void Print20()
	{
		auto p = head.next[0]->get_ptr();
		for (int i = 0; i < 20; ++i) {
			if (p == &tail) break;
			std::cout << p->key << ", ";
			p = p->next[0]->get_ptr();
		}

		std::cout << std::endl;
	}
};

#define MY_SET LF_SK_SET
MY_SET my_set;

constexpr int MAX_THREAD{ 16 };
constexpr int NUM_TEST{ 4000000 };
constexpr int KEY_RANGE{ 1000 };


// ������ ����üũ �ڵ�
class HISTORY {
public:
	int op;
	int i_value;
	bool o_value;
	HISTORY(int o, int i, bool re) : op(o), i_value(i), o_value(re) {}
};

std::array<std::vector<HISTORY>, 16> history;

void worker_check(int num_threads, int thread_id)
{
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

// ��ġ��ũ �ڵ�
void benchmark(const int num_thread)
{
	int key;

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

	// ���� �˻� �κ�
	{
		for (int n = 1; n <= MAX_THREAD; n = n * 2) {
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
			my_set.Clear();
		}
	}

	// ���� ���� ����
	{
		for (int n = 1; n <= MAX_THREAD; n = n * 2) {
			std::vector<std::thread> tv;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < n; ++i) {
				tv.emplace_back(benchmark, n);
			}
			for (auto& th : tv)
				th.join();
			auto end_t = high_resolution_clock::now();
			auto exec_t = end_t - start_t;
			size_t ms = duration_cast<milliseconds>(exec_t).count();
			std::cout << n << " Threads,  " << ms << "ms.";
			my_set.Print20();
			my_set.Clear();
		}

	}
}