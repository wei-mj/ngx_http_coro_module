// by wei-mj 20231001

#ifndef CORO_INTERFACE_INCLUDED
#define CORO_INTERFACE_INCLUDED

#include "IUObject.h"
#include <coroutine>

namespace coro {

	typedef void(*CoroCallback)(void* cb_data, int rsp, const char* exception); // use a C-style callback for providing C-interface conveniently

	struct CTaskContinuation :base::CIUObject
	{
		typedef base::CIUObjectPtrT<CTaskContinuation> Ptr;
		IUMETHOD_(CTaskContinuation*, next()); // get next element from the chain, not a new object
		IUMETHOD_(int, rsp());
		IUMETHOD_(const char*, exception());
		IUMETHOD_(int, resume(int rsp, const char* exception)); // return: 0 - done, resume next; other - suspended, stop resume
	};

	struct CBaseTask :base::CIUObject
	{
		typedef base::CIUObjectPtrT<CBaseTask> Ptr;
		IUMETHOD_(bool, await_ready());
		IUMETHOD_(int, await_suspend(CTaskContinuation*));
		IUMETHOD_(int, await_suspend());
		IUMETHOD_(int, await_suspend(int rsp, const char* exception));
		IUMETHOD_(int, await_resume());
		IUMETHOD_(void, abort());
	};

	struct CTaskScheduler :base::CIUObject
	{
		typedef base::CIUObjectPtrT<CTaskScheduler> Ptr;
		IUEXPORT_(CTaskScheduler*)();
		static Ptr Create();
		typedef CTaskScheduler* RawPtr;
		IUMETHOD_(CBaseTask*, defer(CTaskContinuation*));
		IUMETHOD_(CBaseTask*, defer(int rsp, const char* exception));
		IUMETHOD_(int, run(int time_limit = 0)); // time_limit: <= 0 - no limit
	};

	struct CTaskData :base::CIUObject
	{
	};

	struct CTaskDataImpl :base::CIUObjectImplT<CTaskData>
	{
		typedef base::CIUObjectPtrT<CTaskDataImpl> Ptr;
		bool done;
		int rsp;
		std::string exception;
		CoroCallback cb;
		void* cb_data;
		CTaskContinuation* continuation; // raw pointer here
		CTaskScheduler::RawPtr scheduler; // use raw pointer
		CTaskDataImpl(CTaskScheduler::RawPtr scheduler)
			:done(false), rsp(0), cb(nullptr), cb_data(nullptr), continuation(nullptr), scheduler(scheduler)
		{
		}
	};

	struct CTaskPromise;

	struct CTaskConitnuationImpl :base::CIUObjectImplT<CTaskContinuation>
	{
		CTaskDataImpl::Ptr task_data;
		std::coroutine_handle<CTaskPromise> handle;
		bool handle_destroyed;
		CTaskContinuation::Ptr next_;
		CTaskDataImpl::Ptr prev_task;
		CTaskConitnuationImpl(std::coroutine_handle<CTaskPromise> handle);
		~CTaskConitnuationImpl()
		{
			if (!handle_destroyed)
			{
				handle.destroy();
				if (!task_data->done && task_data->exception.empty())task_data->exception = "destroyed";
			}
		}
		IUMETHODIMPL_(CTaskContinuation*, next()) { return next_; }
		IUMETHODIMPL_(int, rsp()) { return task_data->rsp; }
		IUMETHODIMPL_(const char*, exception()) { return task_data->exception.c_str(); }
		IUMETHODIMPL_(int, resume(int rsp, const char* exception))
		{
			if (*exception == '\0' && task_data->exception.empty())
			{
				if (!handle_destroyed)handle.resume();
			}
			else
			{
				task_data->done = true;
				task_data->rsp = rsp;
				if (*exception != '\0')task_data->exception = exception;
				if (!handle_destroyed)handle.destroy();
			}
			handle_destroyed = true;
			if (task_data->done || !task_data->exception.empty())
			{
				if (task_data->cb)task_data->cb(task_data->cb_data, task_data->rsp, task_data->exception.c_str());
			}
			else return 1;
			return 0;
		}
	};

	template<typename T>
	struct CTaskT :base::CIUObjectT<T>
	{
		bool await_ready() { return impl.operator->() ? impl->await_ready() : true; }
		void await_suspend(std::coroutine_handle<CTaskPromise> handle) { if (impl->await_suspend())if (impl->await_suspend(CTaskContinuation::Ptr(new CTaskConitnuationImpl(handle))))throw std::bad_alloc(); }
		auto await_resume() { return impl.operator->() ? impl->await_resume() : 0; }
	private:
		CBaseTask::Ptr impl;
		struct destructor : base::CIUObjectImplT<base::CIUObject> { CBaseTask::Ptr impl; destructor(CBaseTask::Ptr impl) :impl(impl) {}~destructor() { impl->abort(); } };
		base::CIUObjectPtrT<destructor> destructor_;
	protected:
		CTaskT(CBaseTask::Ptr impl) :impl(impl), destructor_(new destructor(impl)) {}
	};

	struct CDeferTask :CTaskT<CDeferTask>
	{
		static CDeferTask Create(CTaskScheduler::RawPtr scheduler, int rsp, const char* exception) { auto impl = CBaseTask::Ptr(scheduler->defer(rsp, exception), 0); if (!impl.operator->())throw std::bad_alloc(); return CDeferTask(impl); }
	private:
		friend CTaskPromise;
		CDeferTask() :CTaskT<CDeferTask>(CBaseTask::Ptr()) {}
		explicit CDeferTask(CBaseTask::Ptr impl) :CTaskT<CDeferTask>(impl) {}
		static CDeferTask Create(CTaskScheduler::RawPtr scheduler, CTaskContinuation* continuation) { auto impl = CBaseTask::Ptr(scheduler->defer(continuation), 0); if (!impl.operator->())throw std::bad_alloc(); return CDeferTask(impl); }
	};

	struct CTask :base::CIUObjectT<CTask>
	{
		typedef CTaskPromise promise_type;
		CTaskDataImpl::Ptr task_data;
		struct destructor : base::CIUObjectImplT<base::CIUObject>
		{
			CTaskDataImpl::Ptr task_data;
			destructor(CTaskDataImpl::Ptr task_data) :task_data(task_data) {}
			~destructor()
			{
				auto task = task_data;
				if (task->done || !task->exception.empty() || task->cb)return;
				auto p = (CTaskConitnuationImpl*)task->continuation;
				if (p && p->next_.operator->())return;
				for (; p && p->prev_task.operator->(); p = (CTaskConitnuationImpl*)task->continuation)task = p->prev_task;
				if (task->exception.empty())task->exception = "aborted";
				if (p) { p->handle.destroy(); p->handle_destroyed = true; }
			}
		};
		base::CIUObjectPtrT<destructor> destructor_;
		CTask(CTaskDataImpl::Ptr task_data) :task_data(task_data), destructor_(new destructor(task_data)) {}
		bool await_ready() { return task_data->done && task_data->exception.empty(); }
		void await_suspend(std::coroutine_handle<CTaskPromise> handle)
		{
			auto p = (CTaskConitnuationImpl*)task_data->continuation;
			if (task_data->done || !task_data->exception.empty())
				CDeferTask::Create(task_data->scheduler, task_data->rsp, task_data->exception.c_str()).await_suspend(handle);
			else if (!task_data->cb && !p->next_.operator->())
			{
				p->next_ = CTaskContinuation::Ptr(new CTaskConitnuationImpl(handle));
				((CTaskConitnuationImpl*)p->next_.operator->())->prev_task = task_data;
			}
		}
		auto await_resume() { return task_data->rsp; }
		void then(CoroCallback cb, void* cb_data)
		{
			auto p = (CTaskConitnuationImpl*)task_data->continuation;
			if (task_data->done || !task_data->exception.empty())
				cb(cb_data, task_data->rsp, task_data->exception.c_str());
			else if (!task_data->cb && !p->next_.operator->())
			{
				task_data->cb = cb;
				task_data->cb_data = cb_data;
			}
		}
	};

	struct CTaskPromise :base::CIUObjectT<CTaskPromise>
	{
		CTaskDataImpl::Ptr task_data;
		explicit CTaskPromise(CTaskScheduler::RawPtr scheduler) :task_data(new CTaskDataImpl(scheduler)) {}
		template<typename... Args>
		CTaskPromise(CTaskScheduler::RawPtr scheduler, Args&&... args) : task_data(new CTaskDataImpl(scheduler)) {}
		auto get_return_object() { return CTask{ task_data }; }
		auto initial_suspend() { return std::suspend_never{}; }
		auto final_suspend() noexcept { return std::suspend_never{}; }
		void return_void() { task_data->done = true; }
		//void return_value(int rsp) {task_data->done = true;task_data->rsp = rsp;}
		void unhandled_exception()
		{
			task_data->done = true;
			try
			{
				throw;
			}
			catch (std::string&s)
			{
				task_data->exception = s.empty() ? "e" : s;
			}
			catch (std::exception&e)
			{
				task_data->exception = e.what();
				if (task_data->exception.empty())task_data->exception = "e";
			}
			catch (...)
			{
				task_data->exception = "E";
			}
		}
	};

	inline CTaskConitnuationImpl::CTaskConitnuationImpl(std::coroutine_handle<CTaskPromise> handle)
		:task_data(handle.promise().task_data), handle(handle), handle_destroyed(false)
	{
		if (auto p = (CTaskConitnuationImpl*)task_data->continuation)next_ = p->next_;
		if (next_.operator->())((CTaskConitnuationImpl*)next_.operator->())->prev_task = task_data;
		task_data->continuation = this;
	}

}

#endif
