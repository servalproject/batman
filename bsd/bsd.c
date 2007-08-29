/*
 * Copyright (C) 2006, 2007 BATMAN contributors:
 * Stefan Sperling <stsp@stsp.name>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#warning BSD support is known broken - if you compile this on BSD you are expected to fix it :-P

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_tun.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <stdarg.h>

#include "../os.h"
#include "../batman.h"

/* compat.c */

/* Adapted from busybox */
int vdprintf(int d, const char *format, va_list ap)
{
	char buf[1024];
	int len;

	len = vsnprintf(buf, sizeof(buf), format, ap);
	return write(d, buf, len);
}

/* From glibc */
int dprintf(int d, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vdprintf (d, format, arg);
  va_end (arg);

  return done;
}


/* kernel.c */

void set_forwarding(int state)
{
	int mib[4];

	/* FreeBSD allows us to set a boolean sysctl to anything.
	 * Check the value for sanity. */
	if (state < 0 || state > 1) {
		errno = EINVAL;
		err(1, "set_forwarding: %i", state);
	}

	/* "net.inet.ip.forwarding" */
	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_FORWARDING;

	if (sysctl(mib, 4, NULL, 0, (void*)&state, sizeof(state)) == -1)
		err(1, "Cannot change net.inet.ip.forwarding");
}

int get_forwarding(void)
{
	int state;
	size_t len;
	int mib[4];

	/* "net.inet.ip.forwarding" */
	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_FORWARDING;

	len = sizeof(state);

	if (sysctl(mib, 4, &state, &len, NULL, 0) == -1)
		err(1, "Cannot tell if packet forwarding is enabled");

	return state;
}

void set_send_redirects(int32_t state, char* dev)
{
	int mib[4];

	/* FreeBSD allows us to set a boolean sysctl to anything.
	 * Check the value for sanity. */
	if (state < 0 || state > 1) {
		errno = EINVAL;
		err(1, "set_send_redirects: %i", state);
	}

	/* "net.inet.ip.redirect" */
	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_SENDREDIRECTS;

	if (sysctl(mib, 4, NULL, 0, (void*)&state, sizeof(state)) == -1)
		err(1, "Cannot change net.inet.ip.redirect");
}

int32_t get_send_redirects(char *dev)
{
	int state;
	size_t len;
	int mib[4];

	/* "net.inet.ip.redirect" */
	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_SENDREDIRECTS;

	len = sizeof(state);

	if (sysctl(mib, 4, &state, &len, NULL, 0) == -1)
		err(1, "Cannot tell if redirects are enabled");

	return state;
}

void set_rp_filter( int32_t state, char* dev )
{
	/* On BSD, reverse path filtering should be disabled in the firewall. */
	return;
}

int32_t get_rp_filter( char *dev )
{
	/* On BSD, reverse path filtering should be disabled in the firewall. */
	return 0;
}


int8_t bind_to_iface( int32_t udp_recv_sock, char *dev )
{
	/* XXX: Is binding a socket to a specific
	 * interface possible in *BSD?
	 * Possibly via bpf... */
	return 1;
}

int8_t use_kernel_module( char *dev )
{
	return -1;
}

int8_t use_gateway_module( char *dev )
{
	return -1;
}

int32_t get_if_index (struct batman_if *batman_if, struct ifreq *int_req)
{
	return 0;
}

/* route.c */

/* Message structure used to interface the kernel routing table.
 * See route(4) for details on the message passing interface for
 * manipulating the kernel routing table.
 */
struct rt_msg
{
	struct rt_msghdr hdr;
	struct sockaddr_in dest;
	struct sockaddr_in gateway;
	struct sockaddr_in netmask;
};

static inline int32_t n_bits(uint8_t n)
{
	int32_t i, result;

	result = 0;

	if (n > 32)
		n = 32;
	for (i = 0; i < n; i++)
		result |= (0x80000000 >> i);
	
	return result;
}

/* Send routing message msg to the kernel.
 * The kernel's reply is returned in msg. */
static int rt_message(struct rt_msg *msg)
{
	int rt_sock;
	static unsigned int seq = 0;
	ssize_t len;
	pid_t pid;

	rt_sock = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (rt_sock < 0)
		err(1, "Could not open socket to routing table");

	pid = getpid();
	len = 0;
	seq++;

	/* Send message */
	do {
		msg->hdr.rtm_seq = seq;
		len = write(rt_sock, msg, msg->hdr.rtm_msglen);
		if (len < 0) {
			warn("Error sending routing message to kernel");
			return -1;
		}
	} while (len < msg->hdr.rtm_msglen);

	/* Get reply */
	do {
		len = read(rt_sock, msg, sizeof(struct rt_msg));
		if (len < 0)
			err(1, "Error reading from routing socket");
	} while (len > 0 && (msg->hdr.rtm_seq != seq
				|| msg->hdr.rtm_pid != pid));

	if (msg->hdr.rtm_version != RTM_VERSION)
		warn("RTM_VERSION mismatch: compiled with version %i, "
		    "but running kernel uses version %i", RTM_VERSION,
		    msg->hdr.rtm_version);

	/* Check reply for errors. */
	if (msg->hdr.rtm_errno) {
		errno = msg->hdr.rtm_errno;
		return -1;
	}

	return 0;
}

/* Get IP address of a network device (e.g. "tun0"). */
static uint32_t get_dev_addr(char *dev)
{
	int so;
	struct ifreq ifr;
	struct sockaddr_in *addr;

	memset(&ifr, 0, sizeof(ifr));
	
	strlcpy(ifr.ifr_name, dev, IFNAMSIZ);

	so = socket(AF_INET, SOCK_DGRAM, 0);
	if (ioctl(so, SIOCGIFADDR, &ifr, sizeof(ifr)) < 0) {
		perror("SIOCGIFADDR");
		return -1;
	}

	if (ifr.ifr_addr.sa_family != AF_INET) {
		warn("get_dev_addr: got a non-IPv4 interface");
		return -1;
	}

	addr = (struct sockaddr_in*)&ifr.ifr_addr;
	return addr->sin_addr.s_addr;
}

void add_del_route(uint32_t dest, uint8_t netmask, uint32_t router,
		int32_t ifi, char *dev, uint8_t rt_table, int8_t route_type, int8_t del)
{
	char dest_str[16], router_str[16];
	struct rt_msg msg;

	memset(&msg, 0, sizeof(struct rt_msg));

	inet_ntop(AF_INET, &dest, dest_str, sizeof (dest_str));
	inet_ntop(AF_INET, &router, router_str, sizeof (router_str));

	/* Message header */
	msg.hdr.rtm_type = del ? RTM_DELETE : RTM_ADD;
	msg.hdr.rtm_version = RTM_VERSION;
	msg.hdr.rtm_flags = RTF_STATIC | RTF_UP;
	if (netmask == 32)
		msg.hdr.rtm_flags |= RTF_HOST;
	msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	msg.hdr.rtm_msglen = sizeof(struct rt_msg);

	/* Destination and gateway sockaddrs */
	msg.dest.sin_family = AF_INET;
	msg.dest.sin_len = sizeof(struct sockaddr_in);
	msg.gateway.sin_family = AF_INET;
	msg.gateway.sin_len = sizeof(struct sockaddr_in);
	msg.hdr.rtm_flags = RTF_GATEWAY;
	if (dest == router) {
		if (dest == 0) {
			/* Add default route via dev */
			fprintf(stderr, "%s default route via %s\n",
				del ? "Deleting" : "Adding", dev);
			msg.gateway.sin_addr.s_addr = get_dev_addr(dev);
		} else {
			/* Route to dest via default route.
			 * This is a nop. */
			return;
		}
	} else {
		if (router != 0) {
			/* Add route to dest via router */
			msg.dest.sin_addr.s_addr = dest;
			msg.gateway.sin_addr.s_addr = router;
			fprintf(stderr, "%s route to %s/%i via %s\n", del ? "Deleting" : "Adding",
					dest_str, netmask, router_str);
		} else {
			/* Route to dest via default route.
			 * This is a nop. */
			return;
		}
	}

	/* Netmask sockaddr */
	msg.netmask.sin_family = AF_INET;
	msg.netmask.sin_len = sizeof(struct sockaddr_in);
	/* Netmask is passed as decimal value (e.g. 28 for a /28).
	 * So we need to convert it into a bit pattern with n_bits(). */
	msg.netmask.sin_addr.s_addr = htonl(n_bits(netmask));

	if (rt_message(&msg) < 0)
		err(1, "Cannot %s route to %s/%i",
			del ? "delete" : "add", dest_str, netmask);
}

/* tun.c */


/*
 * open_tun_any() opens an available tun device.
 * It returns the file descriptor as return value,
 * or -1 on failure.
 *
 * The human readable name of the device (e.g. "/dev/tun0") is
 * copied into the dev_name parameter. The buffer to hold
 * this string is assumed to be dev_name_size bytes large.
 */
#if defined(__OpenBSD__) || defined(__Darwin__)
int open_tun_any(char *dev_name, size_t dev_name_size)
{
	int i;
	int fd;
	char tun_dev_name[12]; /* 12 = length("/dev/tunxxx\0") */

	for (i = 0; i < sizeof(tun_dev_name); i++)
		tun_dev_name[i] = '\0';

	/* Try opening tun device /dev/tun[0..255] */
	for (i = 0; i < 256; i++) {
		snprintf(tun_dev_name, sizeof(tun_dev_name), "/dev/tun%i", i);
		if ((fd = open(tun_dev_name, O_RDWR)) != -1) {
			if (dev_name != NULL)
				strlcpy(dev_name, tun_dev_name, dev_name_size);
			return fd;
		}
	}
	return -1;
}
#elif defined(__FreeBSD__)
int open_tun_any(char *dev_name, size_t dev_name_size)
{
	int fd;
	struct stat buf;

	/* Open lowest unused tun device */
	if ((fd = open("/dev/tun", O_RDWR)) != -1) {
		fstat(fd, &buf);
		printf("Using %s\n", devname(buf.st_rdev, S_IFCHR));
		if (dev_name != NULL)
			strlcpy(dev_name, devname(buf.st_rdev, S_IFCHR), dev_name_size);
		return fd;
	}
	return -1;
}
#endif

/* Probe for tun interface availability */
int8_t probe_tun()
{
	int fd;
	fd = open_tun_any(NULL, 0);
	if (fd == -1)
		return 0;
	close(fd);
	return 1;
}

int8_t del_dev_tun(int32_t fd)
{
	return close(fd);
}

int8_t add_dev_tun( struct batman_if *batman_if, uint32_t tun_addr, char *tun_dev, size_t tun_dev_size, int32_t *fd, int32_t *ifi )
{
	int so;
	struct ifreq ifr_tun, ifr_if;
	struct tuninfo ti;
	struct sockaddr_in addr;

	/* set up tunnel device */
	memset(&ifr_tun, 0, sizeof(ifr_tun));
	memset(&ifr_if, 0, sizeof(ifr_if));
	memset(&ti, 0, sizeof(ti));

	/* Open tun device. */
	if ((*fd = open_tun_any(tun_dev, tun_dev_size)) < 0) {
		perror("Could not open tun device");
		return -1;
	}

	printf("Using %s\n", tun_dev);

	/* Initialise tuninfo to defaults. */
	if (ioctl(*fd, TUNGIFINFO, &ti) < 0) {
		perror("TUNGIFINFO");
		del_dev_tun(*fd);
		return -1;
	}

	/* Prepare to set IP of this end point of tunnel */
	memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = tun_addr;
	addr.sin_family = AF_INET;
	memcpy(&ifr_tun.ifr_addr, &addr, sizeof(struct sockaddr));

	/* Set name of interface to configure ("tunX") */
	strlcpy(ifr_tun.ifr_name, (tun_dev + strlen("/dev/")), IFNAMSIZ);

	/* Open temporary socket to configure tun interface. */
	so = socket(AF_INET, SOCK_DGRAM, 0);

	/* Set IP of this end point of tunnel */
	if (ioctl(so, SIOCSIFADDR, &ifr_tun, sizeof(ifr_tun)) < 0) {
		perror("SIOCSIFADDR");
		del_dev_tun(*fd);
		return -1;
	}

	/* Get interface flags for tun device */
	if (ioctl(so, SIOCGIFFLAGS, &ifr_tun) < 0) {
		perror("SIOCGIFFLAGS");
		del_dev_tun(*fd);
		return -1;
	}

	/* Set up and running interface flags on tun device. */
	ifr_tun.ifr_flags |= IFF_UP;
	ifr_tun.ifr_flags |= IFF_RUNNING;
	if (ioctl(so, SIOCSIFFLAGS, &ifr_tun) < 0) {
		perror("SIOCSIFFLAGS");
		del_dev_tun(*fd);
		return -1;
	}

	/* get MTU from real interface */
	strlcpy(ifr_if.ifr_name, batman_if->dev, IFNAMSIZ);
	if (ioctl(*fd, SIOCGIFMTU, &ifr_if) < 0) {
		perror("SIOCGIFMTU");
		del_dev_tun(*fd);
		return -1;
	}

	/* set MTU of tun interface: real MTU - 28 */
	if (ifr_if.ifr_mtu < 100) {
		fprintf(stderr, "Warning: MTU smaller than 100 - cannot reduce MTU anymore\n" );
	} else {
		ti.mtu = ifr_if.ifr_mtu - 28;
		if (ioctl(*fd, TUNSIFINFO, &ti) < 0) {
			perror("TUNSIFINFO");
			del_dev_tun(*fd);
			return -1;
		}
	}

	strlcpy(tun_dev, ifr_tun.ifr_name, tun_dev_size);
	return 1;
}

void add_del_rule(uint32_t network, uint8_t netmask, int8_t rt_table,
		uint32_t prio, char *iif, int8_t dst_rule, int8_t del )
{
	fprintf(stderr, "add_del_rule: not implemented\n");
	return;
}

int add_del_interface_rules( int8_t del )
{
	fprintf(stderr, "add_del_interface_rules: not implemented\n");
	return 0;
}

int flush_routes_rules( int8_t rt_table )
{
	fprintf(stderr, "flush_routes_rules: not implemented\n");
	return 0;
}

