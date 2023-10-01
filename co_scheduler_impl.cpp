// by wei-mj 20231001

#include "CoroInterface.h"
#include <list>

using namespace coro;

struct CTaskSchedulerImpl :base::CIUObjectImplT<CTaskScheduler>
{
	struct CDeferTaskImpl :base::CIUObjectImplT<CBaseTask>
	{
		int rsp;
		std::string exception;
		struct dele :base::CIUObjectImplT<base::CIUObject>
		{
			base::CIUObjectPtrT<CDeferTaskImpl> impl;
			CTaskContinuation::Ptr continuation;
			std::list<base::CIUObjectPtrT<dele> >::iterator iter;
			dele(CDeferTaskImpl* impl, CTaskContinuation* continuation) :impl(impl), continuation(continuation) {}
		};
		dele* dele_;
		int phase;
		CTaskSchedulerImpl* scheduler;
		CDeferTaskImpl(CTaskSchedulerImpl* scheduler, int rsp, const char*exception, CTaskContinuation* c)
			:rsp(rsp), exception(exception), dele_(nullptr), phase(-1), scheduler(scheduler)
		{
			if (c)
			{
				base::CIUObjectPtrT<dele> d(new dele(this, c));
				scheduler->tasks1.push_front(d);
				d->iter = scheduler->tasks1.begin();
				dele_ = d.operator->();
				phase = 1;
			}
		}
		IUMETHODIMPL_(bool, await_ready()) { return phase == 0 && exception.empty(); }
		IUMETHODIMPL_(int, await_suspend(CTaskContinuation* c))
		{
			if (phase != -1)return 1;
			try
			{
				base::CIUObjectPtrT<dele> d(new dele(this, c));
				dele_ = d.operator->();
				scheduler->tasks.push_back(d);
				phase = 0;
				return 0;
			}
			catch (...)
			{
				return 1;
			}
		}
		IUMETHODIMPL_(int, await_suspend())
		{
			if (phase == 1)
			{
				scheduler->tasks.splice(scheduler->tasks.end(), scheduler->tasks1, dele_->iter);
				phase = 0;
			}
			else return 1;
			return 0;
		}
		IUMETHODIMPL_(int, await_suspend(int rsp, const char* exception))
		{
			if (phase == 1)
			{
				try
				{
					this->rsp = rsp;
					this->exception = exception;
					scheduler->tasks.splice(scheduler->tasks.end(), scheduler->tasks1, dele_->iter);
					phase = 0;
				}
				catch (...)
				{
					return 1;
				}
			}
			else return 1;
			return 0;
		}
		IUMETHODIMPL_(int, await_resume()) { return rsp; }
		IUMETHODIMPL_(void, abort()) { if (phase == 1) { scheduler->tasks1.erase(dele_->iter); phase = 0; } }
	};
	std::list<base::CIUObjectPtrT<CDeferTaskImpl::dele> > tasks1, tasks;
	IUMETHODIMPL_(CBaseTask*, defer(CTaskContinuation* c))
	{
		try
		{
			CBaseTask::Ptr task(new CDeferTaskImpl(this, 0, "", c));
			return task.release();
		}
		catch (...)
		{
			return nullptr;
		}
	}
	IUMETHODIMPL_(CBaseTask*, defer(int rsp, const char* exception))
	{
		try
		{
			CBaseTask::Ptr task(new CDeferTaskImpl(this, rsp, exception, nullptr));
			return task.release();
		}
		catch (...)
		{
			return nullptr;
		}
	}
	IUMETHODIMPL_(int, run(int time_limit = 0)) // time_limit: <= 0 - no limit
	{
		clock_t time_start = (clock_t)-1;
		if (time_limit > 0)
		{
			time_limit = time_limit * CLOCKS_PER_SEC / 1000;
			if (time_limit <= 0)return -1;
			time_start = clock();
		}
		for (bool b = true; b; )
		{
			b = false;
			for (auto iter = tasks.begin(); iter != tasks.end(); )
			{
				auto task = (*iter)->impl;
				if (task->phase == 0 || !task->exception.empty())
				{
					b = true;
					int rsp = task->rsp;
					std::string exception = std::move(task->exception);
					CTaskContinuation::Ptr continuation = (*iter)->continuation;
					iter = tasks.erase(iter);
					int r = rsp;
					const char * e = exception.c_str();
					CTaskContinuation * c = continuation;
					while (c && c->resume(r, e) == 0)
					{
						r = c->rsp();
						e = c->exception();
						c = c->next();
					}
				}
				else ++iter;
				if (time_limit > 0)
				{
					clock_t time_now = clock();
					if (time_start == (clock_t)-1 || time_now == (clock_t)-1)return 1;
					if (time_now - time_start > time_limit)return 0;
				}
			}
		}
		return 0;
	}
};

IUEXPORT(CTaskScheduler*) CreateTaskScheduler()
{
	return CTaskScheduler::Ptr(new CTaskSchedulerImpl).release();
}
