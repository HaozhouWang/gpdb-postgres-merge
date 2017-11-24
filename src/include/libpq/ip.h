/*-------------------------------------------------------------------------
 *
 * ip.h
 *	  Definitions for IPv6-aware network access.
 *
 * These definitions are used by both frontend and backend code.  Be careful
 * what you include here!
 *
<<<<<<< HEAD
 * Copyright (c) 2003-2010, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/include/libpq/ip.h,v 1.24 2010/02/26 02:01:24 momjian Exp $
=======
 * Copyright (c) 2003-2009, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/include/libpq/ip.h,v 1.21 2009/01/01 17:23:59 momjian Exp $
>>>>>>> b0a6ad70a12b6949fdebffa8ca1650162bf0254a
 *
 *-------------------------------------------------------------------------
 */
#ifndef IP_H
#define IP_H

#include "getaddrinfo.h"
#include "libpq/pqcomm.h"


#ifdef	HAVE_UNIX_SOCKETS
#define IS_AF_UNIX(fam) ((fam) == AF_UNIX)
#else
#define IS_AF_UNIX(fam) (0)
#endif

typedef void (*PgIfAddrCallback) (struct sockaddr * addr,
											  struct sockaddr * netmask,
											  void *cb_data);

extern int pg_getaddrinfo_all(const char *hostname, const char *servname,
				   const struct addrinfo * hintp,
				   struct addrinfo ** result);
extern void pg_freeaddrinfo_all(int hint_ai_family, struct addrinfo * ai);

extern int pg_getnameinfo_all(const struct sockaddr_storage * addr, int salen,
				   char *node, int nodelen,
				   char *service, int servicelen,
				   int flags);

extern int pg_range_sockaddr(const struct sockaddr_storage * addr,
				  const struct sockaddr_storage * netaddr,
				  const struct sockaddr_storage * netmask);

extern int pg_sockaddr_cidr_mask(struct sockaddr_storage * mask,
					  char *numbits, int family);

#ifdef HAVE_IPV6
extern void pg_promote_v4_to_v6_addr(struct sockaddr_storage * addr);
extern void pg_promote_v4_to_v6_mask(struct sockaddr_storage * addr);
#endif

extern int	pg_foreach_ifaddr(PgIfAddrCallback callback, void *cb_data);

#endif   /* IP_H */
