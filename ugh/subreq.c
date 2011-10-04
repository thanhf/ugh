#include "ugh.h"

static
void ugh_subreq_wcb_timeout(EV_P_ ev_timer *w, int tev)
{
	ugh_subreq_t *r = aux_memberof(ugh_subreq_t, wev_timeout, w);
	ugh_subreq_del(r, UGH_UPSTREAM_FT_TIMEOUT);
}

static
void ugh_subreq_wcb_connect(EV_P_ ev_io *w, int tev)
{
	ugh_subreq_t *r = aux_memberof(ugh_subreq_t, wev_connect, w);

	if (EV_READ & tev)
	{
		/* log_warn("connection error (tev=%d, r=%p)", tev, (void *) r); */
		ugh_subreq_del(r, UGH_UPSTREAM_FT_ERROR);
		return;
	}

	ev_io_stop(loop, &r->wev_connect);
	ev_io_start(loop, &r->wev_send);
}

static
void ugh_subreq_wcb_send(EV_P_ ev_io *w, int tev)
{
	int rc;

	ugh_subreq_t *r = aux_memberof(ugh_subreq_t, wev_send, w);

	if (0 > (rc = aux_unix_send(w->fd, r->buf_send.data, r->buf_send.size)))
	{
		ugh_subreq_del(r, UGH_UPSTREAM_FT_ERROR);
		return;
	}

	/* log_info("send %d bytes", rc); */

	if (0 == rc)
	{
		return;
	}

	ev_timer_again(loop, &r->wev_timeout);

	r->buf_send.data += rc;
	r->buf_send.size -= rc;

	if (0 == r->buf_send.size)
	{
		ev_io_stop(loop, &r->wev_send);
		ev_io_start(loop, &r->wev_recv);
		return;
	}
}

static
void ugh_subreq_wcb_recv(EV_P_ ev_io *w, int tev)
{
	ugh_subreq_t *r = aux_memberof(ugh_subreq_t, wev_recv, w);

	int nb = aux_unix_recv(w->fd, r->buf_recv.data, r->buf_recv.size);

	if (0 == nb)
	{
		log_info("subreq DONE (nb=%d, %d: %s)", nb, errno, aux_strerror(errno));
		ugh_subreq_del(r, 0);
		return;
	}

	if (0 > nb)
	{
		if (EAGAIN == errno)
		{
			ev_timer_again(loop, &r->wev_timeout);
			return;
		}

		ugh_subreq_del(r, UGH_UPSTREAM_FT_ERROR);
		return;
	}

	r->buf_recv.data += nb;
	r->buf_recv.size -= nb;

	ev_timer_again(loop, &r->wev_timeout);

	if (NULL == r->body.data)
	{
		int status = ugh_parser_subreq(r, r->buf_recv.data - nb, nb);

		if (UGH_AGAIN == status)
		{
			return;
		}

		if (UGH_ERROR == status)
		{
			ugh_subreq_del(r, UGH_UPSTREAM_FT_INVALID_HEADER);
			return;
		}

		ugh_header_t *hdr_content_length = ugh_subreq_header_get_nt(r, "Content-Length");
		r->content_length = atoi(hdr_content_length->value.data);

		if (r->content_length > (UGH_SUBREQ_BUF - (r->request_end - r->buf_recv_data)))
		{
			r->body.data = aux_pool_malloc(r->c->pool, r->content_length);
			r->body.size = r->buf_recv.data - r->request_end;

			memcpy(r->body.data, r->request_end, r->body.size);

			r->buf_recv.data = r->body.data + r->body.size;
			r->buf_recv.size = r->content_length - r->body.size;
		}
		else
		{
			r->body.data = r->request_end;
			r->body.size = r->buf_recv.data - r->request_end;
		}
	}
	else
	{
		r->body.size += nb;
	}

	/* TODO XXX handle Transfer-Encoding: chunked case */
	if (r->body.size == r->content_length)
	{
		uint32_t ft_type = UGH_UPSTREAM_FT_OFF;

		switch (r->status)
		{
		case 400: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 401: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 402: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 403: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 404: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX|UGH_UPSTREAM_FT_HTTP_404; break;
		case 405: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 406: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 407: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 408: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 409: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 410: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 411: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 412: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 413: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 414: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 415: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 416: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 417: ft_type |= UGH_UPSTREAM_FT_HTTP_4XX; break;
		case 500: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX|UGH_UPSTREAM_FT_HTTP_500; break;
		case 501: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX; break;
		case 502: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX|UGH_UPSTREAM_FT_HTTP_502; break;
		case 503: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX|UGH_UPSTREAM_FT_HTTP_503; break;
		case 504: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX|UGH_UPSTREAM_FT_HTTP_504; break;
		case 505: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX; break;
		case 506: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX; break;
		case 507: ft_type |= UGH_UPSTREAM_FT_HTTP_5XX; break;
		}

		ugh_subreq_del(r, ft_type);
	}
}

static
int ugh_subreq_connect(void *data, in_addr_t addr)
{
	ugh_subreq_t *r = (ugh_subreq_t *) data;

	if (INADDR_NONE == addr)
	{
		log_info("ugh_subreq_connect(INADDR_NONE)");
		aux_pool_free(r->c->pool);
		return -1;
	}

	r->addr.sin_addr.s_addr = addr;

	log_info("ugh_subreq_connect(%s:%u)", inet_ntoa(r->addr.sin_addr), ntohs(r->addr.sin_port));

	int sd, rc;

	if (0 > (sd = socket(AF_INET, SOCK_STREAM, 0)))
	{
		log_error("socket(AF_INET, SOCK_STREAM, 0) (%d: %s)", errno, aux_strerror(errno));
		aux_pool_free(r->c->pool);
		return -1;
	}

	if (0 > (rc = aux_set_nonblk(sd, 1)))
	{
		log_error("aux_set_nonblk(%d, 1) (%d: %s)", sd, errno, aux_strerror(errno));
		close(sd);
		aux_pool_free(r->c->pool);
		return -1;
	}

	rc = connect(sd, (struct sockaddr *) &r->addr, sizeof(r->addr));

	if (0 > rc && EINPROGRESS != errno)
	{
		log_error("connect(%d, ...) (%d: %s)", sd, errno, aux_strerror(errno));
		close(sd);
		aux_pool_free(r->c->pool);
		return -1;
	}

	ev_io_init(&r->wev_recv, ugh_subreq_wcb_recv, sd, EV_READ);
	ev_io_init(&r->wev_send, ugh_subreq_wcb_send, sd, EV_WRITE);
	ev_io_init(&r->wev_connect, ugh_subreq_wcb_connect, sd, EV_READ | EV_WRITE);
	ev_timer_init(&r->wev_timeout, ugh_subreq_wcb_timeout, 0, r->timeout);
	ev_timer_again(loop, &r->wev_timeout);
	ev_io_start(loop, &r->wev_connect);

	return 0;
}

ugh_subreq_t *ugh_subreq_add(ugh_client_t *c, char *url, size_t size, int flags)
{
	ugh_subreq_t *r;

	aux_pool_link(c->pool); /* use c->pool for subreq allocations */

	r = (ugh_subreq_t *) aux_pool_calloc(c->pool, sizeof(*r));

	if (NULL == r)
	{
		aux_pool_free(c->pool);
		return NULL;
	}

	r->c = c;

	r->flags = flags;
	r->handle = NULL;

	r->timeout = UGH_CONFIG_SUBREQ_TIMEOUT;

	ugh_parser_url(&r->u, url, size);

	log_info("ugh_subreq_add(%.*s -> host=%.*s, port=%.*s, uri=%.*s, args=%.*s)",
		(int) size, url,
		(int) r->u.host.size, r->u.host.data,
		(int) r->u.port.size, r->u.port.data,
		(int) r->u.uri.size, r->u.uri.data,
		(int) r->u.args.size, r->u.args.data
	);

	r->method = c->method; /* TODO do smth if c->method = POST, but body == NULL */

	return r;
}

int ugh_subreq_set_header(ugh_subreq_t *r, char *key, size_t key_size, char *value, size_t value_size)
{
#if 0
	/* TODO implement */

	void **dest;
	ugh_header_t *vptr;

	dest = JudyLIns(&r->headers_out_hash, aux_hash_key_lc_header(data, size), PJE0);
	if (PJERR == dest) return NULL;

	vptr = aux_pool_malloc(r->c->pool, sizeof(*vptr));
	if (NULL == vptr) return NULL;

	*dest = vptr;

	vptr->key.data = (char *) data;
	vptr->key.size = size;
	vptr->value.data = value_data;
	vptr->value.size = value_size;

	return vptr;
#endif

	return 0;
}

int ugh_subreq_set_body(ugh_subreq_t *r, char *body, size_t body_size)
{
	r->method = UGH_HTTP_POST;

	r->request_body.data = body;
	r->request_body.size = body_size;

	return 0;
}

int ugh_subreq_set_timeout(ugh_subreq_t *r, ev_tstamp timeout)
{
	r->timeout = timeout;

	return 0;
}

int ugh_subreq_run(ugh_subreq_t *r)
{
	/* buffers */

	r->buf_send_data = aux_pool_malloc(r->c->pool, UGH_SUBREQ_BUF);

	if (NULL == r->buf_send_data)
	{
		aux_pool_free(r->c->pool);
		return -1;
	}

	r->buf_send.data = r->buf_send_data;

	r->buf_recv_data = aux_pool_malloc(r->c->pool, UGH_SUBREQ_BUF);

	if (NULL == r->buf_recv_data)
	{
		aux_pool_free(r->c->pool);
		return -1;
	}

	r->buf_recv.data = r->buf_recv_data;
	r->buf_recv.size = UGH_SUBREQ_BUF;

	/* upstream */

	strp u_host = &r->u.host;

	r->upstream = ugh_upstream_get(r->c->s->cfg, r->u.host.data, r->u.host.size);

	if (NULL != r->upstream)
	{
		r->upstream_current = r->upstream->values_curr;
		r->upstream_tries = 1;

		r->upstream->values_curr += 1;
		r->upstream->values_curr %= r->upstream->values_size;

		u_host = &r->upstream->values[r->upstream_current].host;
	}

	/* generate request */

	/* ugh_subreq_gen(r, u_host); */
	ugh_subreq_gen(r, &r->u.host);

	/* resolve host */

	r->resolver_ctx = aux_pool_malloc(r->c->pool, sizeof(ugh_resolver_ctx_t));

	if (NULL == r->resolver_ctx)
	{
		aux_pool_free(r->c->pool);
		return -1;
	}

	r->resolver_ctx->handle = ugh_subreq_connect;
	r->resolver_ctx->data = r;

	if (NULL != r->upstream)
	{
		r->addr.sin_family = AF_INET;
		r->addr.sin_port = r->upstream->values[r->upstream_current].port;

		if (0 > ugh_resolver_addq(r->c->s->resolver, u_host->data, u_host->size, r->resolver_ctx))
		{
			/* ugh_resolver_addq shall call ctx->handle with INADDR_NONE argument in all error cases */
			return -1;
		}
	}
	else
	{
		r->addr.sin_family = AF_INET;
		r->addr.sin_port = htons(strtoul(r->u.port.data, NULL, 10));

		if (0 > ugh_resolver_addq(r->c->s->resolver, r->u.host.data, r->u.host.size, r->resolver_ctx))
		{
			/* ugh_resolver_addq shall call ctx->handle with INADDR_NONE argument in all error cases */
			return -1;
		}
	}

	if ((r->flags & UGH_SUBREQ_WAIT))
	{
		r->c->wait++;
	}

	return 0;
}

int ugh_subreq_gen(ugh_subreq_t *r, strp u_host)
{
	ugh_client_t *c = r->c;

	/* generate request line */

	if (0 == r->u.uri.size)
	{
		r->buf_send.size = snprintf(r->buf_send.data, UGH_SUBREQ_BUF, "%s %.*s%s%.*s %s" CRLF
			, ugh_method_string[r->method]
			, (int) c->uri.size, c->uri.data
			, c->args.size ? "?" : ""
			, (int) c->args.size, c->args.data
			, ugh_version_string[c->version]
		);
	}
	else if (0 == r->u.args.size)
	{
		r->buf_send.size = snprintf(r->buf_send.data, UGH_SUBREQ_BUF, "%s %.*s%s%.*s %s" CRLF
			, ugh_method_string[r->method]
			, (int) r->u.uri.size, r->u.uri.data
			, c->args.size ? "?" : ""
			, (int) c->args.size, c->args.data
			, ugh_version_string[c->version]
		);
	}
	else
	{
		/* TODO what nginx does on proxy_pass host:port/uri?; ? */

		r->buf_send.size = snprintf(r->buf_send.data, UGH_SUBREQ_BUF, "%s %.*s?%.*s %s" CRLF
			, ugh_method_string[r->method]
			, (int) r->u.uri.size, r->u.uri.data
			, (int) r->u.args.size, r->u.args.data
			, ugh_version_string[c->version]
		);
	}

	/* copy original request headers, change host header with new value */

	Word_t idx = 0;
	void **vptr;

	for (vptr = JudyLFirst(c->headers_hash, &idx, PJE0); NULL != vptr;
		 vptr = JudyLNext (c->headers_hash, &idx, PJE0))
	{
		ugh_header_t *h = *vptr;

		if (4 == h->key.size && aux_hash_key_lc_header("Host", 4) == aux_hash_key_lc_header(h->key.data, h->key.size))
		{
			r->buf_send.size += snprintf(r->buf_send.data + r->buf_send.size, UGH_SUBREQ_BUF - r->buf_send.size,
				"Host: %.*s" CRLF, (int) u_host->size, u_host->data);
		}
		else
		{
			r->buf_send.size += snprintf(r->buf_send.data + r->buf_send.size, UGH_SUBREQ_BUF - r->buf_send.size,
				"%.*s: %.*s" CRLF, (int) h->key.size, h->key.data, (int) h->value.size, h->value.data);
		}
	}

	if (NULL != r->request_body.data) /* TODO check if Content-Length header was in original request (and its method was POST) */
	{
		r->buf_send.size += snprintf(r->buf_send.data + r->buf_send.size, UGH_SUBREQ_BUF - r->buf_send.size,
			"Content-Length: %"PRIuMAX CRLF, (uintmax_t) r->request_body.size);

		/* TODO Content-Type */

		r->buf_send.size += snprintf(r->buf_send.data + r->buf_send.size, UGH_SUBREQ_BUF - r->buf_send.size, CRLF);

		r->buf_send.size += aux_cpymsz(r->buf_send.data + r->buf_send.size, r->request_body.data, r->request_body.size);
	}
	else
	{
		r->buf_send.size += snprintf(r->buf_send.data + r->buf_send.size, UGH_SUBREQ_BUF - r->buf_send.size, CRLF);
	}

	/* log_info("ugh_subreq(%.*s)", (int) r->buf_send.size, r->buf_send.data); */

	return 0;
}

int ugh_subreq_del(ugh_subreq_t *r, uint32_t ft_type)
{
	ev_io_stop(loop, &r->wev_recv);
	ev_io_stop(loop, &r->wev_send);
	ev_io_stop(loop, &r->wev_connect);
	ev_timer_stop(loop, &r->wev_timeout);

	close(r->wev_recv.fd);

	switch (ft_type)
	{
	case UGH_UPSTREAM_FT_ERROR:
		log_warn("connection or read/write error on upstream socket (%.*s:%.*s%.*s%s%.*s)"
			, (int) r->u.host.size, r->u.host.data
			, (int) r->u.port.size, r->u.port.data
			, (int) r->u.uri.size, r->u.uri.data
			, r->u.args.size ? "?" : ""
			, (int) r->u.args.size, r->u.args.data
		);
		break;
	case UGH_UPSTREAM_FT_TIMEOUT:
		log_warn("upstream timeout (%.*s:%.*s%.*s%s%.*s)"
			, (int) r->u.host.size, r->u.host.data
			, (int) r->u.port.size, r->u.port.data
			, (int) r->u.uri.size, r->u.uri.data
			, r->u.args.size ? "?" : ""
			, (int) r->u.args.size, r->u.args.data
		);
		break;
	case UGH_UPSTREAM_FT_INVALID_HEADER:
		log_warn("invalid header in upstream response (%.*s:%.*s%.*s%s%.*s)"
			, (int) r->u.host.size, r->u.host.data
			, (int) r->u.port.size, r->u.port.data
			, (int) r->u.uri.size, r->u.uri.data
			, r->u.args.size ? "?" : ""
			, (int) r->u.args.size, r->u.args.data
		);
		break;
	case UGH_UPSTREAM_FT_HTTP_500:
	case UGH_UPSTREAM_FT_HTTP_502:
	case UGH_UPSTREAM_FT_HTTP_503:
	case UGH_UPSTREAM_FT_HTTP_504:
	case UGH_UPSTREAM_FT_HTTP_404:
	case UGH_UPSTREAM_FT_HTTP_5XX:
	case UGH_UPSTREAM_FT_HTTP_4XX:
		log_warn("error status %u in upstream response (%.*s:%.*s%.*s%s%.*s)"
			, r->status
			, (int) r->u.host.size, r->u.host.data
			, (int) r->u.port.size, r->u.port.data
			, (int) r->u.uri.size, r->u.uri.data
			, r->u.args.size ? "?" : ""
			, (int) r->u.args.size, r->u.args.data
		);
		break;
	}

	if (r->upstream && r->upstream_tries <= r->upstream->values_size
		&& (r->c->s->cfg->next_upstream & ft_type))
	{
		strp u_host;

		if (r->upstream_tries < r->upstream->values_size)
		{
			r->upstream_current += 1;
			r->upstream_current %= r->upstream->values_size;
			r->upstream_tries++;

			r->addr.sin_family = AF_INET;
			r->addr.sin_port = r->upstream->values[r->upstream_current].port;

			u_host = &r->upstream->values[r->upstream_current].host;
		}
		else if (0 < r->upstream->backup_values_size)
		{
			r->upstream->backup_values_curr += 1;
			r->upstream->backup_values_curr %= r->upstream->backup_values_size;
			r->upstream_tries++;

			r->addr.sin_family = AF_INET;
			r->addr.sin_port = r->upstream->backup_values[r->upstream->backup_values_curr].port;

			u_host = &r->upstream->backup_values[r->upstream->backup_values_curr].host;
		}
		else
		{
			goto ok;
		}

		r->body.data = NULL;
		r->body.size = 0;

		r->buf_send.data = r->buf_send_data;
		/* r->buf_send.size = 0; */

		r->buf_recv.data = r->buf_recv_data;
		r->buf_recv.size = UGH_SUBREQ_BUF;

		/* ugh_subreq_gen(r, u_host); */

		JudyLFreeArray(&r->headers_hash, PJE0);

		return ugh_resolver_addq(r->c->s->resolver, u_host->data, u_host->size, r->resolver_ctx);
	}

ok:

	if (/* NULL == r->handle &&*/ (r->flags & UGH_SUBREQ_WAIT))
	{
		r->c->wait--;

		if (0 == r->c->wait)
		{
			coro_transfer(&ctx_main, &r->c->ctx);
		}

		/* coro_transfer(&ctx_main, &r->c->ctx); */
	}

	JudyLFreeArray(&r->headers_hash, PJE0);

	aux_pool_free(r->c->pool);

	return 0;
}

void ugh_subreq_wait(ugh_client_t *c)
{
	if (0 < c->wait)
	{
		coro_transfer(&c->ctx, &ctx_main);
	}
}

#if 1
static ugh_header_t ugh_empty_header = {
	{ 0, "" },
	{ 0, "" }
};

ugh_header_t *ugh_subreq_header_get_nt(ugh_subreq_t *r, const char *data)
{
	void **dest;

	dest = JudyLGet(r->headers_hash, aux_hash_key_lc_header_nt(data), PJE0);
	if (PJERR == dest || NULL == dest) return &ugh_empty_header;

	return *dest;
}

ugh_header_t *ugh_subreq_header_get(ugh_subreq_t *r, const char *data, size_t size)
{
	void **dest;

	dest = JudyLGet(r->headers_hash, aux_hash_key_lc_header(data, size), PJE0);
	if (PJERR == dest || NULL == dest) return &ugh_empty_header;

	return *dest;
}

ugh_header_t *ugh_subreq_header_set(ugh_subreq_t *r, const char *data, size_t size, char *value_data, size_t value_size)
{
	void **dest;
	ugh_header_t *vptr;

	dest = JudyLIns(&r->headers_hash, aux_hash_key_lc_header(data, size), PJE0);
	if (PJERR == dest) return NULL;

	vptr = aux_pool_malloc(r->c->pool, sizeof(*vptr));
	if (NULL == vptr) return NULL;

	*dest = vptr;

	vptr->key.data = (char *) data;
	vptr->key.size = size;
	vptr->value.data = value_data;
	vptr->value.size = value_size;

	return vptr;
}
#endif
