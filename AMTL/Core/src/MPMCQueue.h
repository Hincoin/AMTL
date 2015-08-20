#include <atomic>
#include <memory>

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
				thread increments the internal ref count such that it can reach 0. This notion of a "sucessful" thread comes from the fact that
				although there may be many threads that attempt to push concurrently or many threads that try to pop concurrently, only 1 
				single thread out of the push()ers and 	1 single thread out of the pop()ers will be able to perform the action at a time due to 
				the internal synchronization of atomics.

				Consider an instance of this queue where 10 threads simultaenously wish to pop() some data. Eventually, all 10 requests will be 
			    served but, each update must occur atomically. What this means for pop()-ing threads is that many of them may find themselves 
			    contending with another pop()-ing thread to update	head. Since only 1 of these contending threads can actually modify head at a time, 
			    that "winning" thread is deemed "sucessful" and has the responsiblity of _incrementing_ the internal count to try and bring it 0. 
			    Specifically, the internal ref count is incremented by the value (external count - 2).  This number comes from the fact that
			    there can be at _most_ external_count-2 other threads touching this node.  Why - 2? The "successfull" threads owns a reference,
			    so that leaves external_count-1 other threads, BUT, every node begins life with an external_count of 1. This seemingly unecessary
			    reference count is needed to prevent race conditions between pushers and poppers that contend on the same node. Because the push()ers 
			    _collectively_ own a single reference count, and the poppers _collectively_ also own a single reference count, this prevents a node
			    from being deleted out from a push()-ing thread that simply was scheduled poorly and left to the fate of pop()-ers bringing the internal
			    ref count down to 0. However, since that lonely, poorly-scheduled thread is _still_ a push()-ing thread, it continues to own an external count
			    -- thus preventing the node from being deleted out from under its feet.

			    Alas, this alone doesn't keep the queue safe. Since the queue can be modified by _both_ a head and tail, another value is needed to signal the
			    relinquishment of a head/tail's ownership from a node. Since it only needs to be a bit, this value can be implemented as a 2 bit value. And, this
			    queue being lock-free, the (30 or 62) bits can be used to implement the internal count. This structure is represented below by struct node_counter.
			    It's total size is that of an int, so lock-free operations on it have the same guarantee as std::atomic<int>::is_lock_free()
			 
			*/

			struct node_counter
			{
				unsigned internal_count : sizeof(int)-2;
				unsigned external_counters : 2;
			};

			struct counted_node_pointer;

			struct node
			{
				std::atomic<T*> data; 					// pushing onto the queue is done by atomically CAS-ing this data field, hence the need for atomic<T*>
				std::atomic<node_counter> node_count;	// reference count operations certainly need be atomic as they are read/written by multiple threads
				counted_node_pointer next;				// next is only ever modified by a single thread, so atomicity is not needed.

				node()
				{
					next = {0,nullptr};

					node_counter initial_count;
					initial_count.internal_count = 0;
					initial_count.external_counters = 2;
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

			void increase_ref_count(counted_node_pointer& old_node, std::atomic<counted_node_pointer>& marker) noexcept
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


			void free_external_count(counted_node_pointer& winner_thread_node) noexcept
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

			// push_impl is the internal worker function that actually pushes data onto the queue.
			// it does so by atomically CAS-ing tail's node_ptr->data field. If a thread can swap
			// node_ptr->data from nullptr to valid data, that thread is now responsible for moving tail

			// other threads must keep spinning until tail is updated.
			// TODO: Implement helping-threads that create dummy nodes, push tail.


			void push_impl(std::unique_ptr<node> node_ptr, std::unique_ptr<T> data_ptr) noexcept
			{

				counted_node_pointer  new_tail;
				new_tail.external_count = 1;
				new_tail.node_ptr = node_ptr.get();

				counted_node_pointer old_tail = tail.load();
				for(;;)
				{
					
					increase_ref_count(old_tail,tail);
					T* old_data = nullptr;

					if(old_tail.node_ptr->data.compare_exchange_strong(old_data,data_ptr.get()))
					{
						old_tail.node_ptr->next = new_tail;
						old_tail = tail.exchange(new_tail);
						free_external_count(old_tail);
						data_ptr.release();	
						node_ptr.release();
						break;
					
					}

					old_tail.node_ptr->release_reference();
					
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

			// disable move
			MPMCQueue& operator=(MPMCQueue&& other) = delete;


			~MPMCQueue() 
			{
				node* head_ptr = head.load().node_ptr;
				node* tail_ptr = tail.load().node_ptr;

				// if there's any remaining data items in the queue upon destruction,
				// iterate through the linked list and manage each T* by wrapping it in a unique_ptr.
				// The loop should only extend up until tail's ptr because the class invariants
				// maintain that after a succesful call to push(), tail points to a dummy node
				// without any valid data.
				while(head_ptr != tail_ptr)
				{
					node* ptr_next = head_ptr->next.node_ptr;
					std::unique_ptr<T> head_data{head_ptr->data.load()};
					delete head_ptr;
					head_ptr = ptr_next;

				}
				// no more data to delete, just delete tail's node
				delete head_ptr;
			}


			
			// push will attempt to append a new data item onto the queue in a thread-safe, lock-free manner.
			// appending will construct a T by perfectly-forward along the arguments passed in, to T's constructors.
			// The behavior is undefined unless all arguments are valid.

			// push provides the strong exception safety guarantee
			template<typename... CtorArgs>
			void push(CtorArgs&&... ctor_args)
			{
				std::unique_ptr<node> node_ptr{ new node};
				std::unique_ptr<T> 	  data_ptr{ new T{std::forward<CtorArgs>(ctor_args)...} };
				push_impl(std::move(node_ptr),std::move(data_ptr));
			}
			

			// pop will attempt to remove the next item in the queue, and return a unique_ptr to the data.
			// If the queue is full, a default-constructed unique_ptr<T> is returned, instead.
			// pop will not emit an exception.

			std::unique_ptr<T> pop() noexcept
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

					if(head.compare_exchange_strong(old_head,ptr->next))
					{
						std::unique_ptr<T> data(ptr->data.load());
						free_external_count(old_head);
						return data;
					}

					ptr->release_reference();
				}
			}
			
	};
}
