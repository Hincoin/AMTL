
//
// Synchronization Primitives
//
// Copyright (c) 2015  Dmitry Popov
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

#pragma once
//-------------------------------------------------------------------------------------------------
#include <atomic>
//-------------------------------------------------------------------------------------------------
class Spinlock
{
public:
	Spinlock()		
	{
		m_locked.clear(); //VS workaround
	}

    void lock()
    {
		int repeatCount = 100;

		while (true)
		{
			if (m_locked.test_and_set(std::memory_order::memory_order_acquire))
				--repeatCount;
			else
				break;
			
			if (repeatCount == 0)
			{
				//usleep(100); //ACHTUNG!!! place delay here
				repeatCount = 100;
			}
		}
    }
    void unlock(){m_locked.clear(std::memory_order::memory_order_release);}
    
    bool tryLock()
	{
		return !m_locked.test_and_set(std::memory_order::memory_order_acquire);
	}

private:
	std::atomic_flag m_locked;
};
//-------------------------------------------------------------------------------------------------
