#include "TaskProcessor.h"

#define DEFAULT_THREAD_COUNT 2
//-------------------------------------------------------------------------------------------------
TaskProcessor::TaskProcessor():
	m_Running(true)
{
	int count = std::thread::hardware_concurrency();
	m_Threads.resize((count == 0) ? DEFAULT_THREAD_COUNT : count);

	for (auto &t : m_Threads)
		t = std::thread([this]{this->ExecuteLoop(); });
}
//-------------------------------------------------------------------------------------------------
TaskProcessor::~TaskProcessor()
{
	{
		std::lock_guard<Spinlock> lock(m_TasksLock);
		m_Running = false;
	}
	m_Notify.notify_all();
	
	for (auto &t : m_Threads)
		t.join();
}
//-------------------------------------------------------------------------------------------------
void TaskProcessor::ExecuteLoop()
{
	while (true)
	{
		std::unique_lock<Spinlock> lock(m_TasksLock);
		m_Notify.wait(lock, [&](){ return !m_AllTasks.empty() || !m_Running; });

		if (!m_Running && m_AllTasks.empty())
			return;

		auto task = std::move(m_AllTasks.front());
		m_AllTasks.pop_front();
		lock.unlock();

		task();
	}
}
//-------------------------------------------------------------------------------------------------