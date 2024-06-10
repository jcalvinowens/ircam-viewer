#pragma once

#include <sys/socket.h>
#include <netinet/in.h>

int get_stream_listen_one(int port);
int get_stream_connect(const struct sockaddr_in6 *s);
