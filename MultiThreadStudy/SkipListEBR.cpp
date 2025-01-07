/*--------------------------------------------------------------------------------------------------
	Skip List에 EBR을 적용하는 것 (LF Skip List와 EBR Set을 참고)

	Notion : https://delicate-magic-460.notion.site/EBR-Skip-List-16b893a56e588084bd8bffd50e725921
--------------------------------------------------------------------------------------------------*/

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <array>
#include <queue>
#include <numeric>
#include <atomic>


constexpr int MAX_TOP{ 9 };

constexpr int MAX_THREAD{ 16 };
constexpr int NUM_TEST{ 4000000 };
constexpr int KEY_RANGE{ 1000 };

class SK_LF_NODE;		// SkipList LockFree Node 전방 선언

class SK_SPTR			// SkipList 합성 포인터 (LockFree(CAS)를 위한)
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
		*removed = (1 == (p & 1));	// p의 최하위 1비트로 removed를 판단
		return reinterpret_cast<SK_LF_NODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}

	bool CAS(SK_LF_NODE* old_p, SK_LF_NODE* new_p, bool old_m, bool new_m)
	{
		long long old_v = reinterpret_cast<long long>(old_p);
		if (true == old_m) old_v = old_v | 1;
		else old_v = old_v & 0xFFFFFFFFFFFFFFFE;	// 굳이 필요한 작업인지는 잘 모르겠습니다

		long long new_v = reinterpret_cast<long long>(new_p);
		if (true == new_m) new_v = new_v | 1;
		else new_v = new_v & 0xFFFFFFFFFFFFFFFE;

		return std::atomic_compare_exchange_strong(&sptr, &old_v, new_v);
		// &sptr 위치에 있는 값을 old_v와 비교 후 같다면 new_v로 달라서 바꾸지 못했으면 false를 반환 후 &sptr에 있던 값은 old_v로 확인 가능
	}
};

class SK_LF_NODE		// SkipList LockFree Node 
{
public:
	int key;
	SK_SPTR next[MAX_TOP + 1] = {};	// 0층부터 9층(MAX_TOP)까지 있음
	int top_level;	// 현재 노드의 최상층 (지름길 존재하는 층)

public:
	SK_LF_NODE(int x, int top) : key{ x }, top_level{ top }
	{
	}
	
	~SK_LF_NODE()
	{
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
			head.next[i].set_ptr(&tail);
		}
	}

	void Clear()
	{
		// 0층으로 순회하면서 삭제
		while (head.next[0].get_ptr() != &tail) {
			auto p = head.next[0].get_ptr();
			head.next[0].set_ptr(head.next[0].get_ptr()->next[0].get_ptr());
			delete p;
		}

		// 모두 삭제 후 head의 next는 tail로 설정
		Init();
	}

	bool Find(int x, SK_LF_NODE* prevs[], SK_LF_NODE* currs[])
	{
	retry:

		// 최상층 (지름길 부터 탐색)
		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {		// 최상층 인 경우 받아올 상위 층이 없어서 조건문
				prevs[i] = &head;
			}
			else {		// 최상층이 아니면 이전 상위 층으로 부터 Node 가져옴
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i].get_ptr();

			while (true) {
				bool removed = false;
				SK_LF_NODE* succ = currs[i]->next[i].get_ptr(&removed);
				
				// 순회 중 삭제되었다면 물리적으로 삭제 시도
				while (true == removed) {
					// 내가 삭제하지 못했다면 retry
					if (false == prevs[i]->next[i].CAS(currs[i], succ, false, false)) {
						goto retry;
					}

					// 내가 삭제했다면 이어서 탐색
					currs[i] = succ;
					succ = currs[i]->next[i].get_ptr(&removed);
				}

				// 탐색한 curr가 찾으려는 값 이상이라면 다음층 또는 종결
				if (currs[i]->key >= x)
					break;

				// 아니라면 이어서 순회
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

			// 이미 존재한다면
			if (true == found)
				return false;

			// 생성할 노드의 레벨 (코인 토스)
			int lv = 0;
			for (int i = 0; i < MAX_TOP; ++i) {	// 이거 조건문이 <= 여야 MAX_TOP 까지 하는 것이 아닌가?
				if (rand() % 2 == 0) break;
				++lv;
			}

			SK_LF_NODE* new_node = new SK_LF_NODE{ x, lv };

			for (int i = 0; i <= lv; ++i) {
				new_node->next[i].set_ptr(currs[i]);
			}

			// 최하층을 내가 추가했으면(CAS에 성공했으면 내가 추가한 것임)
			if (false == prevs[0]->next[0].CAS(currs[0], new_node, false, false)) {	// 어떠한 이유로 CAS에 실패하였으므로 Find 부터 다시
				delete new_node;
				continue;
			}

			// 내가 추가했으면 위에 층들도 책임지고 연결
			for (int i = 1; i <= lv; ++i) {
				while (true) {
					if (true == prevs[i]->next[i].CAS(currs[i], new_node, false, false))
						break;

					Find(x, prevs, currs);
					//new_node->next[i]->set_ptr(currs[i]);	// 교재에는 버그로 없다 -> 없으면 EBR에서 오류 // 하지만 지금은 있으면 안돌아가기에 주석 처리
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

			// 존재하지 않는다면
			if (false == found)
				return false;

			// 찾았으므로 삭제 과정 시작
			SK_LF_NODE* del_node = currs[0];
			int lv = del_node->top_level;

			// 최상층 부터 remove 진행
			for (int i = lv; i > 0; --i) {
				bool removed = false;
				SK_LF_NODE* succ = del_node->next[i].get_ptr(&removed);

				// 삭제 되었는지 확인하면서 삭제 시도
				while (false == removed) {	// 삭제가 안되었다면 삭제 시도 (CAS)
					del_node->next[i].CAS(succ, succ, false, true);	// CAS의 성공을 확인하지 않아도 누군가 Removed 했으면 다음 아래 층으로 이동하기 위해
					succ = del_node->next[i].get_ptr(&removed);
				}
			}

			SK_LF_NODE* succ = del_node->next[0].get_ptr();
			while (true) {
				bool marking = del_node->next[0].CAS(succ, succ, false, true);

				// 내가 최하층 마킹(삭제)를 성공하면 내가 삭제한 것!
				if (marking) {
					//delete del_node;	// 메모리 릭 <- Find에서 물리적인 제거 후 해야하지만 거기서도 하지 못함
					Find(x, prevs, currs);	// 연결리스트 정리 작업용
					return true;
				}

				bool removed = false;
				succ = del_node->next[0].get_ptr(&removed);
				// 제거가 되었는데 나는 CAS에 실패했다 (다른놈이 마킹에 성공한 것)
				if (true == removed)
					return false;

				// 누군가 Add 등 연결리스트 구조가 바뀌어서 CAS에 실패한 것... 다시 시도
			}
		}
	}

	bool Contains(int x)
	{
		// Find와 다르게 CAS를 시도하고 goto로 다시 시도하지 않는다, WaitFree..?
		SK_LF_NODE* prevs[MAX_TOP + 1];
		SK_LF_NODE* currs[MAX_TOP + 1];

		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {
				prevs[i] = &head;
			}
			else {
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i].get_ptr();

			while (true)
			{
				bool removed = false;
				SK_LF_NODE* succ = currs[i]->next[i].get_ptr(&removed);
				while (true == removed) {
					currs[i] = succ;
					succ = currs[i]->next[i].get_ptr(&removed);
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
		auto p = head.next[0].get_ptr();
		for (int i = 0; i < 20; ++i) {
			if (p == &tail) break;
			std::cout << p->key << ", ";
			p = p->next[0].get_ptr();
		}

		std::cout << std::endl;
	}
};

class EBR_SK_LF_NODE;		// EBR SkipList LockFree Node 전방 선언

thread_local int thread_id;
thread_local std::queue<EBR_SK_LF_NODE*> node_free_queue;

class EBR_SK_SPTR			// SkipList 합성 포인터 (LockFree(CAS)를 위한)
{
private:
	std::atomic<long long> sptr;

public:
	EBR_SK_SPTR() : sptr{ 0 } {}

	void set_ptr(EBR_SK_LF_NODE* ptr)
	{
		sptr = reinterpret_cast<long long>(ptr);
	}

	EBR_SK_LF_NODE* get_ptr()
	{
		long long p = sptr.load();
		return reinterpret_cast<EBR_SK_LF_NODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}

	EBR_SK_LF_NODE* get_ptr(bool* removed)
	{
		long long p = sptr.load();
		*removed = (1 == (p & 1));	// p의 최하위 1비트로 removed를 판단
		return reinterpret_cast<EBR_SK_LF_NODE*>(p & 0xFFFFFFFFFFFFFFFE);
	}

	bool CAS(EBR_SK_LF_NODE* old_p, EBR_SK_LF_NODE* new_p, bool old_m, bool new_m)
	{
		long long old_v = reinterpret_cast<long long>(old_p);
		if (true == old_m) old_v = old_v | 1;
		else old_v = old_v & 0xFFFFFFFFFFFFFFFE;	// 굳이 필요한 작업인지는 잘 모르겠습니다

		long long new_v = reinterpret_cast<long long>(new_p);
		if (true == new_m) new_v = new_v | 1;
		else new_v = new_v & 0xFFFFFFFFFFFFFFFE;

		return std::atomic_compare_exchange_strong(&sptr, &old_v, new_v);
		// &sptr 위치에 있는 값을 old_v와 비교 후 같다면 new_v로 달라서 바꾸지 못했으면 false를 반환 후 &sptr에 있던 값은 old_v로 확인 가능
	}
};

class EBR_SK_LF_NODE		// SkipList LockFree Node 
{
public:
	int key;
	EBR_SK_SPTR next[MAX_TOP + 1] = {};	// 0층부터 9층(MAX_TOP)까지 있음
	int top_level;	// 현재 노드의 최상층 (지름길 존재하는 층)
	int ebr_number;

public:
	EBR_SK_LF_NODE(int x, int top) : key{ x }, top_level{ top }, ebr_number{ 0 }
	{
	}

	~EBR_SK_LF_NODE()
	{
	}
};

class EBR
{
private:
	std::atomic_int epoch_counter;
	std::atomic_int epoch_array[MAX_THREAD * 16];	// Cache Thrasing으로 성능저하를 막기 위해 (alignas 를 이용하는 방법도 존재)

public:
	EBR() : epoch_counter{ 1 }
	{

	}

	~EBR()
	{
		Clear();
	}

	void Clear()
	{
		while (not node_free_queue.empty()) {
			auto p = node_free_queue.front();
			node_free_queue.pop();
			delete p;
		}

		epoch_counter = 1;
	}

	void Reuse(EBR_SK_LF_NODE* node)
		// 해당 노드 재사용 하고싶을때 호출 (노드에서 Remove 될 때)
	{
		node->ebr_number = epoch_counter;
		node_free_queue.push(node);
		
		// PLAN A : if (node_free_queue.size() > MAX_FREE_SIZE) safe_delete();
		// 낭비되는 메모리의 최대값을 제한한다 (일적 개수 이상이면 삭제 delete를 통해서)
	}

	void Start_epoch()
		// 메서드를 시작할 때 epoch_array에 기록하기 위한 것
	{
		int epoch = ++epoch_counter;
		epoch_array[thread_id * 16] = epoch;
	}

	void End_epoch()
		// 메서드가 끝날 때 (return 할 때)
	{
		epoch_array[thread_id * 16] = 0;
	}

	EBR_SK_LF_NODE* Get_node(int x, int top)
		// 노드를 queue에서 가져오거나 생성해서 반환해주는 메서드
	{
		// PLAN B : 가능한 new/delete 없이 재사용

		// queue가 비어있다면 새로 만들어서 (new)
		if (node_free_queue.empty())
			return new EBR_SK_LF_NODE{ x, top };

		// 재활용 가능한 노드가 없다면 새로 만들어서 바로 반환
		EBR_SK_LF_NODE* p = node_free_queue.front();
		for (int i = 0; i < MAX_THREAD; ++i) {
			if ((epoch_array[i * 16] != 0 and epoch_array[i * 16] < p->ebr_number)) {
				return new EBR_SK_LF_NODE{ x, top };
			}
		}

		// 재활용 가능한 노드가 있다 (찾았다)
		node_free_queue.pop();
		p->key = x;
		p->top_level = top;
		return p;
	}
};

EBR ebr;

class EBR_LF_SK_SET			// EBR SkipList LockFree Set
{
private:
	EBR_SK_LF_NODE head{ std::numeric_limits<int>::min(), MAX_TOP };
	EBR_SK_LF_NODE tail{ std::numeric_limits<int>::max(), MAX_TOP };

public:
	EBR_LF_SK_SET()
	{
		Init();
	}

	void Init()
	{
		for (int i = 0; i <= MAX_TOP; ++i) {
			head.next[i].set_ptr(&tail);
		}
	}

	void Clear()
	{
		// 0층으로 순회하면서 삭제
		while (head.next[0].get_ptr() != &tail) {
			auto p = head.next[0].get_ptr();
			head.next[0].set_ptr(head.next[0].get_ptr()->next[0].get_ptr());
			delete p;
		}

		// 모두 삭제 후 head의 next는 tail로 설정
		Init();
	}

	bool Find(int x, EBR_SK_LF_NODE* prevs[], EBR_SK_LF_NODE* currs[])
	{
	retry:

		// 최상층 (지름길 부터 탐색)
		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {		// 최상층 인 경우 받아올 상위 층이 없어서 조건문
				prevs[i] = &head;
			}
			else {		// 최상층이 아니면 이전 상위 층으로 부터 Node 가져옴
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i].get_ptr();

			while (true) {
				bool removed = false;
				EBR_SK_LF_NODE* succ = currs[i]->next[i].get_ptr(&removed);

				// 순회 중 삭제되었다면 물리적으로 삭제 시도
				while (true == removed) {
					// 내가 삭제하지 못했다면 retry
					if (false == prevs[i]->next[i].CAS(currs[i], succ, false, false)) {
						goto retry;
					}

					// 내가 삭제했다면 이어서 탐색
					currs[i] = succ;
					succ = currs[i]->next[i].get_ptr(&removed);
				}

				// 탐색한 curr가 찾으려는 값 이상이라면 다음층 또는 종결
				if (currs[i]->key >= x)
					break;

				// 아니라면 이어서 순회
				prevs[i] = currs[i];
				currs[i] = succ;
			}

			// TODO : Reuse를 하는 부분이 추가가 필요해 보인다 (Reuse를 호출할 땐 연결리스트에서 물리적으로 삭제가 완료되었을 때)
			//		  연결리스트에서 모든 층이 제거되었음을 어떻게 판단하지?
		}

		return (currs[0]->key == x);
	}

	bool Add(int x)
	{
		// 생성할 노드의 레벨 (코인 토스)
		int lv = 0;
		for (int i = 0; i < MAX_TOP; ++i) {
			if (rand() % 2 == 0) break;
			++lv;
		}

		// TODO : 아래의 코드를 ebr의 Get_node로 바꾸어야 하지 않나?
		EBR_SK_LF_NODE* new_node = new EBR_SK_LF_NODE{ x, lv };

		ebr.Start_epoch();

		while (true) {
			EBR_SK_LF_NODE* prevs[MAX_TOP + 1];
			EBR_SK_LF_NODE* currs[MAX_TOP + 1];

			bool found = Find(x, prevs, currs);

			// 이미 존재한다면
			if (true == found) {
				ebr.Reuse(new_node);
				ebr.End_epoch();

				return false;
			}

			for (int i = 0; i <= lv; ++i) {
				new_node->next[i].set_ptr(currs[i]);
			}

			// 최하층을 내가 추가했으면(CAS에 성공했으면 내가 추가한 것임)
			if (false == prevs[0]->next[0].CAS(currs[0], new_node, false, false)) {	// 어떠한 이유로 CAS에 실패하였으므로 Find 부터 다시
				// delete new_node;
				continue;
			}

			// 내가 추가했으면 위에 층들도 책임지고 연결
			for (int i = 1; i <= lv; ++i) {
				while (true) {
					if (true == prevs[i]->next[i].CAS(currs[i], new_node, false, false))
						break;

					Find(x, prevs, currs);
					//new_node->next[i]->set_ptr(currs[i]);	// 교재에는 버그로 없다 -> 없으면 EBR에서 오류 // 하지만 지금은 있으면 안돌아가기에 주석 처리
				}
			}

			ebr.End_epoch();
			return true;
		}
	}

	bool Remove(int x)
	{
		ebr.Start_epoch();

		while (true) {
			EBR_SK_LF_NODE* prevs[MAX_TOP + 1];
			EBR_SK_LF_NODE* currs[MAX_TOP + 1];

			bool found = Find(x, prevs, currs);

			// 존재하지 않는다면
			if (false == found) {
				ebr.End_epoch();

				return false;
			}

			// 찾았으므로 삭제 과정 시작
			EBR_SK_LF_NODE* del_node = currs[0];
			int lv = del_node->top_level;

			// 최상층 부터 remove 진행
			for (int i = lv; i > 0; --i) {
				bool removed = false;
				EBR_SK_LF_NODE* succ = del_node->next[i].get_ptr(&removed);

				// 삭제 되었는지 확인하면서 삭제 시도
				while (false == removed) {	// 삭제가 안되었다면 삭제 시도 (CAS)
					del_node->next[i].CAS(succ, succ, false, true);	// CAS의 성공을 확인하지 않아도 누군가 Removed 했으면 다음 아래 층으로 이동하기 위해
					succ = del_node->next[i].get_ptr(&removed);
				}
			}

			EBR_SK_LF_NODE* succ = del_node->next[0].get_ptr();
			while (true) {
				bool marking = del_node->next[0].CAS(succ, succ, false, true);

				// 내가 최하층 마킹(삭제)를 성공하면 내가 삭제한 것!
				if (marking) {
					//ebr.Reuse(del_node);	// 이거 자체가 오류? marking에만 성공했다고 연결리스트에서 제거된건 아니기 때문
					Find(x, prevs, currs);	// 연결리스트 정리 작업용
					ebr.End_epoch();

					return true;
				}

				bool removed = false;
				succ = del_node->next[0].get_ptr(&removed);
				// 제거가 되었는데 나는 CAS에 실패했다 (다른놈이 마킹에 성공한 것)
				if (true == removed) {
					ebr.End_epoch();

					return false;
				}

				// 누군가 Add 등 연결리스트 구조가 바뀌어서 CAS에 실패한 것... 다시 시도
			}
		}
	}

	bool Contains(int x)
	{
		// Find와 다르게 CAS를 시도하고 goto로 다시 시도하지 않는다, WaitFree..?
		EBR_SK_LF_NODE* prevs[MAX_TOP + 1];
		EBR_SK_LF_NODE* currs[MAX_TOP + 1];

		ebr.Start_epoch();

		for (int i = MAX_TOP; i >= 0; --i) {
			if (i == MAX_TOP) {
				prevs[i] = &head;
			}
			else {
				prevs[i] = prevs[i + 1];
			}

			currs[i] = prevs[i]->next[i].get_ptr();

			while (true)
			{
				bool removed = false;
				EBR_SK_LF_NODE* succ = currs[i]->next[i].get_ptr(&removed);
				while (true == removed) {
					currs[i] = succ;
					succ = currs[i]->next[i].get_ptr(&removed);
				}

				if (currs[i]->key >= x)
					break;

				prevs[i] = currs[i];
				currs[i] = succ;
			}
		}

		ebr.End_epoch();
		return (currs[0]->key == x);
	}

	void Print20()
	{
		auto p = head.next[0].get_ptr();
		for (int i = 0; i < 20; ++i) {
			if (p == &tail) break;
			std::cout << p->key << ", ";
			p = p->next[0].get_ptr();
		}

		std::cout << std::endl;
	}
};

#define MY_SET EBR_LF_SK_SET
MY_SET my_set;


// 교수님 오류체크 코드
class HISTORY {
public:
	int op;
	int i_value;
	bool o_value;
	HISTORY(int o, int i, bool re) : op(o), i_value(i), o_value(re) {}
};

std::array<std::vector<HISTORY>, 16> history;

void worker_check(int num_threads, int _thread_id)
{
	thread_id = _thread_id;

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

// 벤치마크 코드
void benchmark(const int num_thread, int _thread_id)
{
	thread_id = _thread_id;

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

	// 오류 검사 부분
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

	// 실제 성능 측정
	{
		for (int n = 1; n <= MAX_THREAD; n = n * 2) {
			std::vector<std::thread> tv;
			auto start_t = high_resolution_clock::now();
			for (int i = 0; i < n; ++i) {
				tv.emplace_back(benchmark, n, i);
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
