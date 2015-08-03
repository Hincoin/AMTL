#include "TaskProcessor.h"
#include "MTQueue.h"
#include <iostream>
//-------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	TaskProcessor processor;
	processor.Add([](){std::cout << "Hello world" << std::endl; });

	MT::Structs::MTQueue<int> queue;

	std::getchar();

	return 0;
}
 //-------------------------------------------------------------------------------------------------
