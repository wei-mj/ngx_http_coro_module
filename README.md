# ngx_http_coro_module

Run C++20 coroutines in [nginx](http://nginx.org/).

## Compiling

	cd /path/to/ngx_http_coro_module
	make
	cd /path/to/nginx-source
	./configure --add-module=/path/to/ngx_http_coro_module --with-ld-opt=-L/path/to/ngx_http_coro_module
	make
	make install

## Sample

To run the sample workflow1.so, add followings to nginx.conf:

	coro_pass 1 /test1;
	coro_pass 2 /test2;
	client_body_in_single_buffer on;
	location = /test1 {
		coro /path/to/ngx_http_coro_module/workflow1.so test1 "out_content_type:text/plain; charset=utf-8";
	}
	location = /test2 {
		coro /path/to/ngx_http_coro_module/workflow1.so test2 "out_content_type:text/plain; charset=utf-8";
	}
	location ^~ /coro1 {
		coro /path/to/ngx_http_coro_module/workflow1.so workflow "out_content_type:text/plain; charset=utf-8";
	}

now start nginx and open these locations in your browser, or try to post something to them.
