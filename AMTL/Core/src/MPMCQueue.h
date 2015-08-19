#include <memory>
#include <atomic>

namespace amtl
{
	
	/*
		MPMCQueue is a lock-free multi-producer,multi-consumer queue.
		


	*/
	template<class T>
	class MPMCQueue
	{
		private:

			/* 
				the internals of this queue follow the technique used in Joe Seigh's Atomic Ptr Plus project.
				It continues to use reference counting as the primary method of memory reclamation, but extracts
				some reference counting information to a separate struct due to possible race conditions and incosistencies 
				that would occur in a naive implementation of keeping the internal count along with the primary data.

				With a traditional split reference count implementation, each thread that acceses a node increments
				an external count. When that thread leaves (will not touch the node anymore), it decrements an internal counter. 
				The sum of the internal	and external counts should therefore determine if reclaiming memory is safe. This safeness
				is realized after 2 events -- after all contending threads decrement the internal ref count and after the "sucessfull"
				thread increments the internal ref count such that it can reach 0. This notion of a "sucessful" thream comes from the fact that
				although there may be many threads that attempt to push concurrently, and many threads that try to pop concurrently, only 1 
				single thread out of the push()ers and 	1 single thread out of the pop()ers will be able to perform the action at a time due to 
				the internal synchronization of atomics.

				Consider an instance of this queue where 10 threads simultaenously wish to pop() some data. Eventually, all 10 requests will be 
			    served but, each update must occur atomically. What this means for pop()-ing threads is that many of them may find themselves 
			    contending with another pop()-ing thread to update	head. Since only 1 of these contending threads can actually modify head at a time, 
			    that "winning" thread is deemed "sucessful" and has the responsiblity of _incrementing_ the internal count to try and bring it 0. 
			    Specifically, the internal ref count is incremented by the value (external count - 2).  This number comes from the fact that
			    there can be at _most_ external_count-2 other threads touching this node.  "Why", you ask? The "successfull" threads owns a reference,
			    so that leaves external_count-1 other threads, BUT, every node begins life with an external_count of 1.
		
			*/
			struct node_counter
			{
				unsigned internal_count : 30;
				unsigned external_counters : 2;
			};

			struct counted_node_pointer;

			struct node
			{
				std::atomic<T*> data;
				std::atomic<node_counter> node_count;
				std::atomic<counted_node_pointer> next;

				node()
				{
					next = {0,nullptr};

					node_counter initial_count;
					initial_count.internal_count = 0;
					initial_count.external_counters = 2;// all nodes start off with 2 owning refs.
					node_count.store(initial_count);

					data = nullptr;
				}

				void release_reference()
				{
					node_counter new_counter;
					node_counter old_counter = node_count.load();
					do
					{
						new_counter = old_counter;
						--new_counter.internal_count;
					}while(! node_count.compare_exchange_strong(old_counter,new_counter));

					if(!new_counter.internal_count && !new_counter.external_counters)
					{
						delete this; 
					}
				}
			};

			struct counted_node_pointer
			{
				int external_count;
				node* node_ptr;
			};

			std::atomic<counted_node_pointer> head;
			std::atomic<counted_node_pointer> tail;

			void increase_ref_count(counted_node_pointer& old_node, std::atomic<counted_node_pointer>& marker)
			{
				// spin in a CAS loop until the marker external count can be properly incremented by this thread

				counted_node_pointer new_node;
				do
				{
					new_node = old_node;
					++new_node.external_count;
				} while(! marker.compare_exchange_strong(old_node,new_node));

				old_node.external_count = new_node.external_count;

			}

			void free_external_count(counted_node_pointer& winner_thread_node)
			{
				const int num_increase = winner_thread_node.external_count - 2;

				node* const node_ptr = winner_thread_node.node_ptr;

				node_counter new_count;
				node_counter old_count = node_ptr->node_count.load();

				do
				{
					new_count = old_count;
					new_count.internal_count += num_increase;
					--new_count.external_counters;
				} while(! node_ptr->node_count.compare_exchange_strong(old_count,new_count));

				if(!new_count.internal_count && !new_count.external_counters)
				{
					delete node_ptr;
				}
			}

			void set_tail_node(counted_node_pointer& old_tail, counted_node_pointer& new_tail)
			{
				// tail _should_ be old_tail. If an atomic CAS finds that it is, swap it to new_tail.
				// This needs to be done in a loop until the swap can be guaranteed. However,
				// a naive loop will not be enough.  It's possible that another thread also updates
				// tail while we're trying to modify it. This leads us to the case where a CAS
				// iteration failes, sets old_tail to what it found (which may be a brand new node),
				// then resets tail to new_tail, effectively wiping out the new node, or even worse
				// it may create a queue state where push()-ing threads spin indefinitely.

				// to prevent this, we need to save that node pointer that old_tail has upon entry, and check it
				// upon each failure of the CAS loop. There are 2 reasons why a CAS attempt fails:
				//	- Spurious failure
				//  - Legitamite failure

				// Upon either one, its important to see if the value of old_tail.node_ptr is the same as when 
				// we called this function. Otherwise, the invalid queue state may form.



				node* const saved_ptr = old_tail.node_ptr;
				while(!tail.compare_exchange_weak(old_tail,new_tail) && old_tail.node_ptr == saved_ptr);

				// the while loop will exit either because of a CAS success or a CAS failure w/ changed pointers
				// in the former case, the initial attempt at CAS succeeded (with the exception of spurious failures)

				// in the latter case, a CAS failed and old_tail got updated to a node without the same pointer. 
				// in that case, the loop needs to break.

				if(saved_ptr == old_tail.node_ptr)
				{
					// this thread got to set tail
					free_external_count(old_tail);
				}
				else
				{
					saved_ptr->release_reference();
				}


			}


			void push_impl(std::unique_ptr<T> data_ptr)
			{
				std::unique_ptr<node> node_ptr(new node);

				counted_node_pointer  new_tail;
				new_tail.external_count = 1;

				counted_node_pointer old_tail = tail.load();
				for(;;)
				{
					new_tail.node_ptr = node_ptr.get();
					increase_ref_count(old_tail,tail);
					T* old_data = nullptr;

					if(old_tail.node_ptr->data.compare_exchange_strong(old_data,data_ptr.get()))
					{
						
						counted_node_pointer old_next = {0};
						if(! old_tail.node_ptr->next.compare_exchange_strong(old_next,new_tail))
						{
							// some other thread already set the dummy node
							std::unique_ptr<node>{}.swap(node_ptr);
							new_tail = old_next;
						}

						set_tail_node(old_tail,new_tail);
						data_ptr.release();
						node_ptr.release();
						return;

					}
					else
					{
						// help out the "winner" thread by setting the dummy next node
						counted_node_pointer old_next = {0};
						if(old_tail.node_ptr->next.compare_exchange_strong(old_next,new_tail))
						{
							old_next = new_tail;
							node_ptr.release();
							node_ptr.reset(new node);
						}
						set_tail_node(old_tail,old_next);
					}
				}
			}

		public:
			MPMCQueue()
			{
				std::unique_ptr<node> initial_node{new node};

				counted_node_pointer  initial_counted_node = {1,initial_node.get()};
				head.store(initial_counted_node);
				tail.store(initial_counted_node);
				initial_node.release();
			}

			// disable copying
			MPMCQueue& operator= (const MPMCQueue& other) = delete;
			MPMCQueue(const MPMCQueue& other) = delete;

			// moving takes the donor's head/tail and nulls them out of the donor
			MPMCQueue(MPMCQueue&& other) : head(other.head.load()) , tail(other.tail.load())
			{
				other.head = {0,0};
				other.tail = {0,0};
			}
			MPMCQueue& operator=(MPMCQueue&& other) = delete;


			~MPMCQueue()
			{
				// node* head_ptr = head.load().node_ptr;
				// node* tail_ptr = tail.load().node_ptr;

				// // if there's any remaining data items in the queue upon destruction,
				// // iterate through the linked list and manage each T* by wrapping it in a unique_ptr.
				// // The loop should only extend up until tail's ptr because the class invariants
				// // maintain that after a succesful call to push(), tail points to a dummy node
				// // without any valid data.
				// while(head_ptr != tail_ptr)
				// {
				// 	node* ptr_next = head_ptr->next.load().node_ptr;
				// 	std::unique_ptr<T> head_data{head_ptr->data.load()};
				// 	delete head_ptr;
				// 	head_ptr = ptr_next;

				// }
				// // no more data to delete, just delete tail's node
				// delete head_ptr;
			}


			void push(const T& copy)
			{
				std::unique_ptr<T> data_ptr { new T{copy} };
				push_impl(std::move(data_ptr));
			}

			void push(T&& move)
			{
				std::unique_ptr<T> data_ptr { new T{std::move(move)} };
				push_impl(std::move(data_ptr));
			}

			template<typename... CtorArgs>
			void push(CtorArgs&&... ctor_args)
			{
				std::unique_ptr<T> data_ptr{ new T{std::forward<CtorArgs>(ctor_args)...} };
				push_impl(std::move(data_ptr));
			}
			

			std::unique_ptr<T> pop()
			{
				counted_node_pointer old_head = head.load();
				for(;;)
				{
					increase_ref_count(old_head,head);
					node* ptr = old_head.node_ptr;

					if(ptr == tail.load().node_ptr)
					{
						// empty queue
						ptr->release_reference();
						return {};
					}

					counted_node_pointer next = ptr->next.load();
					if(head.compare_exchange_strong(old_head,next))
					{
						std::unique_ptr<T> data(ptr->data.exchange(nullptr));
						free_external_count(old_head);
						return data;
					}

					ptr->release_reference();
				}
			}
			
	};
}