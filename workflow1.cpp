// by wei-mj 20231001

#include "co_comm.h"

namespace{
struct co_comm_resp_impl :base::CIUObjectImplT<co_comm_ctx::co_comm_resp>
{
	typedef base::CIUObjectPtrT<co_comm_resp_impl> ptr;
	std::string result;
	IUMETHODIMPL_(void, resp(const char* buf, int len)) { result += std::string(buf, len); }
};
}

static void workflow_done(void* workflow, int rsp, const char* exception)
{
	((co_comm_ctx::co_comm_workflow*)workflow)->done(rsp, exception);
}

static coro::CTask workflow_work(coro::CTaskScheduler::RawPtr scheduler, co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	co_comm_resp_impl::ptr response(new co_comm_resp_impl);
	co_await co_comm_ctx::short_req(workflow, 1, buf, len, response);
	co_await co_comm_ctx::short_req(workflow, 2, buf, len, response);
	co_await co_comm_ctx::send_resp(workflow, response->result.c_str(), response->result.length());
}

static coro::CTask test1_work(coro::CTaskScheduler::RawPtr scheduler, co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	co_await co_comm_ctx::send_resp(workflow, "hello,", 6);
}

static coro::CTask test2_work(coro::CTaskScheduler::RawPtr scheduler, co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	std::string resp = len>0?std::string(buf,len)+"!":std::string("world!");
	if(resp.length()>10)throw "Your name is too long: " + resp;
	co_await co_comm_ctx::send_resp(workflow, resp.c_str(), resp.length());
}

IUEXPORT(void) workflow(co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	workflow_work(workflow->get_ctx()->get_scheduler(), workflow, buf, len).then(workflow_done, workflow);
}

IUEXPORT(void) test1(co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	test1_work(workflow->get_ctx()->get_scheduler(), workflow, buf, len).then(workflow_done, workflow);
}

IUEXPORT(void) test2(co_comm_ctx::co_comm_workflow* workflow, const char* buf, int len)
{
	test2_work(workflow->get_ctx()->get_scheduler(), workflow, buf, len).then(workflow_done, workflow);
}
