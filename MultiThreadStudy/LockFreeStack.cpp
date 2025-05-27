#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>	// ����üũ�� ����
#include <xmmintrin.h>	// _mm_pause() ����� ���� ��� (sleep�� ��� ���)
#include <unordered_set>

#define TIME_CHECK

constexpr int CACHE_LINE_SIZE = 64;		// ĳ�ö��� ũ�� (Cache Thrasing ����)
constexpr int MAX_THREADS = 16;			// �ִ� ������ ��

const int NUM_TEST = 10000000;

constexpr int MAX_SPIN = 1 << 16;
constexpr int MIN_SPIN = 1 << 8;

constexpr int EMPTY = -1;	// ������� (����, collision)

constexpr int POP_EMPTY = -1;
constexpr int POP = -2;
constexpr int TIMEOUT = -3;
constexpr int POP_FALSE = -4;

struct Node 
{
	int key;
	Node* volatile next;

	Node(int k = 0) : key(k), next(nullptr) {}
};

// �⺻ LockFree Stack (Treiber ���)
class LockFreeStack 
{
	Node* volatile top;
	bool CAS(Node* old_p, Node* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	LockFreeStack()
	{
		top = nullptr;
	}
	void SetCapacity()
	{
	}
	void SetRangePolicy()
	{
	}
	void Clear()
	{
		while (EMPTY != Pop());
	}
	void Push(int x)
	{
		Node* node = new Node{ x };
		while (true) {
			Node* last = top;
			node->next = last;
			if (true == CAS(last, node))
				return;
		}
	}
	int Pop()
	{
		while (true) {
			Node* volatile last = top;
			if (nullptr == last)
				return EMPTY;
			Node* volatile next = last->next;
			if (last != top) continue;
			int value = last->key;
			if (true == CAS(last, next)) {
				// delete last;
				return value;
			}
		}
	}
	void Print20()
	{
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

// �ܼ� BackOff (_mm_pause ���)
class BackOff
{
	int min_delay, max_delay;
	int now_delay;

public:
	BackOff(int min, int max)
		: min_delay{ min }, max_delay{ max }, now_delay{ min_delay }
	{}

	void Delay()
	{
		int half_delay = (now_delay / 2);
		if (half_delay == 0) half_delay = 1;
		int delay = (rand() % half_delay) + half_delay;

		for (int i = 0; i < delay; ++i) {
			_mm_pause();
		}
	}

	void Increment()
	{
		now_delay = std::min(now_delay * 2, max_delay);
	}

	void Decrement()
	{
		now_delay = std::max(now_delay / 2, min_delay);
	}
};
thread_local BackOff back_off{ MIN_SPIN, MAX_SPIN };
class LockFreeBackOffStack
{
	Node* volatile top;
	bool CAS(Node* old_p, Node* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	LockFreeBackOffStack()
	{
		top = nullptr;
	}

	void SetCapacity()
	{
	}
	void SetRangePolicy()
	{
	}
	void Clear()
	{
		while (EMPTY != Pop());
	}
	void Push(int x)
	{
		Node* node = new Node{ x };
		while (true) {
			Node* last = top;
			node->next = last;
			if (true == CAS(last, node)) {
				back_off.Decrement();
				return;
			}
			back_off.Delay();
			back_off.Increment();
		}
	}
	int Pop()
	{
		while (true) {
			Node* volatile last = top;
			if (nullptr == last)
				return EMPTY;
			Node* volatile next = last->next;
			if (last != top) continue;
			int value = last->key;
			if (true == CAS(last, next)) {
				back_off.Decrement();
				// delete last;
				return value;
			}
			back_off.Delay();
			back_off.Increment();
		}
	}
	void Print20()
	{
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

// LF �Ұ� ��� (������ �ڵ�) <- �ȵ��ư�
struct ThreadInfo
{
	int id;
	char op;
	Node* node;

	ThreadInfo(int th_id, char now_op, Node* ptr) : id(th_id), op(now_op), node(ptr) {}
};
struct alignas(CACHE_LINE_SIZE) ThreadInfoPtr 
{
	ThreadInfo* volatile ptr;

	ThreadInfoPtr() : ptr(nullptr) {}
	ThreadInfoPtr(ThreadInfo* p) : ptr(p) {}

	bool CAS(ThreadInfo* old_ptr, ThreadInfo* new_ptr)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&ptr),
			reinterpret_cast<long long*>(&old_ptr),
			reinterpret_cast<long long>(new_ptr));
	}
};
struct alignas(CACHE_LINE_SIZE) Integer
{
	int volatile val;

	Integer() : val(-1) {}
	Integer(int v) : val(v) {}

	bool CAS(int old_val, int new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_int*>(&val),
			reinterpret_cast<int*>(&old_val),
			new_val);
	}
};
volatile int now_thread_num;
struct RangePolicy
{
	static constexpr int R_MAX_SPIN = 1 << 8;
	static constexpr int R_MIN_SPIN = 1 << 4;

	int range;
	int min_range;
	int max_range;

	int current_spin;

	RangePolicy(int max) : range(1), min_range(1), max_range(max), current_spin(MIN_SPIN) {}

	void init(int max)
	{
		range = 1;
		max_range = max;
		current_spin = MIN_SPIN;
	}

	// �Ұſ� ������ ��Ȳ
	void expand()
	{
		range = std::min(range * 2, max_range);
		current_spin = std::max(current_spin / 2, R_MIN_SPIN);
	}

	// �Ұſ� ������ ��Ȳ
	void shrink()
	{
		range = std::max(range / 2, min_range);
		current_spin = std::min(current_spin * 2, R_MAX_SPIN);
	}
};
thread_local int thread_id;	// ������ ID
thread_local RangePolicy origin_range(MAX_THREADS);
std::atomic_int g_el_success = 0;
struct LockFreeEliminationStack
{

	int capacity;

	Node* volatile top;
	std::vector<ThreadInfoPtr> location;
	std::vector<Integer> collision;

	LockFreeEliminationStack(int th_num = MAX_THREADS) : capacity{ th_num }, top(nullptr), location(th_num), collision(th_num) {}
	
	void SetCapacity()
	{
		capacity = now_thread_num / 2;
		location.resize(now_thread_num);
		collision.resize(capacity);
	}

	void SetRangePolicy()
	{
		origin_range.init(capacity);
	}

	bool CAS(Node* old_val, Node* new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top),
			reinterpret_cast<long long*>(&old_val),
			reinterpret_cast<long long>(new_val));
	}

	int get_random_pos()
	{
		return (rand() % origin_range.range);
	}

	void Clear()
	{
		g_el_success = 0;
		while (top != nullptr) {
			Node* temp = top;
			top = top->next;
			delete temp;
		}
	}

	void Push(int num)
	{
		Node* new_node = new Node(num);
		ThreadInfo* p = new ThreadInfo(thread_id, 'P', new_node);	// ������ ���� ����

		while (true) {
		PUSH:	// �ҰŸ� �õ��Ͽ����� ������ ��� �ٽ� (�� back off ���� �ʴ��� �𸣰���)

			Node* old_top = top;
			new_node->next = old_top;
			if (true == CAS(old_top, new_node)) {
				return;	// ���������� Ǫ�õ� (�߾� ���ÿ� �ٷ�,,, ���� ����� ���� ������ ��Ȳ)
			}
			// ������ ���
			// ������ ġ���ϴٰ� �Ǵ�

			if (location[thread_id].ptr == nullptr)
				location[thread_id].ptr = p;
			int pos = get_random_pos();
			int him = collision[pos].val;	// Ÿ ������ ��ȣ
			while (false == collision[pos].CAS(him, thread_id))	// ������ ġ���ؼ� �Ұŵ� ������
				him = collision[pos].val;
			if (him != EMPTY) {
				ThreadInfo* q = location[him].ptr;	// ������ ���� ��������
				if (q != nullptr && q->id == him && q->op != p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// �Ұŵ��� ���� �ƹ��� ��ã�ƿ�
						// ���� ����(�浹) �õ�
						if (location[him].CAS(q, p)) {	// �ٷ� �Ұ� ���� - ���� ������ Ȯ�� ����
							origin_range.expand();
							g_el_success++;
							return;
						}
						else {
							goto PUSH;	// �ٷ� �ٽ� �߾� ���� push �õ�
						}
					}
					else {	// �Ұ� �� (������ ������) - ���� ������ Ȯ�� ����
						origin_range.expand();
						location[thread_id].ptr = nullptr;
						return;
					}
				}
			}

			// back off (���� �ð� ��ٸ�)
			for (int i = 0; i < origin_range.current_spin; ++i) {
				_mm_pause();
			}
			// spin ������ ����
			if (origin_range.current_spin < MAX_SPIN) {
				origin_range.current_spin <<= 1;
			}

			if (false == location[thread_id].CAS(p, nullptr)) {	// �Ұ� �� (������ ������)
				location[thread_id].ptr = nullptr;
				return;
			}

			origin_range.shrink();
		}
	}

	int Pop()
	{
		ThreadInfo* p = new ThreadInfo(thread_id, 'Q', nullptr);	// ������ ���� ����

		while (true) {
		POP:

			Node* old_top = top;
			if (old_top == nullptr) {
				return EMPTY;	// �������
			}
			Node* new_top = old_top->next;
			if (CAS(old_top, new_top)) {
				int num = old_top->key;
				//delete old_top;	// ��� ����
				return num;	// ���������� pop ��
			}

			if (location[thread_id].ptr == nullptr)
				location[thread_id].ptr = p;
			int pos = get_random_pos();
			int him = collision[pos].val;	// Ÿ ������ ��ȣ
			while (false == collision[pos].CAS(him, thread_id))	// ������ ġ���ؼ� �Ұŵ� ������
				him = collision[pos].val;
			if (him != EMPTY) {
				ThreadInfo* q = location[him].ptr;	// ������ ���� ��������
				if (q != nullptr && q->id == him && q->op != p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// �Ұŵ��� ���� �ƹ��� ��ã�ƿ�
						// ���� ����(�浹) �õ�
						if (location[him].CAS(q, p)) {	// �ٷ� �Ұ� ���� - ���� ������ Ȯ�� ����
							int num = q->node->key;
							origin_range.expand();
							return num;
						}
						else {
							goto POP;	// �ٷ� �ٽ� �߾� ���� push �õ�
						}
					}
					else {	// �Ұ� �� (������ ������) - ���� ������ Ȯ�� ����
						int num = location[thread_id].ptr->node->key;
						origin_range.expand();
						location[thread_id].ptr = nullptr;
						return num;
					}
				}
			}

			// back off (���� �ð� ��ٸ�)
			for (int i = 0; i < origin_range.current_spin; ++i) {
				_mm_pause();
			}
			// spin ������ ����
			if (origin_range.current_spin < MAX_SPIN) {
				origin_range.current_spin <<= 1;
			}

			if (false == location[thread_id].CAS(p, nullptr)) {	// �Ұ� �� (������ ������)
				int num = location[thread_id].ptr->node->key;
				location[thread_id].ptr = nullptr;
				return num;
			}

			origin_range.shrink();
		}
	}

	void Print20()
	{
		std::cout << "Num Eliminations = " << g_el_success << ",   ";
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (p == nullptr) break;
			std::cout << p->key << " ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};
struct LockFreeExchager
{
	enum Status : int
	{
		EMPTY = 0,			// �������
		WAIT = 1,			// ��ٸ�����
		BUSY = 2			// ��ȯ �Ϸ� (ó����?)
	};
	struct Slot
	{
		volatile long long slot = 0;

		Slot()
			: slot{ 0 }
		{

		}
		Slot(int value, int state)
		{
			slot = (static_cast<long long>(value) << 32) | (state);
		}

		void set_slot()
		{
			slot = 0;
		}

		int get_slot(int& status)
		{
			long long now_value = slot;
			status = static_cast<int>(now_value & 0xFFFFFFFF);
			return static_cast<int>((now_value >> 32) & 0xFFFFFFFF);
		}

		int get_state()
		{
			long long now_value = slot;
			return static_cast<int>(now_value & 0xFFFFFFFF);
		}

		bool CAS(int old_val, int new_val, int old_st, int new_st)
		{
			long long old_value = (static_cast<long long>(old_val) << 32) | (old_st);
			long long new_value = (static_cast<long long>(new_val) << 32) | (new_st);
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_value, new_value
			);
		}

		bool CAS(long long old_slot, long long new_slot)
		{
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_slot, new_slot
			);
		}

		bool CAS(Slot old_slot, Slot new_slot)
		{

			long long old_value = old_slot.slot;
			long long new_value = new_slot.slot;
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_value, new_value
			);
		}
	};

	Slot slot;

public:
	LockFreeExchager() : slot() {}

	int exchange(int myItem, const int spin)
	{
		const int status = WAIT;
		int temp;

		int spin_count = 0;
		while (true) {
			Slot old_slot = slot;

			if (spin_count >= spin)
				return TIMEOUT;

			int now_state = old_slot.get_state();
			switch (now_state)
			{
			case EMPTY:
			{
				Slot new_slot{ myItem, status };

				if (slot.CAS(old_slot, new_slot)) {
					while (spin_count < spin) {
						old_slot = slot;
						if (old_slot.get_state() == BUSY) {
							slot.set_slot();

							return old_slot.get_slot(temp);
						}
						++spin_count;	// spin ����
					}
					if (slot.CAS(new_slot, Slot{}))
						return TIMEOUT;
					else {
						old_slot = slot;
						slot.set_slot();

						return old_slot.get_slot(temp);
					}
				}
			}
			break;
			case WAIT:
			{
				Slot new_slot{ myItem, BUSY };
				if (slot.CAS(old_slot, new_slot))
					return old_slot.get_slot(temp);
			}
			break;
			case BUSY:
			{
				// nothing
			}
			break;
			}

			++spin_count;	// spin ����
		}
	}
};
struct EliminationArray
{
	std::vector<LockFreeExchager> exchager;

public:
	EliminationArray(int capacity)
		: exchager(capacity)
	{
	}

	void SetCapacity(int capacity)
		// thread Ȱ�� ������ �޶����� ���� �ʿ��� ������ ���
	{
		exchager.resize(capacity);
	}

	int visit(int value, int range)
	{
		int slot = rand() % range;	// range�� capacity ���Ͽ��� �� ��
		return (exchager[slot].exchange(value, origin_range.current_spin));
	}

};
struct CorrectEliminationStack
{

	int capacity;

	Node* volatile top;
	EliminationArray elimination_array;

	CorrectEliminationStack(int th_num = MAX_THREADS) : capacity{ th_num }, top(nullptr), elimination_array(th_num) {}

	void SetCapacity()
	{
		capacity = now_thread_num / 2;
		elimination_array.SetCapacity(capacity);
	}

	void SetRangePolicy()
	{
		origin_range.init(capacity);
	}

	bool CAS(Node* old_val, Node* new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top),
			reinterpret_cast<long long*>(&old_val),
			reinterpret_cast<long long>(new_val));
	}

	void Clear()
	{
		g_el_success = 0;
		while (top != nullptr) {
			Node* temp = top;
			top = top->next;
			delete temp;
		}
	}

	void Push(int num)
	{
		Node* node = new Node{ num };

		while (true) {
			if (TryPush(node)) {
				return;
			}
			else {
				if (POP == elimination_array.visit(num, origin_range.range)) {
					++g_el_success;
					origin_range.expand();
					return;
				}
				else {
					origin_range.shrink();
				}
			}

		}
	}

	int Pop()
	{
		while (true) {
			int value = TryPop();
			if (POP_FALSE != value)	// POP_EMPTY Ȥ��, ����� �� ���̸� return
				return value;
			else {
				int elimination_ret = elimination_array.visit(POP, origin_range.range);
				if (TIMEOUT != elimination_ret and POP != elimination_ret) {
					origin_range.expand();
					return elimination_ret;
				}
				else {
					origin_range.shrink();
				}
			}
		}
	}

	bool TryPush(Node* new_node)
	{
		Node* old_top = top;
		new_node->next = old_top;
		return CAS(old_top, new_node);
	}
	int TryPop()
	{
		Node* old_top = top;
		if (nullptr == old_top)
			return POP_EMPTY;

		int value = old_top->key;
		Node* new_top = old_top->next;
		if (true == CAS(old_top, new_top)) {
			return value;
		}
		return POP_FALSE;
	}

	void Print20()
	{
		std::cout << "Num Eliminations = " << g_el_success << ",   ";
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (p == nullptr) break;
			std::cout << p->key << " ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

// Improve �Ұ� ��� (�ֱ� �� 2021)
class ImprovedLockFreeExchanger
{
	enum Status : int
	{
		EMPTY = 0,			// �������
		WAITINGPOPPER = 1,	// POP�� ��ٸ�����
		WAITINGPUSHER = 2,	// PUSH�� ��ٸ�����
		BUSY = 3			// ��ȯ �Ϸ� (ó����?)
	};

	struct Slot
	{
		volatile long long slot = 0;
		
		Slot()
			: slot{ 0 }
		{

		}
		Slot(int value, int state)
		{
			slot = (static_cast<long long>(value) << 32) | (state);
		}

		void set_slot()
		{
			slot = 0;
		}

		int get_slot(int& status)
		{
			long long now_value = slot;
			status = static_cast<int>(now_value & 0xFFFFFFFF);
			return static_cast<int>((now_value >> 32) & 0xFFFFFFFF);
		}

		int get_state()
		{
			long long now_value = slot;
			return static_cast<int>(now_value & 0xFFFFFFFF);
		}

		bool CAS(int old_val, int new_val, int old_st, int new_st)
		{
			long long old_value = (static_cast<long long>(old_val) << 32) | (old_st);
			long long new_value = (static_cast<long long>(new_val) << 32) | (new_st);
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_value, new_value
			);
		}

		bool CAS(long long old_slot, long long new_slot)
		{
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_slot, new_slot
			);
		}

		bool CAS(Slot old_slot, Slot new_slot)
		{

			long long old_value = old_slot.slot;
			long long new_value = new_slot.slot;
			return std::atomic_compare_exchange_strong(
				reinterpret_cast<volatile std::atomic_llong*>(&slot),
				&old_value, new_value
			);
		}
	};

	Slot slot;

public:
	ImprovedLockFreeExchanger() : slot() {}

	int exchange(int myItem, std::chrono::nanoseconds timeout)
	{
		using Clock = std::chrono::steady_clock;
		const auto deadline = Clock::now() + timeout;
		const int status = (myItem == POP) ? WAITINGPOPPER : WAITINGPUSHER;
		int temp;

		while (true) {
			Slot old_slot = slot;

			if (Clock::now() >= deadline)
				return TIMEOUT;

			int now_state = old_slot.get_state();
			switch (now_state)
			{
			case EMPTY:
			{
				Slot new_slot{ myItem, status };

				if (slot.CAS(old_slot, new_slot)) {
					while (Clock::now() < deadline) {
						old_slot = slot;
						if (old_slot.get_state() == BUSY) {
							slot.set_slot();

							return old_slot.get_slot(temp);
						}
					}
					if (slot.CAS(new_slot, Slot{}))
						return TIMEOUT;
					else {
						old_slot = slot;
						slot.set_slot();

						return old_slot.get_slot(temp);
					}
				}
			}
			break;
			case WAITINGPOPPER:
			{
				if (status != WAITINGPOPPER) {
					Slot new_slot{ myItem, BUSY };
					if (slot.CAS(old_slot, new_slot))
						return old_slot.get_slot(temp);
				}
			}
			break;
			case WAITINGPUSHER:
			{
				if (status != WAITINGPUSHER) {
					Slot new_slot{ myItem, BUSY };
					if (slot.CAS(old_slot, new_slot))
						return old_slot.get_slot(temp);
				}
			}
			break;
			case BUSY:
			{
				// nothing
			}
			break;
			}
		}
	}

};
class ImprovedEliminationArray
{
	// ������ 1ms�� ��ٸ� ������ ������ (�ʹ� ���� �ʳ�?)
	static constexpr std::chrono::nanoseconds duration{ 1000 };	// 1ms ������ 10ns

	std::vector<ImprovedLockFreeExchanger> exchager;

public:
	ImprovedEliminationArray(int capacity)
		: exchager(capacity)
	{
		// Random...?
	}

	void SetCapacity(int capacity)
		// thread Ȱ�� ������ �޶����� ���� �ʿ��� ������ ���
	{
		exchager.resize(capacity);
	}
	
	int visit(int value, int range)
	{
		int slot = rand() % range;	// range�� capacity ���Ͽ��� �� ��
		return (exchager[slot].exchange(value, duration));
	}
};
struct ImprovedRangePolicy
{
	int range;
	int success_count;
	int timeout_count;
	int max_range;

	ImprovedRangePolicy(int max)
		: max_range(max), range(max), success_count(0), timeout_count(0) {}

	void init(int capacity)
		// thread Ȱ�� ������ �޶����� ���� �ʿ��� ������ ���
	{
		max_range = capacity;
		range = capacity;
		success_count = 0;
		timeout_count = 0;
	}

	void expand()
	{
		++success_count;
		if (success_count > 5) {
			success_count = 0;	// reset
			range = std::min(range + 1, max_range);	// ���� Ȯ��
		}
	}
	void shrink()
	{
		++timeout_count;
		if (timeout_count > 10) {
			timeout_count = 0;	// reset
			range = std::max(range - 1, 1);	// ���� ���
		}
	}
};
thread_local ImprovedRangePolicy range(MAX_THREADS);
class ImprovedEliminationBackoffStack
{
	int capacity;	// ���� EliminationArray�� capacity

	Node* volatile top;
	ImprovedEliminationArray elimination_array;

	bool CAS(Node* old_p, Node* new_p)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top),
			reinterpret_cast<long long*>(&old_p),
			reinterpret_cast<long long>(new_p));
	}
public:
	ImprovedEliminationBackoffStack()
		: capacity{ now_thread_num / 2 }
		, elimination_array(capacity)
	{
		top = nullptr;
	}
	void SetCapacity()
		// Clear �� Benchmark ���� ���� �θ� ��
	{
		capacity = now_thread_num / 2;
		elimination_array.SetCapacity(capacity);
		// .. RangePolicy�� �ؾ��� �׳� ��(�ܺ�)���� �ϴ°�;
	}
	void SetRangePolicy()
		// ��� �����忡�� ���� ���� �θ� ��
	{
		range.init(capacity);
	}
	void Clear()
	{
		g_el_success = 0;
		while (EMPTY != Pop());
	}
	void Push(int x)
	{
		Node* node = new Node{ x };

		while (true) {
			if (TryPush(node)) {
				return;
			}
			else {
				if (TIMEOUT != elimination_array.visit(x, range.range)) {
					++g_el_success;
					range.expand();
					return;
				}
				else {
					range.shrink();
				}
			}

		}
	}
	int Pop()
	{
		while (true) {
			int value = TryPop();
			if (POP_FALSE != value)	// POP_EMPTY Ȥ��, ����� �� ���̸� return
				return value;
			else {
				int elimination_ret = elimination_array.visit(POP, range.range);
				if (TIMEOUT != elimination_ret) {
					range.expand();
					return elimination_ret;
				}
				else {
					range.shrink();
				}
			}
		}
	}
	bool TryPush(Node* new_node)
	{
		Node* old_top = top;
		new_node->next = old_top;
		return CAS(old_top, new_node);
	}
	int TryPop()
	{
		Node* old_top = top;
		if (nullptr == old_top)
			return POP_EMPTY;

		int value = old_top->key;
		Node* new_top = old_top->next;
		if (true == CAS(old_top, new_top)) {
			return value;
		}
		return POP_FALSE;
	}
	void Print20()
	{
		std::cout << "Num Eliminations = " << g_el_success << ",   ";
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (nullptr == p) break;
			std::cout << p->key << ", ";
			p = p->next;
		}
		std::cout << std::endl;
	}
};

ImprovedEliminationBackoffStack stack;

#ifdef TIME_CHECK
struct alignas(CACHE_LINE_SIZE) TimeData
{
	std::vector<long long> times;
};

struct TimeChecker
{
	std::vector<TimeData> times_array;

	TimeChecker()
	{
	}

	void Clear()
	{
		times_array.clear();
	}

	void Setter()
	{
		int thread_num = now_thread_num;
		times_array.resize(thread_num);
		for (auto& times : times_array) {
			times.times.reserve(NUM_TEST / thread_num);
		}
	}

	long long GetMaxTime()
	{
		long long max_time = 0;
		for (auto& times : times_array) {
			if (times.times.empty()) continue;
			auto max_iter = std::max_element(times.times.begin(), times.times.end());
			if (*max_iter > max_time) {
				max_time = *max_iter;
			}
		}
		return max_time;
	}

	long long GetAverageTime()
	{
		long long total_time = 0;
		int count = 0;
		for (auto& times : times_array) {
			for (auto time : times.times) {
				total_time += time;
				++count;
			}
		}
		if (count == 0) return 0;
		return total_time / count;
	}
};

TimeChecker time_checker;
#endif

void benchmark(const int th_id)
{
	thread_id = th_id;

	stack.SetRangePolicy();

	int key = 0;
	int loop_count = NUM_TEST / now_thread_num;

	for (auto i = 0; i < loop_count; ++i) {
#ifdef TIME_CHECK
		auto start_t = std::chrono::high_resolution_clock::now();
#endif
		if ((i < 32) || ((rand() % 2) == 0)) {
			stack.Push(key++);
		}
		else {
			stack.Pop();
		}
#ifdef TIME_CHECK
		auto end_t = std::chrono::high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(exec_t).count();
		time_checker.times_array[th_id].times.push_back(ns);
#endif
	}
}

struct HISTORY {
	std::vector <int> push_values, pop_values;
};
std::atomic_int stack_size;

void check_history(std::vector <HISTORY>& h)
{
	std::unordered_multiset <int> pushed, poped, in_stack;

	for (auto& v : h)
	{
		for (auto num : v.push_values) pushed.insert(num);
		for (auto num : v.pop_values) poped.insert(num);
		while (true) {
			int num = stack.Pop();
			if (num == -1) break;
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
	std::multiset <int> sorted;
	for (auto num : poped) {
		if (-1 == num) continue;
		if (pushed.count(num) == 0) {
			sorted.insert(num);
		}
	}
	if (false == sorted.empty()) {
		std::cout << "There were elements in the STACK no one pushed : ";
		int count = 20;
		for (auto num : sorted) {
			if (0 == count--) break;
			std::cout << num << ", ";
		}
		std::cout << std::endl;
		exit(-1);
	}
	std::cout << "NO ERROR detectd.\n";
}

void benchmark_test(const int th_id, const int num_threads, HISTORY& h)
{
	thread_id = th_id;

	stack.SetRangePolicy();

	int loop_count = NUM_TEST / num_threads;
	for (int i = 0; i < loop_count; i++) {
		if ((rand() % 2) || i < 128 / num_threads) {
			h.push_values.push_back(i);
			stack_size++;
			stack.Push(i);
		}
		else {
			volatile int curr_size = stack_size--;
			int res = stack.Pop();
			if (res == EMPTY) {
				stack_size++;
				if ((curr_size > num_threads * 2) && (stack_size > num_threads)) {
					std::cout << "ERROR Non_Empty Stack Returned NULL\n";
					exit(-1);
				}
			}
			else h.pop_values.push_back(res);
		}
	}
}

int main()
{
	using namespace std::chrono;

	/*
	for (int n = 1; n <= MAX_THREADS; n = n * 2) {
		stack.Clear();
		now_thread_num = n;
		stack.SetCapacity();
		std::vector<std::thread> tv;
		std::vector<HISTORY> history;
		history.resize(n);
		stack_size = 0;
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
		stack.Print20();
		check_history(history);
	}
	*/


	for (int n = 1; n <= MAX_THREADS; n = n * 2) {
		stack.Clear();
		now_thread_num = n;
		stack.SetCapacity();
#ifdef TIME_CHECK
		time_checker.Clear();
		time_checker.Setter();
#endif
		std::vector<std::thread> tv;
		tv.reserve(n);
		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < n; ++i) {
			tv.emplace_back(benchmark, i);
		}
		for (auto& th : tv)
			th.join();
		auto end_t = high_resolution_clock::now();
		auto exec_t = end_t - start_t;
		size_t ms = duration_cast<milliseconds>(exec_t).count();
		std::cout << n << " Threads,  " << ms << "ms. ----";
		stack.Print20();
#ifdef TIME_CHECK
		long long max_time = time_checker.GetMaxTime();
		long long avg_time = time_checker.GetAverageTime();
		std::cout << " Max Time = " << max_time << ",   Avg Time = " << avg_time << std::endl;
#endif
	}
}
