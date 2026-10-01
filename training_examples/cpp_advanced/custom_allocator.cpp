/*
 * custom_allocator.cpp - what a custom allocator really is, and why the
 * parking monitor does not need one.
 *
 * An allocator is any type that satisfies the Allocator requirements: it wraps
 * the allocate/deallocate pair so a container asks *it* for memory instead of
 * calling ::operator new directly. Two useful consequences:
 *   1. the pool can be reused, so a hot loop stops calling malloc;
 *   2. every allocation and deallocation can be counted, which is the
 *      cheapest possible leak detector.
 *
 * Build:  make
 * Run:    ./custom_allocator
 *
 * Relationship to the project: the monitor allocates TelemetryRecord values
 * on the heap constantly. A real-time or embedded version of this program
 * would use a pool allocator so the heap never fragments and the timing stays
 * predictable. The core project uses the standard allocator because KISS wins
 * at 4/10 difficulty - this file is the demonstration of the idea.
 */
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

/* ---------------------------------------------------------------- */
/* 1. A counting allocator: proof that all allocation goes through it */
/* ---------------------------------------------------------------- */

long g_allocations = 0;
long g_deallocations = 0;
long g_bytes_live = 0;

template <typename T>
struct CountingAllocator {
	using value_type = T;

	CountingAllocator() noexcept = default;

	template <typename U>
	CountingAllocator(const CountingAllocator<U>&) noexcept
	{
	}

	T* allocate(std::size_t n)
	{
		void* p = ::operator new(n * sizeof(T));
		if (p == nullptr)
			throw std::bad_alloc();
		++g_allocations;
		g_bytes_live += static_cast<long>(n * sizeof(T));
		return static_cast<T*>(p);
	}

	void deallocate(T* p, std::size_t n) noexcept
	{
		::operator delete(p);
		++g_deallocations;
		g_bytes_live -= static_cast<long>(n * sizeof(T));
	}

	template <typename U>
	bool operator==(const CountingAllocator<U>&) const noexcept
	{
		return true;
	}

	template <typename U>
	bool operator!=(const CountingAllocator<U>&) const noexcept
	{
		return false;
	}
};

/* ---------------------------------------------------------------- */
/* 2. A fixed-size memory pool: allocate/deallocate without malloc  */
/* ---------------------------------------------------------------- */

class MemoryPool {
public:
	MemoryPool(std::size_t block_size, std::size_t block_count)
	    : block_size_(block_size), total_(block_count), free_list_(nullptr)
	{
		for (std::size_t i = 0; i < block_count; ++i) {
			Block* block = ::operator new(block_size_);
			block->next = free_list_;
			free_list_ = block;
		}
	}

	~MemoryPool()
	{
		/* The pool owns every block it ever carved out. */
		while (free_list_) {
			Block* next = free_list_->next;
			::operator delete(free_list_);
			free_list_ = next;
		}
	}

	void* allocate()
	{
		if (free_list_ == nullptr)
			return nullptr; /* pool exhausted */
		Block* block = free_list_;
		free_list_ = block->next;
		++in_use_;
		return block;
	}

	void deallocate(void* p)
	{
		Block* block = static_cast<Block*>(p);
		block->next = free_list_;
		free_list_ = block;
		--in_use_;
	}

	std::size_t in_use() const { return in_use_; }
	std::size_t available() const { return total_ - in_use_; }
	std::size_t total() const { return total_; }

private:
	struct Block {
		Block* next;
	};

	std::size_t block_size_;
	std::size_t total_;
	std::size_t in_use_ = 0;
	Block* free_list_;
};

/* An allocator that hands out blocks from a MemoryPool. The STL only needs
 * allocate/deallocate plus the value_type typedef, so nothing more is
 * required of it. */
template <typename T>
class PoolAllocator {
public:
	using value_type = T;

	explicit PoolAllocator(MemoryPool* pool) : pool_(pool) {}

	/* The converting constructor lets a vector<Derived> share a
	 * vector<Base> allocator, and lets the vector copy the allocator. */
	template <typename U>
	PoolAllocator(const PoolAllocator<U>& other) : pool_(other.pool())
	{
	}

	T* allocate(std::size_t)
	{
		void* p = pool_->allocate();
		if (p == nullptr)
			throw std::bad_alloc();
		return static_cast<T*>(p);
	}

	void deallocate(T* p, std::size_t) noexcept { pool_->deallocate(p); }

	MemoryPool* pool() const { return pool_; }

private:
	MemoryPool* pool_;
};

/* ---------------------------------------------------------------- */
/* 3. Why this matters: manual memory vs RAII vs managed languages  */
/* ---------------------------------------------------------------- */

void demo_manual_vs_raii()
{
	std::cout << "\n=== 3. Manual memory vs RAII ===\n";

	/* The three classic ways to get memory back. */
	{
		int* leaked = new int(1);
		(void)leaked;
		std::cout << "  (a) new without delete: the value is lost, the\n"
			  << "      memory is never reused. This is a leak.\n";
	}

	{
		int* p = new int(2);
		delete p;
		std::cout << "  (b) new + delete: correct only if every exit path\n"
			  << "      remembers the delete.\n";
	}

	{
		std::unique_ptr<int> p(new int(3));
		std::cout << "  (c) unique_ptr: the delete happens in the\n"
			  << "      destructor on every path, including an\n"
			  << "      exception. This is RAII.\n";
	}

	/* And the one the parking monitor actually relies on: a container
	 * that grows without the programmer allocating anything by hand. */
	{
		std::vector<std::string> lines;
		for (int i = 0; i < 1000; ++i)
			lines.push_back("log line " + std::to_string(i));
		std::cout << "  (d) std::vector: 1000 strings, grown and freed by\n"
			  << "      the container. The only allocation the\n"
			  << "      program made was the vector itself.\n";
	}

	std::cout << "  Java/C# would call this (d) garbage collection: the\n"
		  << "  runtime reclaims unreachable memory. C++ has no such\n"
		  << "  guarantee, which is why ownership has to be explicit.\n";
}

} // namespace

int main()
{
	std::cout << "Custom allocators and memory management - training demonstration\n";
	std::cout << "(not part of the parking monitor runtime)\n";

	/* ---- counting allocator ---- */
	std::cout << "\n=== 1. CountingAllocator ===\n";
	{
		std::vector<int, CountingAllocator<int>> v;
		for (int i = 0; i < 100; ++i)
			v.push_back(i * 3);
		std::cout << "  vector size = " << v.size() << ", front = "
			  << v.front() << ", back = " << v.back() << "\n";
		std::cout << "  bytes currently held by the pool allocator = "
			  << g_bytes_live << "\n";
	}
	std::cout << "  allocations   = " << g_allocations << "\n"
		  << "  deallocations = " << g_deallocations << "\n"
		  << "  bytes still live = " << g_bytes_live
		  << "  (zero means no leak)\n";

	/* ---- pool allocator ---- */
	std::cout << "\n=== 2. PoolAllocator over a MemoryPool ===\n";
	{
		MemoryPool pool(64, 8);
		std::cout << "  pool blocks: " << pool.total() << "\n";

		{
			PoolAllocator<int> alloc(&pool);
			/* reserve() makes the number of pool blocks used by
			 * the container exact and predictable, instead of
			 * depending on the implementation's growth factor. */
			std::vector<int, PoolAllocator<int>> v(alloc);
			v.reserve(4);
			std::cout << "  after reserve(4): in use = " << pool.in_use()
				  << ", available = " << pool.available() << "\n";

			for (int i = 0; i < 4; ++i)
				v.push_back(i * 11);
			std::cout << "  contents: ";
			for (int x : v)
				std::cout << x << " ";
			std::cout << "\n  filling to capacity needs no new block: "
				  << "in use = " << pool.in_use() << "\n";

			v.pop_back();
			v.push_back(99); /* reuses the freed slot */
			std::cout << "  after pop+push: in use = " << pool.in_use()
				  << ", back = " << v.back() << "\n";
		}
		std::cout << "  after the scope: in use = " << pool.in_use()
			  << ", available = " << pool.available()
			  << " - the container returned its block\n";
	}

	/* A pool is bounded on purpose. Draining it shows the failure mode a
	 * fixed pool has, which is exactly when a real-time design cares. */
	{
		MemoryPool pool(64, 3);
		void* held[3];
		int taken = 0;
		while (taken < 3) {
			void* p = pool.allocate();
			if (p == nullptr)
				break;
			held[taken++] = p;
		}
		std::cout << "\n  drained a 3-block pool: took " << taken
			  << " blocks, available = " << pool.available() << "\n";
		std::cout << "  the next allocate() returns nullptr, and the\n"
			  << "  PoolAllocator turns that into std::bad_alloc\n"
			  << "  instead of quietly calling malloc - that is the\n"
			  << "  property a real-time system needs.\n";
		for (int i = 0; i < taken; ++i)
			pool.deallocate(held[i]);
		std::cout << "  after returning them: available = "
			  << pool.available() << "\n";
	}

	demo_manual_vs_raii();

	std::cout << "\nAllocator demonstration completed.\n";
	return 0;
}
