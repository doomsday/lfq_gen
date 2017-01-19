// lfq_gen.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <memory>
#include <atomic>
#include <thread>
#include <stdexcept>

template <typename T>
class lock_free_queue {
private:
	struct node;

	struct counted_node_ptr
	{
		int external_count;
		node* ptr;
	};

	struct node_counter {
		// Keeping the structure within a machine word makes it more likely
		// that the atomic operations can be lock-free on many platforms
		unsigned internal_count:30;
		unsigned external_counters:2;	// 2
	};

	struct node
	{
		std::atomic<T*> data;
		std::atomic<node_counter> count;	// 3
		counted_node_ptr next;

		node() {
			node_counter new_count;
			new_count.internal_count = 0;
			// queue's own counters: only head and tail (max = 2)
			new_count.external_counters = 2;	// 4
			count.store(new_count);

			next.ptr = nullptr;
			next.external_count = 0;
		}

		/**
		* Decreases this node's internal_count and deletes useless node */
		void release_ref() {
			node_counter old_counter = count.load(std::memory_order_relaxed);
			node_counter new_counter;

			do {
				new_counter = old_counter;
				--new_counter.internal_count;	// 1
			} while (!count.compare_exchange_strong(old_counter, new_counter,	// 2
				std::memory_order_acquire, std::memory_order_relaxed));

			if (!new_counter.internal_count && !new_counter.external_counters) {
				delete this;	// 3
			}
		}


		/**
		* Makes both arguments equal to the first argument with external_count
		* incremented by 1 */
		static void increase_external_count(std::atomic<counted_node_ptr>& counter,
			counted_node_ptr& old_counter) {
			counted_node_ptr new_counter;

			do {
				new_counter = old_counter;
				++new_counter.external_count;
			} while (!counter.compare_exchange_strong(old_counter, new_counter,
				std::memory_order_acquire, std::memory_order_relaxed));

			old_counter.external_count = new_counter.external_count;
		};

		/**
		* Decrease argument node's external count by 1, and internal count
		* by 2. If both counters become 0 then deletes node pointed by
		* argument's counted_node_ptr */
		static void free_external_count(counted_node_ptr& old_node_ptr) {
			// make a copy of arguments nodes's pointer to node
			node* const ptr = old_node_ptr.ptr;

			// calculate arguments node's external count decremented by 2
			int const count_increase = old_node_ptr.external_count - 2;

			// get argument node's internal 'count'
			node_counter old_counter = ptr->count.load(std::memory_order_relaxed);

			// operating on node's count
			node_counter new_counter;
			do {
				new_counter = old_counter;
				--new_counter.external_counters;	// 1
				new_counter.internal_count += count_increase;	// 2
			} while (!ptr->count.compare_exchange_strong(old_counter, new_counter,	// 3
				std::memory_order_acquire, std::memory_order_relaxed));

			// delete node if all counters became 0
			if (!new_counter.internal_counter && !new_counter.external_counters) {
				delete ptr;	// 4
			}
		};
	};

	std::atomic<counted_node_ptr> head;
	std::atomic<counted_node_ptr> tail;	// 1

public:
	/**
	* Pushes new node to the queue */
	void push(T new_value) {

		std::unique_ptr<T> new_data(new T(new_value));
		counted_node_ptr new_next;
		new_next.ptr = new node;
		new_next.external_count = 1;
		counted_node_ptr old_tail = tail.load();

		for (;;) {
			increase_external_count(tail, old_tail);	// 5

			T* old_data = nullptr;
			// no other thread can modify tail while data is not nullptr
			if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get()))	// 6
			{
				old_tail.ptr->next = new_next;
				old_tail = tail.exchange(new_next);
				// now node is fully pushed (data is nullptr), so other threads
				// can proceed with their pushes
				free_external_counter(old_tail);	// 7
				new_data.release();
				break;
			}
			old_tail.ptr->release_ref(); //
		}
	}

	/**
	* Pops node from the queue */
	std::unique_ptr<T> pop() {
		counted_node_ptr old_head = head.load(std::memory_order_relaxed);	// 1

		for (;;) {
			increase_external_count(head, old_head);	// 2
			node* const ptr = old_head.ptr;

			// if queue is empty ('ptr' is null)
			if (ptr == tail.load().ptr) {
				prt->release_ref();		// 3
				return std::unique_ptr<T>();
			}

			// this compares the external count and pointer as a single entity
			if (head.compare_exchange_strong(old_head, ptr->next)) {	// 4
				T* const res = ptr->data.exchange(nullptr);
				free_external_count(old_head);	// 5
				return std::unique_ptr<T>(res);
			}

			ptr->release_ref;	// 6
		}
	}
};


int main()
{
	return 0;
}

