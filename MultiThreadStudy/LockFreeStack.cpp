#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>	// 에러체크를 위해
#include <xmmintrin.h>	// _mm_pause() 사용을 위한 헤더 (sleep의 대신 사용)
#include <unordered_set>

constexpr int CACHE_LINE_SIZE = 64;		// 캐시라인 크기 (Cache Thrasing 방지)
constexpr int MAX_THREADS = 16;			// 최대 스레드 수

const int NUM_TEST = 10000000;

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

volatile int now_thread_num;

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

	// TODO : RangePolicy를 스레드 로컬 정책으로 병목 현상을 해결해야 함
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
std::atomic_int g_el_success = 0;

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

	int GetPosition()
	{
		//return (rand() % range.current_range);	// 균등 분포로 메인 논문의 방식
		int now_range = range.current_range;
		return (rand() % (now_range / 2)) + (now_range / 4);	// 중앙 집중 분포로 메인 논문의 방식
																// woudenberg 논문의 11page에서 중앙 집중 분포를 사용한 성능 비교 참고
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
		ThreadInfo p{ thread_id, 'P', new_node };	// 스레드 정보 저장

		while (true) {
			PUSH:	// 소거를 시도하였지만 실패한 경우 다시 (왜 back off 하지 않는지 모르겠음)

			Node* old_top = top;
			new_node->next = old_top;
			if (true == CAS(old_top, new_node)) {
				return;	// 성공적으로 푸시됨 (중앙 스택에 바로,,, 가장 깔끔한 적은 부하의 상황)
			}
			// 실패한 경우
			
			// 충돌 시도
			location[thread_id].ptr = &p;
			int pos = GetPosition();
			int him = collision[pos].val;	// 타 스레드 번호
			//bool collision_succeed try_eliminate(pos, him, p);

			// 범위 조정
			/*if (collision_succeed) {
				range.expand();
			}
			else {
				range.shrink();
			}*/

			/* 기존 코드
			while (false == collision[pos].CAS(him, thread_id))	// 경쟁이 치열해서 소거도 실패함
				him = collision[pos].val;
			if (him != EMPTY) {
				ThreadInfo* q = location[him].ptr;	// 스레드 정보 가져오기
				if (q != nullptr && q->id == him && q->op != p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// 소거되지 않음 아무도 안찾아옴
						// 내가 접촉(충돌) 시도
						if (location[him].CAS(q, p)) {	// 바로 소거 성공 - 높은 부하일 확률 높음
							range.expand();
							g_el_success++;
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

			if (false == location[thread_id].CAS(p, nullptr)) {	// 소거 됨 (누군가 가져감)
				location[thread_id].ptr = nullptr;
				return;
			}
			*/
		}
	}

	int Pop()
	{
		ThreadInfo* p = new ThreadInfo(thread_id, 'Q', nullptr);	// 스레드 정보 저장

		while (true) {
			POP:

			Node* old_top = top;
			if (old_top == nullptr) {
				return EMPTY;	// 비어있음
			}
			Node* new_top = old_top->next;
			if (CAS(old_top, new_top)) {
				range.shrink();
				int num = old_top->key;
				//delete old_top;	// 노드 삭제
				return num;	// 성공적으로 pop 됨
			}

			if (location[thread_id].ptr == nullptr)
				location[thread_id].ptr = p;
			int pos = GetPosition();
			int him = collision[pos].val;	// 타 스레드 번호
			while (false == collision[pos].CAS(him, thread_id))	// 경쟁이 치열해서 소거도 실패함
				him = collision[pos].val;
			if (him != EMPTY) {
				ThreadInfo* q = location[him].ptr;	// 스레드 정보 가져오기
				if (q != nullptr && q->id == him && q->op != p->op) {
					if (true == location[thread_id].CAS(p, nullptr)) {	// 소거되지 않음 아무도 안찾아옴
						// 내가 접촉(충돌) 시도
						if (location[him].CAS(q, p)) {	// 바로 소거 성공 - 높은 부하일 확률 높음
							int num = q->node->key;
							range.expand();
							return num;
						}
						else {
							goto POP;	// 바로 다시 중앙 스택 push 시도
						}
					}
					else {	// 소거 됨 (누군가 가져감) - 높은 부하일 확률 높음
						int num = location[thread_id].ptr->node->key;
						range.expand();
						location[thread_id].ptr = nullptr;
						return num;
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

			if (false == location[thread_id].CAS(p, nullptr)) {	// 소거 됨 (누군가 가져감)
				int num = location[thread_id].ptr->node->key;
				location[thread_id].ptr = nullptr;
				return num;
			}
		}
	}
	
	void Print20()
	{
		Node* p = top;
		for (int i = 0; i < 20; ++i) {
			if (p == nullptr) break;
			std::cout << p->key << " ";
			p = p->next;
		}
		std::cout << std::endl;
	}

private:
	bool TryEliminate()
	{
		// TODO : TryEliminate 구현
		// 1. collision 시도 (충돌 시도) <- 바꿀 수 있으면 바꾸고 / 빈자리라면 들어가서 대기

	}

};

LockFreeEliminationStack stack;

void benchmark(const int th_id)
{
	thread_id = th_id;

	range.init(2 * now_thread_num);

	int key = 0;
	int loop_count = NUM_TEST / now_thread_num;

	for (auto i = 0; i < loop_count; ++i) {
		if ((i < 32) || ((rand() % 2) == 0)) {
			stack.Push(key++);
		}
		else {
			stack.Pop();
		}
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

	range.init(2 * num_threads);

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
			if (res == -2) {
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

	for (int n = 1; n <= MAX_THREADS; n = n * 2) {
		stack.Clear();
		std::vector<std::thread> tv;
		std::vector<HISTORY> history;
		history.resize(n);
		stack_size = 0;
		g_el_success = 0;
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
		std::cout << "Num Eliminations = " << g_el_success << ",   ";
		stack.Print20();
		check_history(history);
	}



	for (int n = 1; n <= MAX_THREADS; n = n * 2) {
		stack.Clear();
		now_thread_num = n;
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
	}
}
