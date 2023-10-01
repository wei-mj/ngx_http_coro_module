// by wei-mj 20231001

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include "co_comm.h"

typedef struct {
    struct {
        ngx_uint_t method;
        ngx_str_t url;
        ngx_str_t out_content_type;
    } upstreams[20];
    void(*workflow)(void*, const u_char*, int);
    ngx_msec_t workflow_timeout;
    ngx_str_t out_content_type;
} ngx_http_coro_loc_conf_t;


typedef struct {
    void *co_comm_ctx;
    void *workflow;
    ngx_chain_t out;
    int done;
} ngx_http_coro_ctx_t;


static char *ngx_http_coro(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_coro_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_coro_subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc);
static void ngx_http_coro_post_handler(ngx_http_request_t *r);
static void *ngx_http_coro_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_coro_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_coro_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_command_t  ngx_http_coro_commands[] = {

    { ngx_string("coro"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_2MORE,
      ngx_http_coro,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("coro_pass"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_2MORE,
      ngx_http_coro_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("coro_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_coro_loc_conf_t, workflow_timeout),
      NULL },

    { ngx_string("coro_out_content_type"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_coro_loc_conf_t, out_content_type),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_coro_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_coro_create_loc_conf,         /* create location configuration */
    ngx_http_coro_merge_loc_conf           /* merge location configuration */
};


ngx_module_t  ngx_http_coro_module = {
    NGX_MODULE_V1,
    &ngx_http_coro_module_ctx,             /* module context */
    ngx_http_coro_commands,                /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


int co_comm_short_req(int backend_id, const char* buf, int len, void* task)
{
    ngx_http_request_t *r = co_comm_workflow_data(co_comm_short_req_workflow(task));
    ngx_http_coro_loc_conf_t *clcf = ngx_http_get_module_loc_conf(r, ngx_http_coro_module);

    ngx_http_request_body_t* request_body = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (request_body == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    request_body->bufs = ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
    if (request_body->bufs == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    request_body->bufs->buf = ngx_create_temp_buf(r->pool, len);
    if (request_body->bufs->buf == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    memcpy(request_body->bufs->buf->pos, buf, len);
    request_body->bufs->buf->last = request_body->bufs->buf->pos + len;

    ngx_http_post_subrequest_t *psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
    if (psr == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    psr->handler = ngx_http_coro_subrequest_post_handler;
    psr->data = ngx_palloc(r->pool, sizeof(void*));
    if (psr->data == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    *(void**)psr->data = task;
    ngx_http_request_t *sr;
    ngx_int_t rc = ngx_http_subrequest(r, &clcf->upstreams[backend_id].url, NULL, &sr, psr, NGX_HTTP_SUBREQUEST_IN_MEMORY);
    if (rc != NGX_OK)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }
    static ngx_str_t http_post_method = { 4, (u_char*)"POST" };
    sr->method = NGX_HTTP_POST;
    sr->method_name = http_post_method;
    sr->headers_in.content_length_n = len;
    if (clcf->upstreams[backend_id].out_content_type.len > 0)sr->headers_out.content_type = clcf->upstreams[backend_id].out_content_type;
    sr->request_body = request_body;

    return 0;
}


int co_comm_resp(const char* buf, int len, void* task)
{
    ngx_http_request_t *r = co_comm_workflow_data(co_comm_resp_workflow(task));
    ngx_http_coro_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_coro_module);

    ctx->out.buf = ngx_create_temp_buf(r->pool, len);
    if (ctx->out.buf == NULL)
    {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return 0;
    }

    memcpy(ctx->out.buf->pos, buf, len);
    ctx->out.buf->last = ctx->out.buf->pos + len;
    ctx->out.buf->last_buf = 1;

    return 0;
}


void co_comm_workflow_done(void *workflow, int rsp, const char* exception)
{
    ngx_http_request_t *r = co_comm_workflow_data(workflow);
    ngx_http_coro_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_coro_module);

    ctx->done = 1;
    if (exception && (rsp || *exception))
    {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "workflow error %d: %s", rsp, exception);
    }
}


static ngx_int_t ngx_http_coro_subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc)
{
    void* task = *(void**)data;
    ngx_http_request_t *pr = r->parent;
    //ngx_http_coro_ctx_t *ctx = ngx_http_get_module_ctx(pr, ngx_http_coro_module);
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "ngx_http_coro_subrequest_post_handler: r=%p pr=%p c.data=%p postponed=%p", r, pr, r->connection->data, pr->postponed?pr->postponed->request:(ngx_http_request_t*)NULL);
    if (r->headers_out.status != NGX_HTTP_OK)
    {
        pr->headers_out.status = r->headers_out.status;
        pr->write_event_handler = ngx_http_coro_post_handler;
        return NGX_OK;
    }
    if (!r->out || !r->out->buf || !(r->out->buf->last > r->out->buf->pos))
    {
        pr->headers_out.status = NGX_HTTP_NO_CONTENT;
        pr->write_event_handler = ngx_http_coro_post_handler;
        return NGX_OK;
    }
if (task){*(void**)data = NULL;
    co_comm_resp_push(task, (const char*)r->out->buf->pos, r->out->buf->last - r->out->buf->pos);
    co_comm_resp_done(task);
}
    pr->headers_out.status = NGX_HTTP_OK;
    pr->write_event_handler = ngx_http_coro_post_handler;
    return NGX_OK;
}

static void ngx_http_coro_post_handler(ngx_http_request_t *r)
{
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "ngx_http_coro_post_handler: pr=%p c.data=%p postponed=%p", r, r->connection->data, r->postponed?r->postponed->request:(ngx_http_request_t*)NULL);
    if (r->headers_out.status != NGX_HTTP_OK)
    {
        ngx_http_finalize_request(r, r->headers_out.status);
        return;
    }
    ngx_http_coro_loc_conf_t *clcf = ngx_http_get_module_loc_conf(r, ngx_http_coro_module);
    ngx_http_coro_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_coro_module);
    ngx_pool_cleanup_t  *cln;

    if (!ctx->co_comm_ctx)
    {
        ctx->co_comm_ctx = co_comm_ctx_create(r);
        if (ctx->co_comm_ctx == NULL)
        {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        cln = ngx_pool_cleanup_add(r->pool, 0);
        if (cln == NULL)
        {
            co_comm_ctx_release(ctx->co_comm_ctx);
            ctx->co_comm_ctx = NULL;
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        cln->handler = co_comm_ctx_release;
        cln->data = ctx->co_comm_ctx;
    }

    if (!ctx->workflow)
    {
        ctx->workflow = co_comm_workflow_create(ctx->co_comm_ctx, r);
        if (ctx->workflow == NULL)
        {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        cln = ngx_pool_cleanup_add(r->pool, 0);
        if (cln == NULL)
        {
            co_comm_workflow_release(ctx->workflow);
            ctx->workflow = NULL;
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        cln->handler = co_comm_workflow_release;
        cln->data = ctx->workflow;

        if (r->request_body && r->request_body->bufs && r->request_body->bufs->buf && r->request_body->bufs->buf->pos && r->request_body->bufs->buf->last > r->request_body->bufs->buf->pos)
        {
            clcf->workflow(ctx->workflow, r->request_body->bufs->buf->pos, r->request_body->bufs->buf->last - r->request_body->bufs->buf->pos);
        }
        else
        {
            clcf->workflow(ctx->workflow, (const u_char*)"", 0);
        }
    }

    co_comm_ctx_run(ctx->co_comm_ctx);
    if (!ctx->done)return;

    if (!ctx->out.buf)
    {
        ngx_http_finalize_request(r, NGX_HTTP_NO_CONTENT);
        return;
    }

    int bodylen = ctx->out.buf->last - ctx->out.buf->pos;
    r->headers_out.content_length_n = bodylen;
    if (clcf->out_content_type.len > 0)r->headers_out.content_type = clcf->out_content_type;
    r->connection->buffered |= NGX_HTTP_WRITE_BUFFERED;
    ngx_int_t ret = ngx_http_send_header(r);
    ret = ngx_http_output_filter(r, &ctx->out);
    ngx_http_finalize_request(r, ret);
}

static char *ngx_http_coro(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_coro_loc_conf_t  *clcf;
    ngx_str_t                 *value;
    ngx_http_core_loc_conf_t  *corelcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_coro_module);
    if (clcf->workflow) {
        return "is duplicate";
    }
    value = cf->args->elts;
    for (ngx_uint_t i = 3; i < cf->args->nelts; ++i) {
        if (!value[i].data || value[i].data[value[i].len]) {
            return NGX_CONF_ERROR;
        }
        const char* p = (const char*)value[i].data;
        const char* q = strchr(p, ':');
        if (q) {
#define MATCH(s) (q-p==strlen(s)&&memcmp(p,s,q-p)==0)
            if (MATCH("out_content_type")) {
                int len = strlen(q+1);
                clcf->out_content_type.data = ngx_pcalloc(cf->pool, len+1);
                if (!clcf->out_content_type.data)
                    return NGX_CONF_ERROR;
                memcpy(clcf->out_content_type.data, q+1, len);
                clcf->out_content_type.len = len;
            }
            else if (MATCH("timeout")) {
                ngx_str_t tmp = ngx_string(q+1);
                clcf->workflow_timeout = ngx_parse_time(&tmp, 0);
                if (clcf->workflow_timeout == (ngx_msec_t) NGX_ERROR) {
                    return "invalid timeout value";
                }
            }
#undef MATCH
        }
    }
    if (!value[1].data || value[1].data[value[1].len]
        || !value[2].data || value[2].data[value[2].len]) {
        return NGX_CONF_ERROR;
    }
    void * pfunlib = dlopen((const char*)value[1].data, RTLD_NOW);
    if (!pfunlib) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "dlopen \"%V\": %s", &value[1], dlerror());
        return NGX_CONF_ERROR;
    }
    clcf->workflow = dlsym(pfunlib, (const char*)value[2].data);
    if (!clcf->workflow) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "dlsym \"%V\": %s", &value[1], dlerror());
        return NGX_CONF_ERROR;
    }
    corelcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    corelcf->handler = ngx_http_coro_handler;
    return NGX_CONF_OK;
}

static ngx_int_t ngx_http_coro_handler(ngx_http_request_t *r)
{
    ngx_http_coro_ctx_t *ctx = ngx_http_get_module_ctx(r, ngx_http_coro_module);
    if (ctx == NULL)
    {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_coro_ctx_t));
        if (ctx == NULL)
        {
            return NGX_ERROR;
        }
        ngx_http_set_ctx(r, ctx, ngx_http_coro_module);
    }
    r->headers_out.status = NGX_HTTP_OK;
    ngx_int_t rc = ngx_http_read_client_request_body(r, ngx_http_coro_post_handler);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE)return rc;
    return NGX_DONE;
}

static void *ngx_http_coro_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_coro_loc_conf_t  *clcf;

    clcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_coro_loc_conf_t));
    if (clcf == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(clcf->upstreams)/sizeof(clcf->upstreams[0]); ++i) {
        clcf->upstreams[i].method = NGX_CONF_UNSET_UINT;
    }
    clcf->workflow_timeout = NGX_CONF_UNSET_MSEC;
    //clcf->out_content_type = { 0, NULL };

    return clcf;
}

static char *ngx_http_coro_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_coro_loc_conf_t *prev = parent;
    ngx_http_coro_loc_conf_t *conf = child;

    for (size_t i = 0; i < sizeof(conf->upstreams)/sizeof(conf->upstreams[0]); ++i) {
        ngx_conf_merge_uint_value(conf->upstreams[i].method, prev->upstreams[i].method, NGX_HTTP_GET);
        ngx_conf_merge_str_value(conf->upstreams[i].url, prev->upstreams[i].url, "");
        ngx_conf_merge_str_value(conf->upstreams[i].out_content_type, prev->upstreams[i].out_content_type, "");
    }
    ngx_conf_merge_msec_value(conf->workflow_timeout, prev->workflow_timeout, 60000);
    ngx_conf_merge_str_value(conf->out_content_type, prev->out_content_type, "");

    return NGX_CONF_OK;
}

static char *ngx_http_coro_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_coro_loc_conf_t  *clcf;
    ngx_str_t                 *value;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_coro_module);
    value = cf->args->elts;
    if (!value[1].data || value[1].data[value[1].len]
        || !value[2].data || value[2].data[value[2].len]) {
        return NGX_CONF_ERROR;
    }
    ngx_int_t backend_id = ngx_atoi(value[1].data, value[1].len);
    if (backend_id < 0 || backend_id >= (ngx_int_t)(sizeof(clcf->upstreams) / sizeof(clcf->upstreams[0]))) {
        return "backend id out of range";
    }
    if (clcf->upstreams[backend_id].url.len > 0) {
        return "is duplicate";
    }
    clcf->upstreams[backend_id].url = value[2];
    for (ngx_uint_t i = 3; i < cf->args->nelts; ++i) {
        if (!value[i].data || value[i].data[value[i].len]) {
            return NGX_CONF_ERROR;
        }
        const char* p = (const char*)value[i].data;
        const char* q = strchr(p, ':');
        if (q) {
#define MATCH(s) (q-p==strlen(s)&&memcmp(p,s,q-p)==0)
            if (MATCH("out_content_type")) {
                int len = strlen(q+1);
                clcf->upstreams[backend_id].out_content_type.data = ngx_pcalloc(cf->pool, len+1);
                if (!clcf->upstreams[backend_id].out_content_type.data)
                    return NGX_CONF_ERROR;
                memcpy(clcf->upstreams[backend_id].out_content_type.data, q+1, len);
                clcf->upstreams[backend_id].out_content_type.len = len;
            }
#undef MATCH
        }
    }
    return NGX_CONF_OK;
}
