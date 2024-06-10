/*
 * Simple Linux UDP keyboard forwarder
 * Copyright (C) 2024 Calvin Owens <calvin@wbinvd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/input.h>
#include <linux/uinput.h>

#ifndef PORT
#define PORT 31337
#endif

struct wire_event {
	uint16_t type;
	uint16_t code;
	int32_t value;
} __attribute__((packed));

static sig_atomic_t stop;

static void stopper(int sig)
{
	stop = sig;
}

static int get_dgram_connect(const struct sockaddr_in6 *dst)
{
	int fd;

	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd == -1)
		err(1, "can't get dgram socket");

	if (connect(fd, (const struct sockaddr *)dst, sizeof(*dst)))
		err(1, "can't connect");

	return fd;
}

static int get_dgram_bind(void)
{
	struct sockaddr_in6 bindaddr = {
		.sin6_family = AF_INET6,
		.sin6_addr = IN6ADDR_ANY_INIT,
		.sin6_port = htons(PORT),
	};
	int fd, v;

	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd == -1)
		err(1, "can't get listen socket");

	v = 0;
	setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v, sizeof(v));

	if (bind(fd, (const struct sockaddr *)&bindaddr, sizeof(bindaddr)))
		err(1, "can't bind socket");

	return fd;
}

/*
 * TX MODE
 * Send events from a local device via UDP to a remote instance of this program.
 * See https://www.kernel.org/doc/Documentation/input/input.rst
 */
static void tx_main(const char *txdst, const char *txdev)
{
	struct sockaddr_in6 dst = {
		.sin6_family = AF_INET6,
		.sin6_port = htons(PORT),
	};
	int dev_fd, tx_fd;

	if (inet_pton(AF_INET6, txdst, &dst.sin6_addr) != 1) {
		char v4[strlen("::ffff:XXX.XXX.XXX.XXX") + 1];

		snprintf(v4, sizeof(v4), "::ffff:%s", txdst);
		if (inet_pton(AF_INET6, v4, &dst.sin6_addr) != 1)
			errx(1, "bad address '%s'", txdst);
	}

	tx_fd = get_dgram_connect(&dst);
	dev_fd = open(txdev, O_RDONLY);
	if (dev_fd == -1)
		err(1, "can't open %s", txdev);

	while (!stop) {
		struct input_event in_event;
		struct wire_event txevt;
		ssize_t len;

		len = read(dev_fd, &in_event, sizeof(in_event));
		if (len == -1)
			err(1, "bad tx read");

		txevt.type = htons(in_event.type);
		txevt.code = htons(in_event.code);
		txevt.value = htonl(in_event.value);
		if (write(tx_fd, &txevt, sizeof(txevt)) != sizeof(txevt))
			err(1, "bad tx write");
	}

	close(dev_fd);
	close(tx_fd);
}

/*
 * RX MODE
 * Create a virtual input device, and inject events we receive via UDP into it.
 * See https://www.kernel.org/doc/Documentation/input/uinput.rst
 */
static void rx_main(void)
{
	struct uinput_setup usetup = {
		.name = "kfwd",
		.id = {
			.bustype = BUS_USB,
			.vendor = 0x1,
			.product = 0x1,
		},
	};
	int local_fd, rx_fd, i;

	rx_fd = get_dgram_bind();
	local_fd = open("/dev/uinput", O_WRONLY);
	if (local_fd == -1)
		err(1, "can't open /dev/uinput");

	ioctl(local_fd, UI_SET_EVBIT, EV_SYN);
	ioctl(local_fd, UI_SET_EVBIT, EV_REP);
	ioctl(local_fd, UI_SET_EVBIT, EV_KEY);
	for (i = KEY_ESC; i <= KEY_MICMUTE; i++)
		ioctl(local_fd, UI_SET_KEYBIT, i);

	if (ioctl(local_fd, UI_DEV_SETUP, &usetup))
		err(1, "UI_DEV_SETUP");

	if (ioctl(local_fd, UI_DEV_CREATE))
		err(1, "UI_DEV_CREATE");

	while (!stop) {
		struct wire_event rx_evt;
		ssize_t ret;

		ret = read(rx_fd, &rx_evt, sizeof(rx_evt));
		if (ret == -1)
			err(1, "bad rx read");

		if (ret == sizeof(rx_evt)) {
			struct input_event evt = {
				.type = ntohs(rx_evt.type),
				.code = ntohs(rx_evt.code),
				.value = ntohl(rx_evt.value),
			};

			if (write(local_fd, &evt, sizeof(evt)) != sizeof(evt))
				err(1, "bad rx write");
		}
	}

	ioctl(local_fd, UI_DEV_DESTROY);
	close(local_fd);
	close(rx_fd);
}

int main(int argc, char **argv)
{
	struct sigaction ignore_action = {
		.sa_handler = SIG_IGN,
	};
	struct sigaction stop_action = {
		.sa_handler = stopper,
	};

	sigaction(SIGINT, &stop_action, NULL);
	sigaction(SIGTERM, &stop_action, NULL);
	sigaction(SIGPIPE, &ignore_action, NULL);

	if (argc <= 1) {
		rx_main();
		return 0;
	}

	if (argc == 3) {
		tx_main(argv[1], argv[2]);
		return 0;
	}

	puts("RX usage: ./kfwd\nTX usage: ./kfwd <tgt_ip> <dev>");
	return 1;
}
