// by wei-mj 20231001

#include "co_comm.h"
#include "co_task.h"
#include <set>

using namespace coro;

struct co_comm_ctx_impl :base::CIUObjectImplT<co_comm_ctx> {
	void * data;
	base::CIUObjectPtr user_obj_;
	CTaskScheduler::Ptr scheduler;
	struct co_comm_workflow_impl :base::CIUObjectImplT<co_comm_workflow> {
		void * data;
		base::CIUObjectPtr user_obj_;
		base::CIUObjectPtrT<co_comm_ctx_impl> ctx;
		co_comm_workflow_impl(co_comm_ctx_impl* ctx, void* data) :data(data), ctx(ctx) {}
		IUMETHODIMPL_(void, user_obj(base::CIUObject* obj)) { user_obj_ = base::CIUObjectPtr(obj); }
		IUMETHODIMPL_(base::CIUObject*, user_obj()) { return user_obj_; }
		IUMETHODIMPL_(co_comm_ctx*, get_ctx()) { return ctx; }
		IUMETHODIMPL_(void, done(int rsp, const char* exception)) { co_comm_workflow_done(this, rsp, exception); }
		struct short_req_task_impl :BaseTaskImpl {
			base::CIUObjectPtrT<co_comm_workflow_impl> workflow;
			int serv_id;
			std::string req;
			co_comm_resp* resp;
			short_req_task_impl(co_comm_workflow_impl* workflow, int serv_id, const char* buf, int len, co_comm_resp* resp)
				:BaseTaskImpl(workflow->ctx->scheduler), workflow(workflow), serv_id(serv_id), req(buf, len), resp(resp) {
			}
			void start(std::shared_ptr<short_req_task_impl> self) {
				workflow->short_req_set.insert(self);
				co_comm_short_req(serv_id, req.c_str(), req.length(), this);
			}
		};
		std::set<std::shared_ptr<short_req_task_impl>> short_req_set;
		IUMETHODIMPL_(CBaseTask*, short_req(int serv_id, const char* buf, int len, co_comm_resp* resp)) {
			try {
				return base::CIUObjectPtrT<CBaseTaskImplT<short_req_task_impl>>(new CBaseTaskImplT<short_req_task_impl>(this, serv_id, buf, len, resp)).release();
			}
			catch (...) {
				return nullptr;
			}
		}
		struct resp_task_impl :BaseTaskImpl {
			base::CIUObjectPtrT<co_comm_workflow_impl> workflow;
			std::string resp;
			resp_task_impl(co_comm_workflow_impl* workflow, const char* buf, int len)
				:BaseTaskImpl(workflow->ctx->scheduler), workflow(workflow), resp(buf, len) {
			}
			void start(std::shared_ptr<resp_task_impl> self) {
				::co_comm_resp(resp.c_str(), resp.length(), this);
				status = 0;
			}
		};
		IUMETHODIMPL_(CBaseTask*, resp(const char* buf, int len)) {
			try {
				return base::CIUObjectPtrT<CBaseTaskImplT<resp_task_impl>>(new CBaseTaskImplT<resp_task_impl>(this, buf, len)).release();
			}
			catch (...) {
				return nullptr;
			}
		}
	};
	IUMETHODIMPL_(void, user_obj(base::CIUObject* obj)) { user_obj_ = base::CIUObjectPtr(obj); }
	IUMETHODIMPL_(base::CIUObject*, user_obj()) { return user_obj_; }
	IUMETHODIMPL_(CTaskScheduler*, get_scheduler()) { return scheduler; }
	IUMETHODIMPL_(co_comm_workflow*, create_workflow(void* data)) {
		try {
			return base::CIUObjectPtrT<co_comm_workflow>(new co_comm_workflow_impl(this, data)).release();
		}
		catch (...) {
			return nullptr;
		}
	}
};

int co_comm_resp_push(void* task, const char* buf, int len) {
	co_comm_ctx_impl::co_comm_workflow_impl::short_req_task_impl* p = (co_comm_ctx_impl::co_comm_workflow_impl::short_req_task_impl*)task;
	p->resp->resp(buf, len);
	return 0;
}

int co_comm_resp_done(void* task) {
	co_comm_ctx_impl::co_comm_workflow_impl::short_req_task_impl* p = (co_comm_ctx_impl::co_comm_workflow_impl::short_req_task_impl*)task;
	p->status = 0;
	p->await_suspend(p->rsp, p->exception);
	return 0;
}

void *co_comm_ctx_create(void*data) {
	try {
		base::CIUObjectPtrT<co_comm_ctx_impl> p(new co_comm_ctx_impl);
		p->scheduler = CTaskScheduler::Create();
		p->data = data;
		return p.release();
	}
	catch (...) {
		return nullptr;
	}
}

void  co_comm_ctx_release(void*ctx) {
	((co_comm_ctx*)ctx)->Release();
}

void *co_comm_workflow_create(void* ctx, void* data) {
	return ((co_comm_ctx*)ctx)->create_workflow(data);
}

void  co_comm_workflow_release(void*workflow) {
	((co_comm_ctx::co_comm_workflow*)workflow)->Release();
}

void  co_comm_ctx_run(void*ctx) {
	((co_comm_ctx*)ctx)->get_scheduler()->run();
}

void *co_comm_workflow_data(void*workflow) {
	return ((co_comm_ctx_impl::co_comm_workflow_impl*)workflow)->data;
}

void *co_comm_short_req_workflow(void*task) {
	return ((co_comm_ctx_impl::co_comm_workflow_impl::short_req_task_impl*)task)->workflow;
}

void *co_comm_resp_workflow(void*task) {
	return ((co_comm_ctx_impl::co_comm_workflow_impl::resp_task_impl*)task)->workflow;
}
