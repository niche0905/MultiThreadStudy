#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>	// 에러체크를 위해
#include <xmmintrin.h>	// _mm_pause() 사용을 위한 헤더 (sleep의 대신 사용)

constexpr int CACHE_LINE_SIZE = 64;		// 캐시라인 크기 (Cache Thrasing 방지)
constexpr int MAX_THREADS = 16;			// 최대 스레드 수


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

thread_local int thread_id;	// 스레드 ID
thread_local RangePolicy range(2 * now_thread_num);

constexpr int EMPTY = -1;	// 비어있음 (collision)

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
		ThreadInfo* p = new ThreadInfo(thread_id, 'P', new_node);	// 스레드 정보 저장

		while (true) {
			PUSH:	// 소거를 시도하였지만 실패한 경우 다시 (왜 back off 하지 않는지 모르겠음)

			Node* old_top = top;
			if (true == CAS(old_top, new_node)) {
				range.shrink();
				return;	// 성공적으로 푸시됨 (중앙 스택에 바로,,, 가장 깔끔한 적은 부하의 상황)
			}
			// 실패한 경우
			// 경쟁이 치열하다고 판단

			if (location[thread_id].ptr == nullptr)
				location[thread_id].ptr = p;
			int pos = get_random_pos();
			int him = collision[pos].val;	// 타 스레드 번호
			while (false == collision[pos].CAS(him, thread_id))	// 경쟁이 치열해서 소거도 실패함
				him = collision[pos].val;
			if (him != EMPTY) {
				ThreadInfo* q = location[him].ptr;	// 스레드 정보 가져오기
				if (q != nullptr && q->id == him && q->op == p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// 소거되지 않음 아무도 안찾아옴
						// 내가 접촉(충돌) 시도
						if (location[him].CAS(q, p)) {	// 바로 소거 성공 - 높은 부하일 확률 높음
							range.expand();
							return;
						}
						else {
							goto PUSH;	// 바로 다시 중앙 스택 push 시도
						}
					}
					else {	// 소거 됨 (누군가 가져감) - 높은 부하일 확률 높음
						range.expand();
						location[thread_id].ptr = nullptr;
						return;
					}
				}
			}

			// back off (일정 시간 기다림)
			for (int i = 0; i < p->spin; ++i) {
				_mm_pause();
			}
			// spin 지수로 증가
			if (p->spin < MAX_SPIN) {
				p->spin <<= 1;
			}

			if (location[thread_id].CAS(p, nullptr)) {	// 소거 됨 (누군가 가져감)
				location[thread_id].ptr = nullptr;
				return;
			}
		}
	}
	
};

