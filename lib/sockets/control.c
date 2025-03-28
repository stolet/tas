/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <utils.h>
#include <tas_sockets.h>
#include <tas_ll.h>

#include "internal.h"

static void conn_close(struct flextcp_context *ctx, struct socket *s);

int tas_init(void)
{
  int groupid;
  char *groupidstr;


  groupidstr = getenv("TAS_GROUP");

  if (flextcp_fd_init() != 0) {
    fprintf(stderr, "flextcp_fd_init failed\n");
    return -1;
  }

  if (groupidstr == NULL)
  {
    groupid = 0;
  } else
  {
    groupid = atoi(groupidstr);
  }

  if (flextcp_init(groupid) != 0) {
    fprintf(stderr, "flextcp_init failed\n");
    return -1;
  }

  return 0;
}

int tas_socket(int domain, int type, int protocol)
{
  struct socket *s;
  int fd;
  int nonblock = 0, cloexec = 0;

  if ((type & SOCK_NONBLOCK) == SOCK_NONBLOCK) {
    nonblock = 1;
  }
  if ((type & SOCK_CLOEXEC) == SOCK_CLOEXEC) {
    cloexec = 1;
  }

  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (domain != AF_INET || type != SOCK_STREAM) {
    errno = EINVAL;
    return -1;
  }

  if ((fd = flextcp_fd_salloc(&s)) < 0) {
    return -1;
  }

  s->type = SOCK_SOCKET;
  s->flags = 0;
  flextcp_epoll_sockinit(s);

  if (nonblock) {
    s->flags |= SOF_NONBLOCK;
  }
  if (cloexec) {
    s->flags |= SOF_CLOEXEC;
  }

  flextcp_fd_srelease(fd, s);
  return fd;
}

int tas_close(int sockfd)
{
  struct socket *s;
  struct epoll *ep;

  if (flextcp_fd_slookup(sockfd, &s) == 0) {
    flextcp_fd_close(sockfd);

    /* there is another fd associated with this socket */
    if (s->refcnt != 0) {
      flextcp_fd_srelease(sockfd, s);
      return 0;
    }

    tas_sock_close(s);
  } else if (flextcp_fd_elookup(sockfd, &ep) == 0) {
    flextcp_fd_close(sockfd);

    /* there is another fd associated with this epoll */
    if (ep->refcnt != 0) {
      flextcp_fd_erelease(sockfd, ep);
      return 0;
    }

    /* destroy epoll */
    flextcp_epoll_destroy(ep);
  } else {
    errno = EBADF;
    return -1;
  }

  return 0;
}

int tas_sock_close(struct socket *s)
{
  struct flextcp_context *ctx;

  assert(s->refcnt == 0);

  /* remove from epoll */
  flextcp_epoll_sockclose(s);

  ctx = flextcp_sockctx_get();
  if (s->type == SOCK_CONNECTION) {
    conn_close(ctx, s);
  } else if (s->type == SOCK_SOCKET) {
    free(s);
  } else {
    fprintf(stderr, "TODO: close for non-connections. (leak)\n");
  }

  return 0;
}

/* called with lock on s held, takes over ownership of s struct */
static void conn_close(struct flextcp_context *ctx, struct socket *s)
{
  s->data.connection.status = SOC_CLOSED;

  if ((s->data.connection.st_flags & CSTF_TXCLOSED_ACK) &&
      (s->data.connection.st_flags & CSTF_RXCLOSED))
  {
    /* rx and tx already closed */
    flextcp_sockclose_finish(ctx, s);
  } else if (!(s->data.connection.st_flags & CSTF_TXCLOSED)) {
    if (flextcp_connection_tx_close(ctx, &s->data.connection.c) != 0) {
      fprintf(stderr, "conn_close: flextcp_connection_tx_close failed\n");
      abort();
    }

    s->data.connection.st_flags |= CSTF_TXCLOSED;
  } else {
    /* TX close already initiated, so we're waiting either for TX close to
     * be acknowledged or an RX close to arrive.
     *
     * TODO: send reset if data arrives after this */
  }
  socket_unlock(s);
}

void flextcp_sockclose_finish(struct flextcp_context *ctx, struct socket *s)
{
  /* socket struct will be freed after asynchronous completion */

  if (flextcp_connection_close(ctx, &s->data.connection.c) != 0) {
    fprintf(stderr, "close: flextcp_connection_close failed (unhandled, "
        "results in leak)\n");
    return;
  }

}

int tas_shutdown(int sockfd, int how)
{
  struct socket *s;
  struct flextcp_context *ctx;
  int ret = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  if (s->type != SOCK_CONNECTION) {
    /* TODO: probably the wrong thing for listeners */
    errno = ENOTSOCK;
    ret = -1;
    goto out;
  }

  if (s->data.connection.status != SOC_CONNECTED) {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  if (how != SHUT_WR) {
    fprintf(stderr, "flextcp shutdown: TODO how != SHUT_WR\n");
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  /* already closed for tx -> NOP */
  if ((s->data.connection.st_flags & CSTF_TXCLOSED) == CSTF_TXCLOSED) {
    goto out;
  }

  ctx = flextcp_sockctx_get();
  if (flextcp_connection_tx_close(ctx, &s->data.connection.c) != 0) {
    /* a bit fishy.... */
    errno = ENOBUFS;
    ret = -1;
    goto out;
  }

  s->data.connection.st_flags |= CSTF_TXCLOSED;

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  struct socket *s;
  int ret = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  if (addrlen != sizeof(s->addr) || addr->sa_family != AF_INET) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  memcpy(&s->addr, addr, sizeof(s->addr));
  s->flags |= SOF_BOUND;

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
  struct socket *s;
  int ret = 0, block;
  struct sockaddr_in *sin = (struct sockaddr_in *) addr;
  struct flextcp_context *ctx;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* socket already used */
  if (s->type == SOCK_LISTENER ||
      (s->type == SOCK_CONNECTION && s->data.connection.status == SOC_CONNECTED))
  {
    errno = EISCONN;
    ret = -1;
    goto out;
  }

  /* non blocking socket connecting */
  if (s->type == SOCK_CONNECTION && s->data.connection.status == SOC_CONNECTING) {
    errno = EALREADY;
    ret = -1;
    goto out;
  }

  /* filter out invalid address types */
  if (addrlen != sizeof(s->addr) || addr->sa_family != AF_INET) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  /* open flextcp connection */
  ctx = flextcp_sockctx_get();
  if (flextcp_connection_open(ctx, &s->data.connection.c,
        ntohl(sin->sin_addr.s_addr), ntohs(sin->sin_port)))
  {
    /* TODO */
    errno = ECONNREFUSED;
    ret = -1;
    goto out;
  }

  assert(s->type == SOCK_CONNECTION || s->type == SOCK_SOCKET);
  s->type = SOCK_CONNECTION;
  s->data.connection.status = SOC_CONNECTING;
  s->data.connection.listener = NULL;
  s->data.connection.rx_len_1 = 0;
  s->data.connection.rx_len_2 = 0;
  s->data.connection.ctx = ctx;
  s->data.connection.accepted = 1;

  /* check whether the socket is blocking */
  if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
    /* if non-blocking, just return */
    errno = EINPROGRESS;
    ret = -1;
    goto out;
  } else {
    /* if this is blocking, wait for connection to complete */
    block = 0;
    do {
      socket_unlock(s);
      if (block)
        flextcp_context_wait(ctx, -1);
      block = 1;
      flextcp_sockctx_poll(ctx);
      socket_lock(s);
    } while (s->data.connection.status == SOC_CONNECTING);
  }

  if (s->data.connection.status == SOC_FAILED) {
    /* TODO */
    errno = ECONNREFUSED;
    ret = -1;
    goto out;
  }

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

/** Enqueue accept requests for backlog. Lock on s has to be held. */
static int enqueue_accept(struct flextcp_context *ctx, struct socket *s)
{
  int newfd;
  struct socket *ns;
  struct socket_listen *l = &s->data.listener;
  struct socket_backlog *bl;

  /* try to fill backlog */
  while (l->backlog_num != l->backlog_len) {
    /* allocate socket structure */
    if ((newfd = flextcp_fd_salloc(&ns)) < 0) {
      break;
    }

    ns->type = SOCK_CONNECTION;
    ns->flags = 0;
    ns->data.connection.status = SOC_CONNECTING;
    ns->data.connection.listener = s;
    ns->data.connection.rx_len_1 = 0;
    ns->data.connection.rx_len_2 = 0;
    ns->data.connection.ctx = ctx;
    ns->data.connection.accepted = 0;

    bl = l->backlog + ((l->backlog_next + l->backlog_num) % l->backlog_len);
    bl->s = ns;
    bl->fd = newfd;

    /* send accept request to kernel */
    if (flextcp_listen_accept(ctx, &l->l, &ns->data.connection.c) != 0)
    {
      /* TODO: check error code... */
      flextcp_fd_close(newfd);
      break;
    }

    socket_unlock(ns);
    l->backlog_num = l->backlog_num + 1;
  }

  if (l->backlog_num == 0) {
    errno = ENOBUFS;
    return -1;
  }

  return 0;
}

int tas_listen(int sockfd, int backlog)
{
  struct socket *s;
  struct socket_backlog *bl;
  struct socket_listen *l;
  struct flextcp_context *ctx;
  int block;
  uint32_t flags = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  /* socket already used */
  if (s->type != SOCK_SOCKET) {
    errno = EOPNOTSUPP;
    goto err;
  }

  /* socket not bound */
  /* TODO: technically sohuld probably bind to an ephemeral port */
  if ((s->flags & SOF_BOUND) != SOF_BOUND) {
    errno = EADDRINUSE;
    goto err;
  }

  /* pass on reuseport flags */
  if ((s->flags & SOF_REUSEPORT) == SOF_REUSEPORT) {
    flags |= FLEXTCP_LISTEN_REUSEPORT;
  }

  /* make sure we have a reasonable backlog */
  if (backlog < 8) {
    backlog = 8;
  }

  if ((bl = calloc(backlog, sizeof(*bl))) == NULL) {
    errno = ENOMEM;
    goto err;
  }

  /* open flextcp listener */
  ctx = flextcp_sockctx_get();
  if (flextcp_listen_open(ctx, &s->data.listener.l, ntohs(s->addr.sin_port),
        backlog, flags))
  {
    free (bl);
    errno = ECONNREFUSED;
    goto err_bl;
  }

  s->type = SOCK_LISTENER;
  l = &s->data.listener;
  l->backlog = bl;
  l->backlog_len = backlog;
  l->backlog_next = 0;
  l->backlog_num = 0;
  l->status = SOL_OPENING;
  l->ctx = ctx;

  /* wait for listen to complete */
  block = 0;
  do {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;
    flextcp_sockctx_poll(ctx);
    socket_lock(s);
  } while (s->data.listener.status == SOL_OPENING);

  /* check whether listen failed */
  if (s->data.listener.status == SOL_FAILED) {
    errno = ENOBUFS;
    goto err_bl;
  }

  /* enqueue accepts */
  if (enqueue_accept(ctx, s)) {
    goto err_close;
  }

  flextcp_fd_srelease(sockfd, s);
  return 0;

err_close:
  /* TODO: close listener */
err_bl:
  free(bl);
err:
  flextcp_fd_srelease(sockfd, s);
  return -1;
}

int tas_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
    int flags)
{
  struct socket *s, *ns;
  struct socket_listen *sl;
  struct flextcp_context *ctx;
  struct socket_backlog *bl;
  int ret = 0, nonblock = 0, cloexec = 0, newfd, block;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }
  sl = &s->data.listener;

  /* validate flags */
  if ((flags & SOCK_NONBLOCK) == SOCK_NONBLOCK) {
    nonblock = 1;
  }
  if ((flags & SOCK_CLOEXEC) == SOCK_CLOEXEC) {
    cloexec = 1;
  }

  flags &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (flags != 0) {
    errno = EINVAL;
    ret = -1;
    goto out;
  }

  /* socket is not a listening socket */
  if (s->type != SOCK_LISTENER) {
    errno = EOPNOTSUPP;
    ret = -1;
    goto out;
  }

  ctx = flextcp_sockctx_get();
  block = 0;
  while (1) {
    /* grab next pending accept */
    if (sl->backlog_num == 0 && enqueue_accept(ctx, s)) {
      errno = ENOBUFS;
      ret = -1;
      goto out;
    }
    bl = sl->backlog + sl->backlog_next;
    ns = bl->s;
    newfd = bl->fd;

    socket_lock(ns);
    if (ns->data.connection.status != SOC_CONNECTING) {
      /* connection is ready */
      break;
    } else {
      /* connection is still pending */
      socket_unlock(ns);

      if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
        /* if non-blocking, just return */
        errno = EAGAIN;
        ret = -1;
        goto out;
      } else {
        /* if this is blocking, wait for a connection to complete */
        socket_unlock(s);

        if (block)
          flextcp_context_wait(ctx, -1);
        flextcp_sockctx_poll(ctx);
        block = 1;

        socket_lock(s);
      }
    }
  }

  /* connection is opened now */
  assert(ns->data.connection.status == SOC_CONNECTED);

  if (cloexec)
    ns->flags |= SOF_CLOEXEC;
  if (nonblock)
    ns->flags |= SOF_NONBLOCK;

  /* remove this connection from backlog now */
  sl->backlog_next = (sl->backlog_next + 1) % sl->backlog_len;
  --sl->backlog_num;

  flextcp_fd_srelease(newfd, ns);

  /* refill backlog */
  enqueue_accept(ctx, s);

  /* clear epollin on listening socket if no more connections */
  if (!sl->backlog_num) {
    flextcp_epoll_clear(s, EPOLLIN);
  } else {
    struct socket *next = sl->backlog[sl->backlog_next].s;
    socket_lock(next);
    if (next->data.connection.status == SOC_CONNECTING)
      flextcp_epoll_clear(s, EPOLLIN);
    socket_unlock(next);
  }

  // fill in addr if given
  if(addr != NULL) {
    int r = tas_getpeername(newfd, addr, addrlen);
    assert(r == 0);
  }

  ret = newfd;
out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

/** map: accept  -->  accept4 */
int tas_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  return tas_accept4(sockfd, addr, addrlen, 0);
}

int tas_fcntl(int sockfd, int cmd, ...)
{
  struct socket *s;
  int ret = 0;
  int iarg;
  va_list arg;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  switch (cmd) {
    case F_GETFL:
      /* return flags */
      if ((s->flags & SOF_NONBLOCK) == SOF_NONBLOCK) {
        ret |= O_NONBLOCK;
      }
      break;

    case F_SETFL:
      /* update flags */
      va_start(arg, cmd);
      iarg = va_arg(arg, int);
      va_end(arg);

      /* make sure only supported flags are set */
      if ((iarg & ~(O_NONBLOCK | O_ACCMODE)) != 0) {
        fprintf(stderr, "flextcp fcntl: unsupported flags set (%x)\n",
            iarg);
        /* not sure if that's the right error code */
        errno = EINVAL;
        ret = -1;
        goto out;
      }

      /* set or clear nonblocking socket flags */
      if ((iarg & O_NONBLOCK) == 0) {
        s->flags &= ~SOF_NONBLOCK;
      } else {
        s->flags |= SOF_NONBLOCK;
      }
      break;

    case F_GETFD:
      if ((s->flags & SOF_CLOEXEC) == SOF_CLOEXEC) {
        ret |= FD_CLOEXEC;
      }
      break;

    case F_SETFD:
      /* set/clear cloexec flag */
      va_start(arg, cmd);
      iarg = va_arg(arg, int);
      va_end(arg);

      if ((iarg & ~FD_CLOEXEC) != 0) {
        fprintf(stderr, "flextcp fcntl: setfd unsupported flag (%x)\n",
            iarg);
        errno = EINVAL;
        ret = -1;
        goto out;
      }

      if ((iarg & FD_CLOEXEC) == FD_CLOEXEC)
        s->flags |= SOF_CLOEXEC;
      else
        s->flags &= ~SOF_CLOEXEC;
      break;

    default:
      fprintf(stderr, "flextcp fcntl: unsupported cmd\n");
      errno = EINVAL;
      ret = -1;
      goto out;
  }

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_getsockopt(int sockfd, int level, int optname, void *optval,
    socklen_t *optlen)
{
  struct socket *s;
  int ret = 0, res, len;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  if(level == IPPROTO_TCP && optname == TCP_NODELAY) {
    /* check nodelay flag: always set */
    res = 1;

  } else if(level == SOL_SOCKET &&
      (optname == SO_RCVBUF || optname == SO_SNDBUF))
  {
    /* read receive or transmit buffer size: TODO */
    res = 1024 * 1024;
  } else if (level == SOL_SOCKET && optname == SO_ERROR) {
    /* check socket error */
    if (s->type == SOCK_LISTENER) {
      res = (s->data.listener.status == SOL_OPEN ? 0 : EINPROGRESS);
    } else if (s->type == SOCK_CONNECTION) {
      /* if connection is opening, make sure to poll context to make busy loops
       * work */
      if (s->data.connection.status == SOC_CONNECTING) {
        socket_unlock(s);
        flextcp_sockctx_poll(flextcp_sockctx_get());
        socket_lock(s);
      }

      if (s->data.connection.status == SOC_CONNECTED) {
        res = 0;
      } else if (s->data.connection.status == SOC_CONNECTING) {
        res = EINPROGRESS;
      } else {
        /* TODO */
        res = ECONNREFUSED;
      }
    } else {
      /* TODO */
      res = ENOTSUP;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEPORT) {
    res = !!(s->flags & SOF_REUSEPORT);
  } else if (level == SOL_SOCKET && optname == SO_REUSEADDR) {
    /* reuseaddr is always on */
    res = 1;
  } else if (level == SOL_SOCKET && optname == SO_KEEPALIVE) {
    /* keepalive is always disabled */
    res = 0;
  } else if (level == IPPROTO_TCP && (optname == TCP_KEEPIDLE ||
        optname == TCP_KEEPINTVL || optname == TCP_KEEPCNT)) {
    res = 0;
  } else if (level == SOL_SOCKET && optname == SO_LINGER) {
    fprintf(stderr, "flextcp getsockopt: SO_LINGER not implemented\n");
    errno = ENOPROTOOPT;
    ret = -1;
    goto out;
  } else if (level == SOL_TCP && optname == TCP_MAXSEG) {
    fprintf(stderr, "flextcp getsockopt: warning TCP_MAXSEG hardcoded\n");
    res = 1460;
  } else if (level == SOL_TCP && optname == TCP_INFO) {
    fprintf(stderr, "flextcp getsockopt: warning TCP_INFO hardcoded\n");
    len = TAS_MIN(*optlen, sizeof(struct tcp_info));
    memset(optval, 0, len);
    *optlen = len;
    goto out;
  } else {
    /* unknown option */
    fprintf(stderr, "flextcp getsockopt: unknown level optname combination "
        "(l=%u, o=%u)\n", level, optname);
    errno = ENOPROTOOPT;
    ret = -1;
    goto out;
  }

  /* copy result to optval, truncate if necessary */
  len = TAS_MIN(*optlen, sizeof(res));
  memcpy(optval, &res, len);
  *optlen = res;

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_setsockopt(int sockfd, int level, int optname, const void *optval,
    socklen_t optlen)
{
  struct socket *s;
  int ret = 0, res;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  if(level == IPPROTO_TCP && optname == TCP_NODELAY) {
    /* do nothing */
    if (optlen != sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }
  } else if(level == SOL_SOCKET &&
      (optname == SO_RCVBUF || optname == SO_SNDBUF))
  {
    if (optlen < sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }

    /* we allow "resizing" up to 1MB */
    res = * ((int *) optval);
    if (res <= 1024 * 1024) {
      ret = 0;
    } else {
      errno = ENOMEM;
      ret = -1;
      goto out;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEPORT) {
    if (optlen != sizeof(int)) {
      errno = EINVAL;
      ret = -1;
      goto out;
    }

    if (*(int *) optval != 0) {
      s->flags |= SOF_REUSEPORT;
    } else {
      s->flags &= ~SOF_REUSEPORT;
    }
  } else if (level == SOL_SOCKET && optname == SO_REUSEADDR) {
    /* ignore silently */
  } else if (level == SOL_SOCKET && optname == SO_KEEPALIVE) {
    /* ignore silently */
  } else if (level == SOL_SOCKET && optname == SO_PRIORITY) {
    /* ignore silently */
  } else if (level == IPPROTO_TCP && (optname == TCP_KEEPIDLE ||
       optname == TCP_KEEPINTVL || optname == TCP_KEEPCNT ||
       optname == TCP_DEFER_ACCEPT)) {
    /* ignore silently */
  } else if (level == SOL_SOCKET && optname == SO_LINGER) {
    fprintf(stderr, "flextcp setsockopt: warning SO_LINGER not implemented\n");
  } else {
    /* unknown option */
    fprintf(stderr, "flextcp setsockopt: unknown level optname combination "
        "(l=%u, o=%u)\n", level, optname);
    errno = ENOPROTOOPT;
    ret = -1;
    goto out;
  }

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  struct socket *s;
  int ret = 0;
  socklen_t len;
  struct sockaddr_in sin;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  if ((s->flags & SOF_BOUND) == SOF_BOUND) {
    sin = s->addr;
  } else if (s->type == SOCK_CONNECTION &&
      s->data.connection.status == SOC_CONNECTED)
  {
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    /* FIXME: without breaking abstraction */
    sin.sin_addr.s_addr = htonl(s->data.connection.c.in_local_ip);
    sin.sin_port = htons(s->data.connection.c.local_port);
  } else {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  len = TAS_MIN(*addrlen, sizeof(sin));
  *addrlen = len;
  memcpy(addr, &sin, len);

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  struct socket *s;
  int ret = 0;
  socklen_t len;
  struct sockaddr_in sin;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  tas_sock_move(s);

  /* if not connection or not currently connected then there is no peername */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    ret = -1;
    goto out;
  }

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  /* FIXME: without breaking abstraction */
  sin.sin_addr.s_addr = htonl(s->data.connection.c.in_remote_ip);
  sin.sin_port = htons(s->data.connection.c.remote_port);

  len = TAS_MIN(*addrlen, sizeof(sin));
  *addrlen = len;
  memcpy(addr, &sin,  len);

out:
  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_move_conn(int sockfd)
{
  struct socket *s;
  int ret = 0;

  if (flextcp_fd_slookup(sockfd, &s) != 0) {
    errno = EBADF;
    return -1;
  }

  ret = tas_sock_move(s);

  flextcp_fd_srelease(sockfd, s);
  return ret;
}

int tas_sock_move(struct socket *s)
{
  int ret, block;
  struct flextcp_context *ctx;

  /* if not connection or not currently connected then there is no peername */
  if (s->type != SOCK_CONNECTION ||
      s->data.connection.status != SOC_CONNECTED)
  {
    errno = ENOTCONN;
    return -1;
  }

  /* no-op if already on the right core */
  ctx = flextcp_sockctx_get();
  if (s->data.connection.ctx == ctx) {
    return 0;
  }

  s->data.connection.move_status = INT_MIN;
  if (flextcp_connection_move(ctx, &s->data.connection.c) != 0) {
    /* TODO */
    errno = EINVAL;
    return -1;
  }

  block = 0;
  do {
    socket_unlock(s);
    if (block)
      flextcp_context_wait(ctx, -1);
    block = 1;
    flextcp_sockctx_poll(ctx);
    socket_lock(s);
  } while (s->data.connection.move_status == INT_MIN);
  ret = s->data.connection.move_status;
  if (ret == 0) {
    s->data.connection.ctx = ctx;
  }

  return ret;
}

pid_t tas_fork(pid_t pid, pid_t parent_pid)
{
  struct flextcp_context *ctx;

  /** Thread local variables get copied when a process is forked.
   *  Clear the current context and then get a new context for
   *  the forked child process.
   */

  flextcp_local_context_clear();
  flextcp_sockctx_getfull();

  ctx = flextcp_sockctx_get();
  flextcp_kernel_fork(ctx, pid, parent_pid);

  return pid;
}
