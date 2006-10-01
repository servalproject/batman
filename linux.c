/*
 * Copyright (C) 2006 BATMAN contributors:
 * Thomas Lopatic
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <linux/if.h>
#include <netinet/ip.h>
#include <asm/types.h>
#include <linux/if_tun.h>
#include <linux/if_tunnel.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "os.h"
#include "batman.h"

void set_forwarding(int state)
{
	FILE *f;

	if((f = fopen("/proc/sys/net/ipv4/ip_forward", "w")) == NULL)
		return;

	fprintf(f, "%d", state);
	fclose(f);
}

int get_forwarding(void)
{
	FILE *f;
	int state = 0;

	if((f = fopen("/proc/sys/net/ipv4/ip_forward", "r")) == NULL)
		return 0;

	fscanf(f, "%d", &state);
	fclose(f);

	return state;
}

void add_del_route(unsigned int dest, unsigned int router, int del, char *dev, int sock)
{
	struct rtentry route;
	char str1[16], str2[16];
	struct sockaddr_in *addr;

	inet_ntop(AF_INET, &dest, str1, sizeof (str1));
	inet_ntop(AF_INET, &router, str2, sizeof (str2));

	memset(&route, 0, sizeof (struct rtentry));

	addr = (struct sockaddr_in *)&route.rt_dst;

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = dest;

	addr = (struct sockaddr_in *)&route.rt_genmask;

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = 0xffffffff;

	route.rt_flags = RTF_HOST | RTF_UP;

	if (dest != router)
	{
		addr = (struct sockaddr_in *)&route.rt_gateway;

		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = router;

		route.rt_flags |= RTF_GATEWAY;

		output("%s route to %s via %s (%s)\n", del ? "Deleting" : "Adding", str1, str2, dev);
	} else {
		output("%s route to %s via 0.0.0.0 (%s)\n", del ? "Deleting" : "Adding", str1, dev);
	}

	route.rt_metric = 1;

	route.rt_dev = dev;

	if (ioctl(sock, del ? SIOCDELRT : SIOCADDRT, &route) < 0)
	{
		fprintf(stderr, "Cannot %s route to %s via %s: %s\n",
			del ? "delete" : "add", str1, str2, strerror(errno));
	}
}

/* Probe for tun interface availability */
int probe_tun()
{
	int fd;

	if ( ( fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		fprintf( stderr, "Error - could not open '/dev/net/tun' ! Is the tun kernel module loaded ?\n" );
		return 0;

	}

	return 1;

}

int del_ipip_tun( int fd ) {

	if ( ioctl( fd, TUNSETPERSIST, 0 ) < 0 ) {

		perror("TUNSETPERSIST");
		return -1;

	}

	close( fd );

	return 1;

}

int add_ipip_tun( struct batman_if *batman_if, unsigned int dest_addr, char *tun_dev, int *fd ) {

	int tmp_fd;
	struct ifreq ifr;
	struct sockaddr_in addr;

	/* set up tunnel device */
	memset( &ifr, 0, sizeof(ifr) );
	ifr.ifr_flags = IFF_TUN;

	if ( ( *fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		perror("/dev/net/tun");
		return -1;

	}

	if ( ( ioctl( *fd, TUNSETIFF, (void *) &ifr ) ) < 0 ) {

		perror("TUNSETIFF");
		close(*fd);
		return -1;

	}

	if ( ioctl( *fd, TUNSETPERSIST, 1 ) < 0 ) {

		perror("TUNSETPERSIST");
		close(*fd);
		return -1;

	}


	tmp_fd = socket(AF_INET, SOCK_DGRAM, 0);

	if ( tmp_fd < 0 ) {
		fprintf(stderr, "Cannot create send socket: %s", strerror(errno));
		del_ipip_tun( *fd );
		return -1;
	}


	/* set ip of this end point of tunnel */
	memset( &addr, 0, sizeof(addr) );
	addr.sin_addr.s_addr = batman_if->addr.sin_addr.s_addr;
	addr.sin_family = AF_INET;
	memcpy( &ifr.ifr_addr, &addr, sizeof(struct sockaddr) );


	if ( ioctl( tmp_fd, SIOCSIFADDR, &ifr) < 0 ) {

		perror("SIOCSIFADDR");
		del_ipip_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	/* set ip of this remote point of tunnel */
	memset( &addr, 0, sizeof(addr) );
	addr.sin_addr.s_addr = dest_addr;
	addr.sin_family = AF_INET;
	memcpy( &ifr.ifr_addr, &addr, sizeof(struct sockaddr) );

	if ( ioctl( tmp_fd, SIOCSIFDSTADDR, &ifr) < 0 ) {

		perror("SIOCSIFDSTADDR");
		del_ipip_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	if ( ioctl( tmp_fd, SIOCGIFFLAGS, &ifr) < 0 ) {

		perror("SIOCGIFFLAGS");
		del_ipip_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	ifr.ifr_flags |= IFF_UP;
	ifr.ifr_flags |= IFF_RUNNING;

	if ( ioctl( tmp_fd, SIOCSIFFLAGS, &ifr) < 0 ) {

		perror("SIOCSIFFLAGS");
		del_ipip_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	close( tmp_fd );
	strncpy( tun_dev, ifr.ifr_name, IFNAMSIZ );

	return 1;

}


