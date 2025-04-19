#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <set>	// 에러체크를 위해


struct Node {
	int key;
	Node* volatile next;

	Node(int k = 0) : key(k), next(nullptr) {}
};

class LockFreeStack {
private:
	Node* volatile top;

public:
	LockFreeStack() : top(nullptr) {}
	~LockFreeStack() { Clear(); }

	void Clear();
	void Push(int key);
	int Pop();
	void Print20();

};

class Exchanger {
// 다시 봐야 함 (이해 못함)
};

class EliminationArray {
private:
	static const int SIZE = 8;
	std::vector<Exchanger> slots;

public:
	EliminationArray() : slots(SIZE) {}
	~EliminationArray() {}

	int Visit(int key);

};

class EliminationBackoffStack {
private:
	LockFreeStack stack;
	EliminationArray elimination;

public:
	void Push(int key);
	int Pop();

};