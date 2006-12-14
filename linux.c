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

void set_rp_filter(int state, char* dev)
{
	FILE *f;
	char filename[100], *colon_ptr;

	/* if given interface is an alias use parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	sprintf( filename, "/proc/sys/net/ipv4/conf/%s/rp_filter", dev);

	if((f = fopen(filename, "w")) == NULL)
		return;

	fprintf(f, "%d", state);
	fclose(f);

	if ( colon_ptr != NULL )
		*colon_ptr = ':';
}

int get_rp_filter(char *dev)
{
	FILE *f;
	int state = 0;
	char filename[100], *colon_ptr;

	/* if given interface is an alias use parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	sprintf( filename, "/proc/sys/net/ipv4/conf/%s/rp_filter", dev);

	if((f = fopen(filename, "r")) == NULL)
		return 0;

	fscanf(f, "%d", &state);
	fclose(f);

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	return state;
}

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

int bind_to_iface( int udp_recv_sock, char *dev ) {

	char *colon_ptr;

	/* if given interface is an alias bind to parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	if ( setsockopt( udp_recv_sock, SOL_SOCKET, SO_BINDTODEVICE, dev, strlen ( dev ) + 1 ) < 0 ) {

		fprintf( stderr, "Cannot bind socket to device %s : %s \n", dev, strerror(errno) );
		return -1;

	}

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	return 1;

}

void add_del_route( unsigned int dest, unsigned int netmask, unsigned int router, int del, char *dev, int sock ) {

	struct rtentry route;
	char str1[16], str2[16], log_str[100];
	struct sockaddr_in *addr;

	inet_ntop(AF_INET, &dest, str1, sizeof (str1));
	inet_ntop(AF_INET, &router, str2, sizeof (str2));

	memset(&route, 0, sizeof (struct rtentry));

	addr = (struct sockaddr_in *)&route.rt_dst;

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = dest;

	addr = (struct sockaddr_in *)&route.rt_genmask;

	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = ( netmask == 32 ? 0xffffffff : htonl( ~ ( 0xffffffff >> netmask ) ) );

	route.rt_flags = ( netmask == 32 ? ( RTF_HOST | RTF_UP ) : RTF_UP );
	route.rt_metric = 1;

	if ( (dest != router) || ( ( dest == 0 ) && ( router == 0 ) ) )
	{
		addr = (struct sockaddr_in *)&route.rt_gateway;

		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = router;

		if ( ( dest == 0 ) && ( router == 0 ) ) {

			route.rt_metric = 0;

			if ( debug_level == 3 ) {
				printf("%s default route via %s\n", del ? "Deleting" : "Adding", dev);
			} else if ( debug_level == 4 ) {
				output("%s default route via %s\n", del ? "Deleting" : "Adding", dev);
			}

		} else {

			route.rt_flags |= RTF_GATEWAY;

			if ( debug_level == 3 ) {
				printf("%s route to %s/%i via %s (%s)\n", del ? "Deleting" : "Adding", str1, netmask, str2, dev);
			} else if ( debug_level == 4 ) {
				output("%s route to %s/%i via %s (%s)\n", del ? "Deleting" : "Adding", str1, netmask, str2, dev);
			}

		}

	} else {

		if ( debug_level == 3 ) {
			printf("%s route to %s via 0.0.0.0 (%s)\n", del ? "Deleting" : "Adding", str1, dev);
		} else if ( debug_level == 4 ) {
			output("%s route to %s via 0.0.0.0 (%s)\n", del ? "Deleting" : "Adding", str1, dev);
		}

	}

	route.rt_dev = dev;

	if ( ioctl( sock, del ? SIOCDELRT : SIOCADDRT, &route ) < 0 ) {
		snprintf( log_str, sizeof( log_str ), "Error - can't %s route to %s/%i via %s: %s\n", del ? "delete" : "add", str1, netmask, str2, strerror(errno) );
		do_log( log_str, strerror(errno) );
	}

}

/* Probe for tun interface availability */
int probe_tun()
{
	int fd;

	if ( ( fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		do_log( "Error - could not open '/dev/net/tun' ! Is the tun kernel module loaded ?\n", strerror(errno) );
		return 0;

	}

	close( fd );

	return 1;

}

int del_dev_tun( int fd ) {

	if ( ioctl( fd, TUNSETPERSIST, 0 ) < 0 ) {

		do_log( "Error - can't delete tun device: %s\n", strerror(errno) );
		return -1;

	}

	close( fd );

	return 1;

}

int add_dev_tun( struct batman_if *batman_if, unsigned int tun_addr, char *tun_dev, size_t tun_dev_size, int *fd ) {

	int tmp_fd;
	struct ifreq ifr_tun, ifr_if;
	struct sockaddr_in addr;

	/* set up tunnel device */
	memset( &ifr_tun, 0, sizeof(ifr_tun) );
	memset( &ifr_if, 0, sizeof(ifr_if) );
	ifr_tun.ifr_flags = IFF_TUN | IFF_NO_PI;

	if ( ( *fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		do_log( "Error - can't create tun device (/dev/net/tun): %s\n", strerror(errno) );
		return -1;

	}

	if ( ( ioctl( *fd, TUNSETIFF, (void *) &ifr_tun ) ) < 0 ) {

		do_log( "Error - can't create tun device (TUNSETIFF): %s\n", strerror(errno) );
		close(*fd);
		return -1;

	}

	if ( ioctl( *fd, TUNSETPERSIST, 1 ) < 0 ) {

		do_log( "Error - can't create tun device (TUNSETPERSIST): %s\n", strerror(errno) );
		close(*fd);
		return -1;

	}


	tmp_fd = socket(AF_INET, SOCK_DGRAM, 0);

	if ( tmp_fd < 0 ) {
		do_log( "Error - can't create tun device (udp socket): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		return -1;
	}


	/* set ip of this end point of tunnel */
	memset( &addr, 0, sizeof(addr) );
	addr.sin_addr.s_addr = tun_addr;
	addr.sin_family = AF_INET;
	memcpy( &ifr_tun.ifr_addr, &addr, sizeof(struct sockaddr) );


	if ( ioctl( tmp_fd, SIOCSIFADDR, &ifr_tun) < 0 ) {

		do_log( "Error - can't create tun device (SIOCSIFADDR): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}


	if ( ioctl( tmp_fd, SIOCGIFFLAGS, &ifr_tun) < 0 ) {

		do_log( "Error - can't create tun device (SIOCGIFFLAGS): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	ifr_tun.ifr_flags |= IFF_UP;
	ifr_tun.ifr_flags |= IFF_RUNNING;

	if ( ioctl( tmp_fd, SIOCSIFFLAGS, &ifr_tun) < 0 ) {

		do_log( "Error - can't create tun device (SIOCSIFFLAGS): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	/* get MTU from real interface */
	strncpy( ifr_if.ifr_name, batman_if->dev, IFNAMSIZ - 1 );

	if ( ioctl( tmp_fd, SIOCGIFMTU, &ifr_if ) < 0 ) {

		do_log( "Error - can't create tun device (SIOCGIFMTU): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	/* set MTU of tun interface: real MTU - 28 */
	if ( ifr_if.ifr_mtu < 100 ) {

		do_log( "Warning - MTU smaller than 100 -> can't reduce MTU anymore\n", strerror(errno) );

	} else {

		ifr_tun.ifr_mtu = ifr_if.ifr_mtu - 28;

		if ( ioctl( tmp_fd, SIOCSIFMTU, &ifr_tun ) < 0 ) {

			do_log( "Error - can't create tun device (SIOCSIFMTU): %s\n", strerror(errno) );
			del_dev_tun( *fd );
			close( tmp_fd );
			return -1;

		}

	}


	strncpy( tun_dev, ifr_tun.ifr_name, tun_dev_size - 1 );
	close( tmp_fd );

	return 1;

}


