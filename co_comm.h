// by wei-mj 20231001

#ifndef CO_COMM_INCLUDED
#define CO_COMM_INCLUDED

#ifdef __cplusplus
#include "CoroInterface.h"

extern "C" {
#endif

	// implemented by co_comm
	int co_comm_resp_push(void* task, const char* buf, int len);
	int co_comm_resp_done(void* task);
	void *co_comm_ctx_create(void*);
	void  co_comm_ctx_release(void*);
	void *co_comm_workflow_create(void* ctx, void* data);
	void  co_comm_workflow_release(void*);
	void  co_comm_ctx_run(void*);
	void *co_comm_workflow_data(void*);
	void *co_comm_short_req_workflow(void*);
	void *co_comm_resp_workflow(void*);

	// implemented by host
	void co_comm_workflow_done(void *workflow, int rsp, const char* exception);
	int co_comm_short_req(int serv_id, const char* buf, int len, void* task);
	int co_comm_resp(const char* buf, int len, void* task);

#ifdef __cplusplus
}

struct co_comm_ctx :base::CIUObject {
	// implemented by user
	struct co_comm_resp :base::CIUObject {
		IUMETHOD_(void, resp(const char* buf, int len));
	};
	// implemented by co_comm
	struct co_comm_workflow :base::CIUObject {
		IUMETHOD_(void, user_obj(base::CIUObject*));
		IUMETHOD_(base::CIUObject*, user_obj());
		IUMETHOD_(co_comm_ctx*, get_ctx());
		IUMETHOD_(void, done(int rsp, const char* exception));
		IUMETHOD_(coro::CBaseTask*, short_req(int serv_id, const char* buf, int len, co_comm_resp*));
		IUMETHOD_(coro::CBaseTask*, resp(const char* buf, int len));
	};
	IUMETHOD_(void, user_obj(base::CIUObject*));
	IUMETHOD_(base::CIUObject*, user_obj());
	IUMETHOD_(coro::CTaskScheduler*, get_scheduler());
	IUMETHOD_(co_comm_workflow*, create_workflow(void* data));
	// tasks
	struct short_req_task :coro::CTaskT<short_req_task> {
		static short_req_task create(co_comm_workflow* p, int serv_id, const char* buf, int len, co_comm_resp*resp) {
			coro::CBaseTask::Ptr impl(p->short_req(serv_id, buf, len, resp), 0);
			if (!impl.operator->())throw std::bad_alloc();
			return short_req_task(impl);
		}
	private:
		short_req_task(coro::CBaseTask::Ptr impl) :coro::CTaskT<short_req_task>(impl) {}
	};
	static short_req_task short_req(co_comm_workflow* p, int serv_id, const char* buf, int len, co_comm_resp*resp) { return short_req_task::create(p, serv_id, buf, len, resp); }
	struct resp_task :coro::CTaskT<resp_task> {
		static resp_task create(co_comm_workflow* p, const char* buf, int len) {
			coro::CBaseTask::Ptr impl(p->resp(buf, len), 0);
			if (!impl.operator->())throw std::bad_alloc();
			return resp_task(impl);
		}
	private:
		resp_task(coro::CBaseTask::Ptr impl) :coro::CTaskT<resp_task>(impl) {}
	};
	static resp_task send_resp(co_comm_workflow* p, const char* buf, int len) { return resp_task::create(p, buf, len); }
};
#endif

#endif
