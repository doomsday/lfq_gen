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
		unsigned external_counters:2;
	};

	struct node
	{
		std::atomic<T*> data;
		std::atomic<node_counter> count;
		std::atomic<counted_node_ptr> next;	// 1

		node() {
			node_counter new_count;
			new_count.internal_count = 0;
			// queue's own counters: only head and tail (max = 2)
			new_count.external_counters = 2;
			count.store(new_count);

			next.ptr = nullptr;
			next.external_count = 0;
		}

		/**
		* Decreases this node's internal_count and deletes useless node.
		* Release the single reference held by this thread */
		void release_ref() {
			node_counter old_counter = count.load(std::memory_order_relaxed);
			node_counter new_counter;

			do {
				new_counter = old_counter;
				--new_counter.internal_count;
			} while (!count.compare_exchange_strong(old_counter, new_counter,
				std::memory_order_acquire, std::memory_order_relaxed));

			if (!new_counter.internal_count && !new_counter.external_counters) {
				delete this;
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
				--new_counter.external_counters;
				new_counter.internal_count += count_increase;
			} while (!ptr->count.compare_exchange_strong(old_counter, new_counter,
				std::memory_order_acquire, std::memory_order_relaxed));

			// delete node if all counters became 0
			if (!new_counter.internal_counter && !new_counter.external_counters) {
				delete ptr;
			}
		};

		void set_new_tail(counted_node_ptr& old_tail, counted_node_ptr const& new_tail) {	// 1
			node* const current_tail_ptr = old_tail.ptr;

			// if another thread updated tail -> check that new tail pointer to node is the same as
			// it was before loop -> break if not the same
			while (!tail.compare_exchange_weak(old_tail, new_tail) && old_tail.ptr == current_tail_ptr);	// 2

			if (old_tail.ptr == current_tail_ptr)	// 3
			// ptr is the same once the loop has exited, successfully set the tail
				free_external_count(old_tail);	// 4
			else
			// another thread will have freed the counter, release the single reference held by this thread
				current_tail_ptr->release_ref();	// 5
		}
	};

	std::atomic<counted_node_ptr> head;
	std::atomic<counted_node_ptr> tail;

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
			increase_external_count(tail, old_tail);

			T* old_data = nullptr;

			if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get()))	// 6
			{
			// successfully set the tail's node's data pointer to 'new_data' because it was 'nullptr'
				counted_node_ptr old_next = { 0 };
				if (!old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {	// 7
				// another thread has already helped this thread and set the next pointer in step 10
					delete new_next.ptr;	// 8: don't need the new empty node, delete it
					new_next = old_next;	// 9: use the next value that the other thread set for updating tail
				}
				set_new_tail(old_tail, new_next);
				new_data.release();
				break;
			}
			else {	// 10: help the successful thread to complete the update: set 'next' and 'tail'
			// the tail's node's data pointer was not 'nullptr'
				counted_node_ptr old_next = { 0 };
				if (old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {	// 11
					old_next = new_next;	// 12
					new_next.ptr = new node;	// 13
				}
				set_new_tail(old_tail, old_next);	// 14
			}
		}
	}

	/**
	* Pops node from the queue */
	std::unique_ptr<T> pop() {
		counted_node_ptr old_head = head.load(std::memory_order_relaxed);

		for (;;) {
			increase_external_count(head, old_head);
			node* const ptr = old_head.ptr;

			/* if head's counted_node_ptr's ptr points to the same node as tail
			// it means that the queue is empty so return new empty unique_ptr */
			if (ptr == tail.load().ptr) {
				prt->release_ref();
				return std::unique_ptr<T>();
			}

			counted_node_ptr next = ptr->next.load();	// 2
			// this compares the external count and pointer as a single entity
			if (head.compare_exchange_strong(old_head, next)) {
				T* const res = ptr->data.exchange(nullptr);
				free_external_count(old_head);
				return std::unique_ptr<T>(res);
			}

			ptr->release_ref;
		}
	}
};


int main()
{
	lock_free_queue<int>* lfq = new lock_free_queue<int>();
	lfq->push(5);

	return 0;
}

