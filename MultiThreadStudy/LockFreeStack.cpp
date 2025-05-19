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

constexpr int MAX_SPIN = 1 << 16;
constexpr int MIN_SPIN = 1 << 8;

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
	int current_range;
	int min_range;
	int max_range;

	int current_spin;

	RangePolicy(int max) : current_range(1), min_range(1), max_range(max), current_spin(MIN_SPIN) {}

	void init(int max)
	{
		current_range = 1;
		max_range = max;
		current_spin = MIN_SPIN;
	}

	// 소거에 성공한 상황
	void expand()
	{
		current_range = std::min(current_range * 2, max_range);
		current_spin = std::max(current_spin / 2, MIN_SPIN);
	}

	// 소거에 실패한 상황
	void shrink()
	{
		current_range = std::max(current_range / 2, min_range);
		current_spin = std::min(current_spin * 2, MAX_SPIN);
	}
};

thread_local int thread_id;	// 스레드 ID
thread_local RangePolicy range(2 * now_thread_num);
std::atomic_int g_el_success = 0;

constexpr int EMPTY = -1;	// 비어있음 (collision)

struct LockFreeEliminationStack
{
	Node* volatile top;
	std::vector<ThreadInfoPtr> location;
	std::vector<Integer> collision;

	LockFreeEliminationStack(int th_num = MAX_THREADS) : top(nullptr), location(th_num), collision(2 * th_num, -1) {}

	bool CAS(Node* old_val, Node* new_val)
	{
		return std::atomic_compare_exchange_strong(
			reinterpret_cast<volatile std::atomic_llong*>(&top), 
			reinterpret_cast<long long*>(&old_val), 
			reinterpret_cast<long long>(new_val));
	}

	int GetPosition()
	{
		return (rand() % range.current_range);	// 중앙 집중 방식 (결론적으로)
		/*	복잡만 하고 위와 비슷한 일을 하는 코드이다
		int mid = range.max_range / 2;
		int half_range = range.current_range / 2;
		if (half_range == 0) return mid;							// 범위가 너무 작음 (zero division 방지)
		return (rand() % range.current_range) + (mid - half_range);	// 중앙 집중 분포로 서브 논문의 방식
																	// woudenberg 논문의 11page에서 중앙 집중 분포를 사용한 성능 비교 참고
																	// 메인 논문에도 있었다 (배열의 중앙 집중 방식이지만 Range를 늘려감)
		*/
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
		ThreadInfo* p = new ThreadInfo{ thread_id, 'P', new_node };	// 스레드 정보 저장

		while (true) {
			Node* old_top = top;
			new_node->next = old_top;
			if (true == CAS(old_top, new_node)) {
				return;	// 성공적으로 푸시됨 (중앙 스택에 바로,,, 가장 깔끔한 적은 부하의 상황)
			}
			// 실패한 경우
			
			// 충돌 시도
			ThreadInfo* matched_info = nullptr;
			location[thread_id].ptr = p;
			if (TryEliminate(p, matched_info)) {
				range.expand();
				g_el_success++;
				return;	// 소거 성공
			}
			else {
				range.shrink();	// 범위 축소
			}
		}
	}

	int Pop()
	{
		ThreadInfo* p = new ThreadInfo{ thread_id, 'Q', nullptr };	// 스레드 정보 저장

		while (true) {
			Node* old_top = top;
			if (old_top == nullptr) {
				return EMPTY;	// 비어있음
			}
			Node* new_top = old_top->next;
			if (CAS(old_top, new_top)) {
				int num = old_top->key;
				//delete old_top;	// 노드 삭제
				return num;	// 성공적으로 pop 됨
			}

			ThreadInfo* matched_info = nullptr;
			location[thread_id].ptr = p;
			if (TryEliminate(p, matched_info)) {
				int num = matched_info->node->key;	// p노드가 nullptr임 지역 변수가 사라진 듯 함
				range.expand();
				g_el_success++;
				//delete p.node;
				return num;	
			}
			else {
				range.shrink();	// 범위 축소
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
	bool TryEliminate(ThreadInfo* info, ThreadInfo*& matched_info)
	{
		int pos = GetPosition();
		int expected = EMPTY;	// 처음은 비어 있을 것이라 기대

		int spin_cnt = 0;
		if (collision[pos].CAS(expected, thread_id)) {				// 기다리기
			while (spin_cnt < range.current_spin) {
				_mm_pause();
				// check elimination (성공이면 return)
				if (location[thread_id].ptr->id != thread_id) {
					matched_info = location[thread_id].ptr;			// 스레드 정보 가져오기
					if (matched_info != nullptr && matched_info->op != info->op) {
						return true;								// 소거 성공
					}
					break;
				}
				++spin_cnt;
			}

			if (true == location[thread_id].CAS(info, nullptr)) {	// 소거 타임아웃 실패 (아무도 찾아오지 않음)
				collision[pos].CAS(thread_id, EMPTY);				// 충돌 정보 초기화
				return false;
			}
			else {													// 소거 됨 (그새 누군가 가져감)
				matched_info = location[thread_id].ptr;				// 스레드 정보 가져오기
				location[thread_id].CAS(matched_info, nullptr);
				if (matched_info != nullptr && matched_info->op != info->op) {
					return true;									// 소거 성공
				}
				collision[pos].CAS(thread_id, EMPTY);				// 충돌 정보 초기화
				return false;
			}
		}
		else {
			int other_id = collision[pos].val;						// 타 스레드 번호
			if (other_id == EMPTY) return false;					// 다른 스레드가 없으면 실패
			ThreadInfo* other_info = location[other_id].ptr;		// 스레드 정보 가져오기
			if (other_info != nullptr && other_info->op != info->op) {	// 다른 스레드가 소거를 시도했음
				if (true == collision[pos].CAS(other_id, thread_id)) {
					if (true == location[other_id].CAS(other_info, info)) {	// 소거 성공
						matched_info = other_info;					// 스레드 정보 가져오기
						if (matched_info != nullptr && matched_info->op != info->op) {
							return true;									// 소거 성공
						}
						return false;
					}
				}
			}

			if (true == location[thread_id].CAS(info, nullptr)) {	// 소거 타임아웃 실패 (아무도 찾아오지 않음)
				collision[pos].CAS(thread_id, EMPTY);				// 충돌 정보 초기화
				return false;
			}
			else {													// 소거 됨 (그새 누군가 가져감)
				matched_info = location[thread_id].ptr;				// 스레드 정보 가져오기
				location[thread_id].CAS(matched_info, nullptr);
				if (matched_info != nullptr && matched_info->op != info->op) {
					return true;									// 소거 성공
				}
				collision[pos].CAS(thread_id, EMPTY);				// 충돌 정보 초기화
				return false;
			}
		}
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
