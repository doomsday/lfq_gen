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
			new_count.external_counters = 2;	// 4
			count.store(new_count);

			next.ptr = nullptr;
			next.external_count = 0;
		}
	};

	std::atomic<counted_node_ptr> head;
	std::atomic<counted_node_ptr> tail;	// 1

public:
	void push(T new_value) {

		std::unique_ptr<T> new_data(new T(new_value));
		counted_node_ptr new_next;
		new_next.ptr = new node;
		new_next.external_count = 1;
		counted_node_ptr old_tail = tail.load();

		for (;;) {
			increase_external_count(tail, old_tail);	// 5

			T* old_data = nullptr;
			if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get()))	// 6
			{
				old_tail.ptr->next = new_next;
				old_tail = tail.exchange(new_next);
				free_external_counter(old_tail);	// 7
				new_data.release();
				break;
			}
			old_tail.ptr->release_ref();
		}
	}
};


int main()
{
	return 0;
}

