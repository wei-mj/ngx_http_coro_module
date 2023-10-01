// by wei-mj 20231001

#ifndef CO_TASK_INCLUDED
#define CO_TASK_INCLUDED

#include "CoroInterface.h"
#include <memory>

namespace coro {

	template<typename ImplT>
	struct CBaseTaskImplT :base::CIUObjectImplT<CBaseTask>
	{
		std::shared_ptr<ImplT> impl;
		template<typename... Args>
		CBaseTaskImplT(Args&&... args) { impl = std::make_shared<ImplT>(std::forward<Args>(args)...); impl->start(impl); }
		IUMETHODIMPL_(bool, await_ready()) { return impl->await_ready(); }
		IUMETHODIMPL_(int, await_suspend(CTaskContinuation* c)) { try { impl->await_suspend(CTaskContinuation::Ptr(c)); return 0; } catch (...) { return 1; } }
		IUMETHODIMPL_(int, await_suspend()) { return 1; }
		IUMETHODIMPL_(int, await_suspend(int rsp, const char* exception)) { return 1; }
		IUMETHODIMPL_(int, await_resume()) { return impl->await_resume(); }
		IUMETHODIMPL_(void, abort()) { impl->abort(); }
	};

	struct BaseTaskImpl :base::CIUObjectT<BaseTaskImpl>
	{
		int status, rsp;
		std::string exception;
		CBaseTask::Ptr defertask;
		CTaskScheduler::RawPtr scheduler;
		bool await_ready() const noexcept { return status == 0 && exception.empty(); }
		void await_suspend(CTaskContinuation::Ptr c) { if (!defertask.operator->())defertask = CBaseTask::Ptr(scheduler->defer(c), 0); }
		void await_suspend(int rsp, const std::string& exception) { if (defertask.operator->())defertask->await_suspend(rsp, exception.c_str()); }
		void abort() { if (defertask.operator->())defertask->abort(); }
		auto await_resume() const { if (!exception.empty())throw std::move(exception); return rsp; }
	protected:
		BaseTaskImpl(CTaskScheduler::RawPtr scheduler) :status(1), rsp(0), scheduler(scheduler) {}
		BaseTaskImpl(const BaseTaskImpl&) = delete;
		BaseTaskImpl& operator=(const BaseTaskImpl&) = delete;
		BaseTaskImpl(BaseTaskImpl&&) = delete;
		BaseTaskImpl& operator=(BaseTaskImpl&&) = delete;
	};

}

#endif
