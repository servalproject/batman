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
		goto end;

	fprintf(f, "%d", state);
	fclose(f);

end:
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
		goto end;

	fscanf(f, "%d", &state);
	fclose(f);

end:
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
		goto end;

	fprintf(f, "%d", state);
	fclose(f);

end:
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
		goto end;

	fscanf(f, "%d", &state);
	fclose(f);

end:
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

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

		return -1;

	}

	if ( colon_ptr != NULL )
		*colon_ptr = ':';

	return 1;

}



int8_t use_gateway_module() {

	int32_t fd;


	if ( ( fd = open( "/dev/batgat", O_WRONLY ) ) < 0 ) {

		debug_output( 0, "Warning - batgat kernel modul interface (/dev/batgat) not usable: %s\nThis may decrease the performance of batman!\n", strerror(errno) );

		return -1;

	}

	return fd;

}


