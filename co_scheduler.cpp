// by wei-mj 20231001

#include "CoroInterface.h"

using namespace coro;

extern "C" CTaskScheduler* CreateTaskScheduler();

CTaskScheduler::Ptr CTaskScheduler::Create()
{
	return CTaskScheduler::Ptr(CreateTaskScheduler(), 0);
}
