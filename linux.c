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
#include "batman-specific.h"



void set_rp_filter(int32_t state, char* dev)
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



int32_t get_rp_filter(char *dev)
{
	FILE *f;
	int32_t state = 0;
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



void set_forwarding(int32_t state)
{
	FILE *f;

	if((f = fopen("/proc/sys/net/ipv4/ip_forward", "w")) == NULL)
		return;

	fprintf(f, "%d", state);
	fclose(f);
}



int32_t get_forwarding(void)
{
	FILE *f;
	int32_t state = 0;

	if((f = fopen("/proc/sys/net/ipv4/ip_forward", "r")) == NULL)
		return 0;

	fscanf(f, "%d", &state);
	fclose(f);

	return state;
}



int8_t bind_to_iface( int32_t sock, char *dev ) {

	char *colon_ptr;

	/* if given interface is an alias bind to parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	if ( setsockopt( sock, SOL_SOCKET, SO_BINDTODEVICE, dev, strlen ( dev ) + 1 ) < 0 ) {

		debug_output( 0, "Cannot bind socket to device %s : %s \n", dev, strerror(errno) );
		return -1;

	}

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	return 1;

}


