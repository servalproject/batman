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



#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "../os.h"
#include "../batman.h"


#define IOCGETNWDEV 1



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



void set_send_redirects( int32_t state, char* dev ) {

	FILE *f;
	char filename[100], *colon_ptr;

	/* if given interface is an alias use parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	sprintf( filename, "/proc/sys/net/ipv4/conf/%s/send_redirects", dev);

	if((f = fopen(filename, "w")) == NULL)
		return;

	fprintf(f, "%d", state);
	fclose(f);

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

}



int32_t get_send_redirects( char *dev ) {

	FILE *f;
	int32_t state = 0;
	char filename[100], *colon_ptr;

	/* if given interface is an alias use parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	sprintf( filename, "/proc/sys/net/ipv4/conf/%s/send_redirects", dev);

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

	if ( setsockopt( sock, SOL_SOCKET, SO_BINDTODEVICE, dev, strlen( dev ) + 1 ) < 0 ) {

		debug_output( 0, "Cannot bind socket to device %s : %s \n", dev, strerror(errno) );

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

		return -1;

	}

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	return 1;

}



int8_t use_kernel_module( char *dev ) {

	int32_t fd, sock, dummy = 0;
	char *colon_ptr;

	/* if given interface is an alias bind to parent interface */
	if ( ( colon_ptr = strchr( dev, ':' ) ) != NULL )
		*colon_ptr = '\0';

	if ( ( sock = open( "/dev/batman", O_WRONLY ) ) < 0 ) {

		debug_output( 0, "Warning - batman kernel modul interface (/dev/batman) not usable: %s\nThis may decrease the performance of batman!\n", strerror(errno) );

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

		return -1;

	}

	if ( ( fd = ioctl( sock, IOCGETNWDEV, dummy ) ) < 0 ) {

		debug_output( 0, "Warning - can't get batman interface from kernel module: %s\n", strerror(errno) );

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

		close( sock );
		return -1;

	}

	if ( ioctl( fd, strlen( dev ) + 1, dev ) < 0 ) {

		debug_output( 0, "Warning - can't bind batman kernel interface: %s\n", strerror(errno) );

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

		close( sock );
		close( fd );
		return -1;

	}

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	close( sock );

	return fd;

}


