/*
 * data_structures.cpp - one runnable file covering the C++ data structure
 * part of the syllabus.
 *
 * Covered: array, singly linked list, stack, queue, circular queue,
 * priority queue (binary heap), deque, binary search tree, graph (BFS/DFS),
 * hash table, set, and trie. Plus the searching and sorting algorithms.
 *
 * Build:  make
 * Run:    ./data_structures
 *
 * This file is a teaching module. It is NOT part of the parking monitor and
 * the monitor does not depend on it.
 */
#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void section(const char* title)
{
	std::cout << "\n=== " << title << " ===\n";
}

/* ---------------- 1. array ---------------- */
void demo_array()
{
	section("1. Array (contiguous memory, O(1) index)");

	int raw[5] = {10, 20, 30, 40, 50};
	std::vector<int> v(raw, raw + 5);

	for (size_t i = 0; i < v.size(); ++i)
		std::cout << "  v[" << i << "] = " << v[i] << "  (address offset "
			  << i * sizeof(int) << " bytes)\n";

	/* The same thing with a range-based loop, which is what you should
	 * write by default in modern C++. */
	int sum = 0;
	for (int x : v)
		sum += x;
	std::cout << "  sum = " << sum << "\n";
	std::cout << "  sizeof(int) = " << sizeof(int) << " bytes, so the array "
		  << "needs " << v.size() * sizeof(int) << " bytes\n";
}

/* ---------------- 2. singly linked list ---------------- */
struct Node {
	int data;
	Node* next;
	explicit Node(int d) : data(d), next(nullptr) {}
};

class LinkedList {
public:
	LinkedList() : head_(nullptr) {}
	~LinkedList()
	{
		/* The list owns its nodes, so the destructor frees them.
		 * This is manual ownership done correctly. */
		Node* n = head_;
		while (n) {
			Node* tmp = n->next;
			delete n;
			n = tmp;
		}
	}

	void push_back(int value)
	{
		Node* node = new Node(value);
		if (!head_) {
			head_ = node;
			return;
		}
		Node* n = head_;
		while (n->next)
			n = n->next;
		n->next = node;
	}

	void print() const
	{
		for (Node* n = head_; n; n = n->next)
			std::cout << n->data << " -> ";
		std::cout << "NULL\n";
	}

private:
	Node* head_;
};

void demo_linked_list()
{
	section("2. Singly linked list (nodes scattered on the heap)");

	LinkedList list;
	for (int v : {5, 10, 15})
		list.push_back(v);
	list.print();
	std::cout << "  insert at the tail is O(n); a doubly linked list or a\n"
		  << "  std::list would be better for heavy use. The point here\n"
		  << "  is the node/pointer relationship and the ownership rule.\n";
}

/* ---------------- 3. stack ---------------- */
void demo_stack()
{
	section("3. Stack - LIFO (last in, first out)");

	std::stack<int> s;
	for (int i = 1; i <= 5; ++i)
		s.push(i * 10);
	std::cout << "  pushed 10..50, top = " << s.top() << "\n";
	while (!s.empty()) {
		std::cout << "  pop -> " << s.top() << "\n";
		s.pop();
	}
	std::cout << "  related to the project: the call stack, and the LIFO\n"
		  << "  undo discipline of a driver's module parameter stack.\n";
}

/* ---------------- 4. queue ---------------- */
void demo_queue()
{
	section("4. Queue - FIFO (first in, first out)");

	std::queue<int> q;
	for (int i = 1; i <= 4; ++i)
		q.push(i);
	std::cout << "  front = " << q.front() << ", back = " << q.back()
		  << ", size = " << q.size() << "\n";
	while (!q.empty()) {
		std::cout << "  pop -> " << q.front() << "\n";
		q.pop();
	}
	std::cout << "  >>> this is the container the Logger uses:\n"
		  << "      the monitor thread pushes log lines and the writer\n"
		  << "      thread pops them in the same order.\n";
}

/* ---------------- 5. circular queue ---------------- */
class CircularQueue {
public:
	explicit CircularQueue(size_t capacity) : buf_(capacity), head_(0), tail_(0), count_(0) {}

	bool enqueue(int value)
	{
		if (count_ == buf_.size())
			return false; /* full */
		buf_[tail_] = value;
		tail_ = (tail_ + 1) % buf_.size();
		++count_;
		return true;
	}

	bool dequeue(int* out)
	{
		if (count_ == 0)
			return false; /* empty */
		*out = buf_[head_];
		head_ = (head_ + 1) % buf_.size();
		--count_;
		return true;
	}

private:
	std::vector<int> buf_;
	size_t head_, tail_, count_;
};

void demo_circular_queue()
{
	section("5. Circular queue (fixed buffer, index wraps with modulo)");

	CircularQueue cq(4);
	for (int v : {1, 2, 3, 4}) {
		if (!cq.enqueue(v))
			std::cout << "  enqueue(" << v << ") rejected: full\n";
	}
	std::cout << "  enqueue(5) on a full queue = "
		  << (cq.enqueue(5) ? "accepted" : "rejected (full)") << "\n";
	int value = 0;
	cq.dequeue(&value);
	std::cout << "  dequeued " << value << ", now enqueue(5) = "
		  << (cq.enqueue(5) ? "accepted" : "rejected")
		  << " - the freed slot is reused by wrapping around\n";
}

/* ---------------- 6. priority queue / heap ---------------- */
void demo_priority_queue()
{
	section("6. Priority queue (binary heap, best first)");

	std::priority_queue<int> maxHeap;
	for (int v : {7, 1, 9, 3})
		maxHeap.push(v);
	std::cout << "  max-heap pops: ";
	while (!maxHeap.empty()) {
		std::cout << maxHeap.top() << " ";
		maxHeap.pop();
	}
	std::cout << "\n";

	std::priority_queue<int, std::vector<int>, std::greater<int>> minHeap;
	for (int v : {7, 1, 9, 3})
		minHeap.push(v);
	std::cout << "  min-heap pops: ";
	while (!minHeap.empty()) {
		std::cout << minHeap.top() << " ";
		minHeap.pop();
	}
	std::cout << "\n  push/pop are O(log n) because the array is kept as a heap.\n";
}

/* ---------------- 7. deque ---------------- */
void demo_deque()
{
	section("7. Deque - double ended queue, push/pop at both ends");

	std::deque<std::string> d;
	d.push_back("middle");
	d.push_front("front");
	d.push_back("back");
	for (const auto& s : d)
		std::cout << "  " << s << "\n";
	std::cout << "  front = " << d.front() << ", back = " << d.back()
		  << "\n  the SRS suggests a deque for a window of recent\n"
		  << "  telemetry records.\n";
}

/* ---------------- 8. binary search tree ---------------- */
class BST {
public:
	explicit BST(int v) : value_(v), left_(nullptr), right_(nullptr) {}

	void insert(int v)
	{
		if (v < value_) {
			if (left_)
				left_->insert(v);
			else
				left_ = new BST(v);
		} else {
			if (right_)
				right_->insert(v);
			else
				right_ = new BST(v);
		}
	}

	bool contains(int v) const
	{
		const BST* n = this;
		while (n) {
			if (v == n->value_)
				return true;
			n = (v < n->value_) ? n->left_ : n->right_;
		}
		return false;
	}

	void inorder(std::vector<int>* out) const
	{
		if (left_)
			left_->inorder(out);
		out->push_back(value_);
		if (right_)
			right_->inorder(out);
	}

	~BST()
	{
		delete left_;
		delete right_;
	}

private:
	int value_;
	BST* left_;
	BST* right_;
};

void demo_tree()
{
	section("8. Binary search tree (inorder traversal gives sorted output)");

	BST root(50);
	for (int v : {30, 70, 20, 40, 60, 80})
		root.insert(v);

	std::vector<int> sorted;
	root.inorder(&sorted);
	std::cout << "  inorder: ";
	for (int v : sorted)
		std::cout << v << " ";
	std::cout << "\n  contains(40) = " << std::boolalpha << root.contains(40)
		  << ", contains(45) = " << root.contains(45) << "\n";
	std::cout << "  search is O(h); h is O(log n) only while the tree stays\n"
		  << "  balanced, which is why std::map (a red-black tree) is used\n"
		  << "  in real code instead.\n";
}

/* ---------------- 9. graph ---------------- */
class Graph {
public:
	explicit Graph(int vertices) : adj_(vertices) {}

	void add_edge(int from, int to)
	{
		adj_[from].push_back(to);
	}

	void bfs(int start, std::vector<int>* visited_order) const
	{
		std::vector<bool> seen(adj_.size(), false);
		std::queue<int> q;
		q.push(start);
		seen[start] = true;

		while (!q.empty()) {
			int u = q.front();
			q.pop();
			visited_order->push_back(u);
			for (int v : adj_[u]) {
				if (!seen[v]) {
					seen[v] = true;
					q.push(v);
				}
			}
		}
	}

	void dfs(int start, std::vector<int>* visited_order) const
	{
		std::vector<bool> seen(adj_.size(), false);
		dfs_recursive(start, seen, visited_order);
	}

private:
	void dfs_recursive(int u, std::vector<bool>& seen,
			    std::vector<int>* order) const
	{
		seen[u] = true;
		order->push_back(u);
		for (int v : adj_[u]) {
			if (!seen[v])
				dfs_recursive(v, seen, order);
		}
	}

	std::vector<std::vector<int>> adj_;
};

void demo_graph()
{
	section("9. Graph - breadth first and depth first search");

	Graph g(6);
	int edges[][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 4}, {4, 5}};
	for (auto& e : edges)
		g.add_edge(e[0], e[1]);

	std::vector<int> order;
	g.bfs(0, &order);
	std::cout << "  BFS from 0: ";
	for (int v : order)
		std::cout << v << " ";
	std::cout << "\n";

	order.clear();
	g.dfs(0, &order);
	std::cout << "  DFS from 0: ";
	for (int v : order)
		std::cout << v << " ";
	std::cout << "\n  BFS uses a queue, DFS uses recursion or an explicit\n"
		  << "  stack - the only structural difference.\n";
}

/* ---------------- 10. hash table ---------------- */
void demo_hash_table()
{
	section("10. Hash table (unordered_map, O(1) average lookup)");

	std::unordered_map<std::string, int> counts;
	for (const char* state : {"SAFE", "WARNING", "SAFE", "DANGER", "SAFE",
				  "WARNING"})
		++counts[state];

	for (const auto& entry : counts)
		std::cout << "  " << entry.first << " : " << entry.second << "\n";

	std::cout << "  find(\"DANGER\") -> ";
	auto it = counts.find("DANGER");
	std::cout << (it == counts.end() ? "not present" : std::to_string(it->second))
		  << "\n";
	std::cout << "  >>> the SRS asks for a map of state/event counts;\n"
		  << "      unordered_map is the same idea with O(1) lookup.\n";
}

/* ---------------- 11. set ---------------- */
void demo_set()
{
	section("11. Set (unique keys, sorted)");

	std::set<int> seen;
	for (int v : {50, 20, 50, 80, 20, 110})
		seen.insert(v);

	std::cout << "  unique values: ";
	for (int v : seen)
		std::cout << v << " ";
	std::cout << "\n  size = " << seen.size() << " (duplicates dropped)\n";
	std::cout << "  lower_bound(50) = " << *seen.lower_bound(50) << "\n";
}

/* ---------------- 12. trie ---------------- */
class Trie {
public:
	Trie() : root_(new Node()) {}
	~Trie() { delete root_; }

	void insert(const std::string& word)
	{
		Node* n = root_;
		for (char c : word) {
			c = static_cast<char>(std::tolower(
				static_cast<unsigned char>(c)));
			if (!n->child[static_cast<unsigned char>(c) - 'a']) {
				n->child[static_cast<unsigned char>(c) - 'a'] =
					new Node();
			}
			n = n->child[static_cast<unsigned char>(c) - 'a'];
		}
		n->is_word = true;
	}

	bool search(const std::string& word) const { return find(word) != nullptr; }

	bool starts_with(const std::string& prefix) const
	{
		return find(prefix) != nullptr;
	}

private:
	struct Node {
		Node* child[26] = {nullptr};
		bool is_word = false;
		~Node()
		{
			for (Node* c : child)
				delete c;
		}
	};

	const Node* find(const std::string& word) const
	{
		const Node* n = root_;
		for (char c : word) {
			c = static_cast<char>(std::tolower(
				static_cast<unsigned char>(c)));
			int index = static_cast<unsigned char>(c) - 'a';
			if (index < 0 || index >= 26 || !n->child[index])
				return nullptr;
			n = n->child[index];
		}
		return n;
	}

	Node* root_;
};

void demo_trie()
{
	section("12. Trie (prefix tree)");

	Trie trie;
	for (const char* w : {"sensor", "sense", "safe", "danger"})
		trie.insert(w);

	std::cout << "  search(\"sensor\")       = " << std::boolalpha
		  << trie.search("sensor") << "\n";
	std::cout << "  search(\"sensing\")      = " << trie.search("sensing")
		  << "\n";
	std::cout << "  starts_with(\"sens\")    = " << trie.starts_with("sens")
		  << "\n";
	std::cout << "  starts_with(\"senso\")   = " << trie.starts_with("senso")
		  << "\n";
}

/* ---------------- 13. searching and sorting ---------------- */
void demo_search_and_sort()
{
	section("13. Searching and sorting");

	std::vector<int> values = {120, 30, 90, 10, 50, 20};

	std::cout << "  before sort: ";
	for (int v : values)
		std::cout << v << " ";
	std::cout << "\n";

	std::sort(values.begin(), values.end());
	std::cout << "  std::sort:  ";
	for (int v : values)
		std::cout << v << " ";
	std::cout << "\n";

	/* binary search, hand written */
	int target = 50;
	int lo = 0, hi = static_cast<int>(values.size()) - 1, found = -1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		if (values[mid] == target) {
			found = mid;
			break;
		}
		if (values[mid] < target)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	std::cout << "  binary search for " << target << " -> index " << found
		  << " (O(log n) comparisons, needs sorted data)\n";

	auto it = std::lower_bound(values.begin(), values.end(), 30);
	std::cout << "  std::lower_bound(30) -> " << (it - values.begin()) << "\n";

	std::cout << "  std::find on unsorted data would be O(n), which is why\n"
		  << "  the SRS pairs find() with sort() for statistics work.\n";
}

} // namespace

int main()
{
	std::cout << "C++ data structures - training demonstration\n";
	std::cout << "(not part of the parking monitor runtime)\n";

	demo_array();
	demo_linked_list();
	demo_stack();
	demo_queue();
	demo_circular_queue();
	demo_priority_queue();
	demo_deque();
	demo_tree();
	demo_graph();
	demo_hash_table();
	demo_set();
	demo_trie();
	demo_search_and_sort();

	std::cout << "\nAll data structure demonstrations completed.\n";
	return 0;
}
