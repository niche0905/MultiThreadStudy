#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>	// ����üũ�� ����
#include <xmmintrin.h>	// _mm_pause() ����� ���� ��� (sleep�� ��� ���)

constexpr int CACHE_LINE_SIZE = 64;		// ĳ�ö��� ũ�� (Cache Thrasing ����)
constexpr int MAX_THREADS = 16;			// �ִ� ������ ��


struct Node 
{
	int key;
	Node* volatile next;

	Node(int k = 0) : key(k), next(nullptr) {}
};

struct ThreadInfo
{
	int id;
	char op;
	Node* node;
	int spin;

	ThreadInfo(int th_id, char now_op, Node* ptr, int init_spin = 0) : id(th_id), op(now_op), node(ptr), spin(init_spin) {}
};

struct alignas(CACHE_LINE_SIZE) ThreadInfoPtr 
{
	ThreadInfo* volatile ptr;

	ThreadInfoPtr() : ptr(nullptr) {}

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

	Integer() : val(0) {}

	bool CAS(int old_val, int new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_int*>(&val),
			reinterpret_cast<int*>(&old_val),
			new_val);
	}
};

int now_thread_num;

struct RangePolicy
{
	int current_range;
	int min_range;
	int max_range;

	RangePolicy(int max) : current_range(1), min_range(1), max_range(max) {}

	void init(int max)
	{
		current_range = 1;
		max_range = max;
	}

	void expand()
	{
		current_range = std::min(current_range * 2, max_range);
	}

	void shrink()
	{
		current_range = std::max(current_range / 2, min_range);
	}
};

thread_local int thread_id;	// ������ ID
thread_local RangePolicy range(2 * now_thread_num);

constexpr int EMPTY = -1;	// ������� (collision)

struct LockFreeEliminationStack
{
	constexpr static int MAX_SPIN = 1 << 20;

	Node* volatile top;
	std::vector<ThreadInfoPtr> location;
	std::vector<Integer> collision;

	LockFreeEliminationStack(int th_num = MAX_THREADS) : top(nullptr), location(th_num), collision(2 * th_num) {}

	bool CAS(Node* old_val, Node* new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top), 
			reinterpret_cast<long long*>(&old_val), 
			reinterpret_cast<long long>(new_val));
	}

	int get_random_pos()
	{
		int base = range.current_range / 2;
		return (rand() % range.current_range) + base;
	}

	void Clear()
	{
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
			if (true == CAS(old_top, new_node)) {
				range.shrink();
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
				if (q != nullptr && q->id == him && q->op == p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// �Ұŵ��� ���� �ƹ��� ��ã�ƿ�
						// ���� ����(�浹) �õ�
						if (location[him].CAS(q, p)) {	// �ٷ� �Ұ� ���� - ���� ������ Ȯ�� ����
							range.expand();
							return;
						}
						else {
							goto PUSH;	// �ٷ� �ٽ� �߾� ���� push �õ�
						}
					}
					else {	// �Ұ� �� (������ ������) - ���� ������ Ȯ�� ����
						range.expand();
						location[thread_id].ptr = nullptr;
						return;
					}
				}
			}

			// back off (���� �ð� ��ٸ�)
			for (int i = 0; i < p->spin; ++i) {
				_mm_pause();
			}
			// spin ������ ����
			if (p->spin < MAX_SPIN) {
				p->spin <<= 1;
			}

			if (location[thread_id].CAS(p, nullptr)) {	// �Ұ� �� (������ ������)
				location[thread_id].ptr = nullptr;
				return;
			}
		}
	}
	
};

