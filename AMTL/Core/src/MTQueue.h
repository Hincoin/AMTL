//
// Queue
//
// Copyright (c) 2015  Alejandro Lucena
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include <mutex>
#include <memory>

template<class T>
class MTQueue
{
 private:
  constexpr static std::size_t CACHE_LINE_SIZE = 64;
  struct node
  {
    std::shared_ptr<T> data;
    std::unique_ptr<node> next;
  };

  std::unique_ptr<node> head;
  node* tail;

  std::mutex head_mut alignas(CACHE_LINE_SIZE);
  std::mutex tail_mut alignas(CACHE_LINE_SIZE);

  /*
    get_tail() serves as a convenience function for taking a short lived
    lock and returning the current value of tail. Since this is scoped,
    the lock will be released almost immediately.

    get_tail() provides the strong exception safety guarantee
  */
  node* get_tail()
  {
    std::lock_guard<std::mutex> lock(tail_mut);
    return tail;
  }

  /*
    remove_head() serves as a convenience function 
    for popping an element off the queue. If the queue
    is empty, returns nullptr. Otherwise, returns a 
    unique_ptr to the front of the queue and readjusts
    the head pointer.

    remove_head provides the strong exception safety guarantee
  */
  std::unique_ptr<node> remove_head()
    {
      std::lock_guard<std::mutex> lock(head_mut);
      if(head.get() == get_tail())
	{
	  return nullptr;
	}

      std::unique_ptr<node> old_head = std::move(head);
      head = std::move(old_head->next);
      return old_head;
    }

 public:
 MTQueue() : head(new node) , tail(head.get()) {}
  MTQueue(const MTQueue&) = delete;
  MTQueue& operator=(const MTQueue&) = delete;


  /*
    push accepts a forwarding-reference to U and attempts to construct
    a shared_ptr<T> with the argument. If a T cannot be constructed with the given U
    this function will fail to compile.

    This function pushes to the tail of the queue in a thread-safe manner while still
    allowing other threads to make progress.

    push() provides the strong exception safety guarantee

  */
  template<typename U>
    void push(U val)
    {
      std::shared_ptr<T>    new_data(std::make_shared<T>(std::move(val)));
      std::unique_ptr<node> new_node(new node);
      node*   new_tail(new_node.get()); 
      std::lock_guard<std::mutex> lock(tail_mut);
      
      tail->data = std::move(new_data);
      tail->next = std::move(new_node);
      tail = new_tail;
    }

  /*
    pop() attempts to remove the front element from the queue.
    If the queue is empty, a default-constructed shared_ptr is returned.
    Otherwise, a shared_ptr to the data is returned.

    pop() provides the strong exception safety guarantee

  */
  std::shared_ptr<T> pop()
    {
      auto old_head = remove_head();
      return old_head ? old_head->data : std::shared_ptr<T>{} ;
    }

};
