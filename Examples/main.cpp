#include "TaskProcessor.h"
#include <iostream>
//-------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
	TaskProcessor processor;
	processor.Add([](){std::cout << "Hello world" << std::endl; });

	std::getchar();

	return 0;
}
 //-------------------------------------------------------------------------------------------------
