/*
 * inet.c: Networking functions
 *
 * Copyright (C) 2023 Calvin Owens <jcalvinowens@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "inet.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

static int get_stream_listen(int port)
{
	const struct sockaddr_in6 sa = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(port),
		.sin6_addr = IN6ADDR_ANY_INIT,
	};
	int listen_fd;
	int v;

	listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_fd == -1)
		return -1;

	v = 0;
	setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v, sizeof(v));

	v = 1;
	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)))
		goto err;

	if (bind(listen_fd, (const struct sockaddr *)&sa, sizeof(sa)))
		goto err;

	if (listen(listen_fd, 32))
		goto err;

	return listen_fd;

err:
	close(listen_fd);
	return -1;
}

int get_stream_listen_one(int port)
{
	int fd, nfd;

	fd = get_stream_listen(port);
	if (fd == -1)
		return -1;

	nfd = accept(fd, NULL, NULL);

	close(fd);
	return nfd;
}

int get_stream_connect(const struct sockaddr_in6 *s)
{
	int fd;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1)
		return fd;

	if (connect(fd, (const struct sockaddr *)s, sizeof(*s)))
		goto err;

	return fd;
err:
	close(fd);
	return -1;
}
