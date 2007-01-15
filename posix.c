/*
 * Copyright (C) 2006 BATMAN contributors:
 * Thomas Lopatic, Marek Lindner
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
#include <net/if.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>

#include "os.h"
#include "batman-specific.h"
#include "list.h"
#include "allocate.h"

#define BAT_LOGO_PRINT(x,y,z) printf( "\x1B[%i;%iH%c", y + 1, x, z )                      /* write char 'z' into column 'x', row 'y' */
#define BAT_LOGO_END(x,y) printf("\x1B[8;0H");fflush(NULL);bat_wait( x, y );              /* end of current picture */


extern struct vis_if vis_if;

static struct timeval start_time;
static short stop;


static void get_time_internal(struct timeval *tv)
{
	int sec;
	int usec;
	gettimeofday(tv, NULL);

	sec = tv->tv_sec - start_time.tv_sec;
	usec = tv->tv_usec - start_time.tv_usec;

	if (usec < 0)
	{
		sec--;
		usec += 1000000;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

unsigned int get_time(void)
{
	struct timeval tv;

	get_time_internal(&tv);

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void output(char *format, ...)
{
	va_list args;

	printf("[%10u] ", get_time());

	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}



void do_log( char *description, char *error_msg ) {

	if ( debug_level == 0 ) {

		syslog( LOG_ERR, description, error_msg );

	} else {

		printf( description, error_msg );

	}

}



/* batman animation */
void sym_print( char x, char y, char *z ) {

	char i = 0, Z;

	do{

		BAT_LOGO_PRINT( 25 + (int)x + (int)i, (int)y, z[(int)i] );

		switch ( z[(int)i] ) {

			case 92:
				Z = 47;   // "\" --> "/"
				break;

			case 47:
				Z = 92;   // "/" --> "\"
				break;

			case 41:
				Z = 40;   // ")" --> "("
				break;

			default:
				Z = z[(int)i];
				break;

		}

		BAT_LOGO_PRINT( 24 - (int)x - (int)i, (int)y, Z );
		i++;

	} while( z[(int)i - 1] );

	return;

}



void bat_wait( int T, int t ) {

	struct timeval time;

	time.tv_sec = T;
	time.tv_usec = ( t * 10000 );

	select(0, NULL, NULL, NULL, &time);

	return;

}



int print_animation( void ) {

	system( "clear" );
	BAT_LOGO_END( 0, 50 );

	sym_print( 0, 3, "." );
	BAT_LOGO_END( 1, 0 );

	sym_print( 0, 4, "v" );
	BAT_LOGO_END( 0, 20 );

	sym_print( 1, 3, "^" );
	BAT_LOGO_END( 0, 20 );

	sym_print( 1, 4, "/" );
	sym_print( 0, 5, "/" );
	BAT_LOGO_END( 0, 10 );

	sym_print( 2, 3, "\\" );
	sym_print( 2, 5, "/" );
	sym_print( 0, 6, ")/" );
	BAT_LOGO_END( 0, 10 );

	sym_print( 2, 3, "_\\" );
	sym_print( 4, 4, ")" );
	sym_print( 2, 5, " /" );
	sym_print( 0, 6, " )/" );
	BAT_LOGO_END( 0, 10 );

	sym_print( 4, 2, "'\\" );
	sym_print( 2, 3, "__/ \\" );
	sym_print( 4, 4, "   )" );
	sym_print( 1, 5, "   " );
	sym_print( 2, 6, "   /" );
	sym_print( 3, 7, "\\" );
	BAT_LOGO_END( 0, 15 );

	sym_print( 6, 3, " \\" );
	sym_print( 3, 4, "_ \\   \\" );
	sym_print( 10, 5, "\\" );
	sym_print( 1, 6, "          \\" );
	sym_print( 3, 7, " " );
	BAT_LOGO_END( 0, 20 );

	sym_print( 7, 1, "____________" );
	sym_print( 7, 3, " _   \\" );
	sym_print( 3, 4, "_      " );
	sym_print( 10, 5, " " );
	sym_print( 11, 6, " " );
	BAT_LOGO_END( 0, 25 );

	sym_print( 3, 1, "____________    " );
	sym_print( 1, 2, "'|\\   \\" );
	sym_print( 2, 3, " /         " );
	sym_print( 3, 4, " " );
	BAT_LOGO_END( 0, 25 );

	sym_print( 3, 1, "    ____________" );
	sym_print( 1, 2, "    '\\   " );
	sym_print( 2, 3, "__/  _   \\" );
	sym_print( 3, 4, "_" );
	BAT_LOGO_END( 0, 35 );

	sym_print( 7, 1, "            " );
	sym_print( 7, 3, " \\   " );
	sym_print( 5, 4, "\\    \\" );
	sym_print( 11, 5, "\\" );
	sym_print( 12, 6, "\\" );
	BAT_LOGO_END( 0 ,35 );

	return 0;

}



void *client_to_gw_tun( void *arg ) {

	struct gw_node *gw_node = (struct gw_node *)arg;
	struct batman_if *curr_gateway_batman_if;
	struct sockaddr_in gw_addr, my_addr, sender_addr;
	struct timeval tv;
	int res, max_sock, status, buff_len, curr_gateway_tcp_sock, curr_gateway_tun_sock, curr_gateway_tun_fd, server_keep_alive_timeout;
	unsigned int addr_len, curr_gateway_ip;
	char curr_gateway_tun_if[IFNAMSIZ], keep_alive_string[] = "ping\0";
	unsigned char buff[1500];
	fd_set wait_sockets, tmp_wait_sockets;


	curr_gateway_ip = gw_node->orig_node->orig;
	curr_gateway_batman_if = gw_node->orig_node->batman_if;
	addr_len = sizeof (struct sockaddr_in);

	memset( &gw_addr, 0, sizeof(struct sockaddr_in) );
	memset( &my_addr, 0, sizeof(struct sockaddr_in) );

	gw_addr.sin_family = AF_INET;
	gw_addr.sin_port = htons(PORT + 1);
	gw_addr.sin_addr.s_addr = curr_gateway_ip;

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(PORT + 1);
	my_addr.sin_addr.s_addr = curr_gateway_batman_if->addr.sin_addr.s_addr;


	/* connect to server (ask permission) */
	if ( ( curr_gateway_tcp_sock = socket(PF_INET, SOCK_STREAM, 0) ) < 0 ) {

		do_log( "Error - can't create tcp socket: %s\n", strerror(errno) );
		curr_gateway = NULL;
		return NULL;

	}

	if ( connect ( curr_gateway_tcp_sock, (struct sockaddr *)&gw_addr, sizeof(struct sockaddr) ) < 0 ) {

		do_log( "Error - can't connect to gateway: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );

		gw_node->last_failure = get_time();
		gw_node->unavail_factor++;

		curr_gateway = NULL;
		return NULL;

	}

	server_keep_alive_timeout = get_time();


	/* connect to server (establish udp tunnel) */
	if ( ( curr_gateway_tun_sock = socket(PF_INET, SOCK_DGRAM, 0) ) < 0 ) {

		do_log( "Error - can't create udp socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		curr_gateway = NULL;
		return NULL;

	}

	if ( bind( curr_gateway_tun_sock, (struct sockaddr *)&my_addr, sizeof (struct sockaddr_in) ) < 0) {

		do_log( "Error - can't bind tunnel socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		return NULL;

	}


	if ( add_dev_tun( curr_gateway_batman_if, curr_gateway_batman_if->addr.sin_addr.s_addr, curr_gateway_tun_if, sizeof(curr_gateway_tun_if), &curr_gateway_tun_fd ) > 0 ) {

		add_del_route( 0, 0, 0, 0, curr_gateway_tun_if, curr_gateway_batman_if->udp_send_sock );

	} else {

		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		return NULL;

	}


	FD_ZERO(&wait_sockets);
	FD_SET(curr_gateway_tcp_sock, &wait_sockets);
	FD_SET(curr_gateway_tun_sock, &wait_sockets);
	FD_SET(curr_gateway_tun_fd, &wait_sockets);

	max_sock = curr_gateway_tcp_sock;
	if ( curr_gateway_tun_sock > max_sock )
		max_sock = curr_gateway_tun_sock;
	if ( curr_gateway_tun_fd > max_sock )
		max_sock = curr_gateway_tun_fd;

	while ( ( !is_aborted() ) && ( curr_gateway != NULL ) ) {


		if ( server_keep_alive_timeout + 30000 < get_time() ) {

			server_keep_alive_timeout = get_time();

			if ( write( curr_gateway_tcp_sock, keep_alive_string, sizeof( keep_alive_string ) ) < 0 ) {

				if ( debug_level == 3 )
					printf( "server_keepalive failed: no connect to server\n" );

				gw_node->last_failure = get_time();
				gw_node->unavail_factor++;

				break;

			}

		}


		tv.tv_sec = 0;
		tv.tv_usec = 250;

		tmp_wait_sockets = wait_sockets;

		res = select(max_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv);

		if ( res > 0 ) {

			/* tcp message from server */
			if ( FD_ISSET( curr_gateway_tcp_sock, &tmp_wait_sockets ) ) {

				status = read( curr_gateway_tcp_sock, buff, sizeof( buff ) );

				if ( status > 0 ) {

					if ( debug_level == 3 )
						printf( "server message ?\n" );

				} else if ( status < 0 ) {

					if ( debug_level == 3 )
						printf( "Cannot read message from gateway: %s\n", strerror(errno) );

					break;

				} else if (status == 0) {

					if ( debug_level == 3 )
						printf( "Gateway closed connection - timeout ?\n" );

					gw_node->last_failure = get_time();
					gw_node->unavail_factor++;

					break;

				}

			/* udp message (tunnel data) */
			} else if ( FD_ISSET( curr_gateway_tun_sock, &tmp_wait_sockets ) ) {

				if ( ( buff_len = recvfrom( curr_gateway_tun_sock, buff, sizeof( buff ), 0, (struct sockaddr *)&sender_addr, &addr_len ) ) < 0 ) {

					do_log( "Error - can't receive packet: %s\n", strerror(errno) );

				} else {

					if ( write( curr_gateway_tun_fd, buff, buff_len ) < 0 ) {

						do_log( "Error - can't write packet: %s\n", strerror(errno) );

					}

				}

			} else if ( FD_ISSET( curr_gateway_tun_fd, &tmp_wait_sockets ) ) {

				if ( ( buff_len = read( curr_gateway_tun_fd, buff, sizeof( buff ) ) ) < 0 ) {

					do_log( "Error - couldn't read data: %s\n", strerror(errno) );

				} else {

					if ( sendto(curr_gateway_tun_sock, buff, buff_len, 0, (struct sockaddr *)&gw_addr, sizeof (struct sockaddr_in) ) < 0 ) {
						do_log( "Error - can't send to gateway: %s\n", strerror(errno) );
					}

				}

			}

		} else if ( ( res < 0 ) && (errno != EINTR) ) {

			do_log( "Error - can't select: %s\n", strerror(errno) );
			break;

		}

	}

	/* cleanup */
	add_del_route( 0, 0, 0, 1, curr_gateway_tun_if, curr_gateway_batman_if->udp_send_sock );

	close( curr_gateway_tcp_sock );
	close( curr_gateway_tun_sock );

	del_dev_tun( curr_gateway_tun_fd );

	curr_gateway = NULL;

	return NULL;

}

void del_default_route() {

	curr_gateway = NULL;

	if ( curr_gateway_thread_id != 0 )
		pthread_join( curr_gateway_thread_id, NULL );

}



int add_default_route() {

	if ( pthread_create( &curr_gateway_thread_id, NULL, &client_to_gw_tun, curr_gateway ) != 0 ) {

		do_log( "Error - couldn't not spawn thread: %s\n", strerror(errno) );
		curr_gateway = NULL;

	}

	return 1;

}




void close_all_sockets() {

	struct list_head *if_pos, *if_pos_tmp;
	struct batman_if *batman_if;

	list_for_each_safe(if_pos, if_pos_tmp, &if_list) {

		batman_if = list_entry(if_pos, struct batman_if, list);

		if ( batman_if->listen_thread_id != 0 ) {

			pthread_join( batman_if->listen_thread_id, NULL );
			close(batman_if->tcp_gw_sock);

		}

		close(batman_if->udp_recv_sock);
		close(batman_if->udp_send_sock);

		list_del( if_pos );
		debugFree( if_pos, 203 );

	}

	if ( ( routing_class != 0 ) && ( curr_gateway != NULL ) )
		del_default_route();

	if (vis_if.sock)
		close(vis_if.sock);

	if ( debug_level == 0 )
		closelog();

}

int is_aborted()
{
	return stop != 0;
}

void addr_to_string(unsigned int addr, char *str, int len)
{
	inet_ntop(AF_INET, &addr, str, len);
}

int rand_num(int limit)
{
	return ( limit == 0 ? 0 : rand() % limit );
}

int receive_packet( unsigned char *packet_buff, int packet_buff_len, int *hna_buff_len, unsigned int *neigh, unsigned int timeout, struct batman_if **if_incoming ) {

	fd_set wait_set;
	int res, max_sock = 0;
	struct sockaddr_in addr;
	unsigned int addr_len;
	struct timeval tv;
	struct list_head *if_pos;
	struct batman_if *batman_if;


	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;


	FD_ZERO(&wait_set);

	list_for_each(if_pos, &if_list) {

		batman_if = list_entry(if_pos, struct batman_if, list);

		FD_SET(batman_if->udp_recv_sock, &wait_set);

		if ( batman_if->udp_recv_sock > max_sock )
			max_sock = batman_if->udp_recv_sock;

	}

	for (;;)
	{
		res = select( max_sock + 1, &wait_set, NULL, NULL, &tv );

		if (res >= 0)
			break;

		if ( errno != EINTR ) {
			do_log( "Error - can't select: %s\n", strerror(errno) );
			return -1;
		}
	}

	if ( res == 0 )
		return 0;

	addr_len = sizeof (struct sockaddr_in);

	list_for_each(if_pos, &if_list) {
		batman_if = list_entry(if_pos, struct batman_if, list);

		if ( FD_ISSET( batman_if->udp_recv_sock, &wait_set) ) {

			if ( ( *hna_buff_len = recvfrom( batman_if->udp_recv_sock, packet_buff, packet_buff_len - 1, 0, (struct sockaddr *)&addr, &addr_len ) ) < 0 ) {
				do_log( "Error - can't receive packet: %s\n", strerror(errno) );
				return -1;
			}

			if ( *hna_buff_len < sizeof(struct packet) )
				return 0;

			packet_buff[*hna_buff_len] = '\0';

			(*if_incoming) = batman_if;
			break;

		}

	}

	*neigh = addr.sin_addr.s_addr;

	return 1;

}

int send_packet(unsigned char *buff, int len, struct sockaddr_in *broad, int sock)
{

	char log_str[200];

	if ( sendto( sock, buff, len, 0, (struct sockaddr *)broad, sizeof (struct sockaddr_in) ) < 0 ) {

		if ( errno == 1 ) {

			snprintf( log_str, sizeof( log_str ), "Error - can't send packet: %s.\nDoes your firewall allow outgoing packets on port %i ?\n", strerror(errno), ntohs(broad->sin_port ) );
			do_log( log_str, strerror(errno) );

		} else {

			do_log( "Error - can't send packet: %s.\n", strerror(errno) );

		}

		return -1;

	}

	return 0;

}

static void handler(int sig)
{
	stop = 1;
}

void *gw_listen( void *arg ) {

	struct batman_if *batman_if = (struct batman_if *)arg;
	struct gw_client *gw_client;
	struct list_head *client_pos, *client_pos_tmp;
	struct timeval tv;
	struct sockaddr_in addr;
	struct in_addr tmp_ip_holder;
	socklen_t sin_size = sizeof(struct sockaddr_in);
	char gw_addr[16], str2[16], tun_dev[IFNAMSIZ], tun_ip[] = "104.255.255.254\0";
	int res, status, max_sock_min, max_sock, buff_len, tun_fd;
	unsigned int addr_len, client_timeout;
	unsigned char buff[1500];
	fd_set wait_sockets, tmp_wait_sockets;


	addr_to_string(batman_if->addr.sin_addr.s_addr, gw_addr, sizeof (gw_addr));
	addr_len = sizeof (struct sockaddr_in);
	client_timeout = get_time();

	if ( inet_pton(AF_INET, tun_ip, &tmp_ip_holder) < 1 ) {

		do_log( "Error - invalid tunnel IP specified: %s\n", tun_ip );
		exit(EXIT_FAILURE);

	}

	if ( add_dev_tun( batman_if, tmp_ip_holder.s_addr, tun_dev, sizeof(tun_dev), &tun_fd ) < 0 ) {
		return NULL;
	}


	FD_ZERO(&wait_sockets);
	FD_SET(batman_if->tcp_gw_sock, &wait_sockets);
	FD_SET(batman_if->tunnel_sock, &wait_sockets);
	FD_SET(tun_fd, &wait_sockets);

	max_sock_min = batman_if->tcp_gw_sock;
	if ( batman_if->tunnel_sock > max_sock_min )
		max_sock_min = batman_if->tunnel_sock;
	if ( tun_fd > max_sock_min )
		max_sock_min = tun_fd;

	max_sock = max_sock_min;

	while (!is_aborted()) {

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		tmp_wait_sockets = wait_sockets;

		res = select(max_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv);

		if (res > 0) {

			/* new client */
			if ( FD_ISSET( batman_if->tcp_gw_sock, &tmp_wait_sockets ) ) {

				gw_client = debugMalloc( sizeof(struct gw_client), 18 );
				memset( gw_client, 0, sizeof(struct gw_client) );

				if ( ( gw_client->sock = accept(batman_if->tcp_gw_sock, (struct sockaddr *)&gw_client->addr, &sin_size) ) == -1 ) {
					do_log( "Error - can't accept client packet: %s\n", strerror(errno) );
					continue;
				}

				INIT_LIST_HEAD(&gw_client->list);
				gw_client->batman_if = batman_if;
				gw_client->last_keep_alive = get_time();

				FD_SET(gw_client->sock, &wait_sockets);
				if ( gw_client->sock > max_sock )
					max_sock = gw_client->sock;

				list_add_tail(&gw_client->list, &batman_if->client_list);

				if ( debug_level == 3 ) {
					addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
					printf( "gateway: %s (%s) got connection from %s (internet via %s)\n", gw_addr, batman_if->dev, str2, tun_dev );
				}

			/* tunnel activity */
			} else if ( FD_ISSET( batman_if->tunnel_sock, &tmp_wait_sockets ) ) {

				if ( ( buff_len = recvfrom( batman_if->tunnel_sock, buff, sizeof( buff ), 0, (struct sockaddr *)&addr, &addr_len ) ) < 0 ) {

					do_log( "Error - can't receive packet: %s\n", strerror(errno) );

				} else {

					if ( write( tun_fd, buff, buff_len ) < 0 ) {

						do_log( "Error - can't write packet: %s\n", strerror(errno) );

					}

				}

			/* /dev/tunX activity */
			} else if ( FD_ISSET( tun_fd, &tmp_wait_sockets ) ) {

				/* not needed - kernel knows client adress and routes traffic directly */

				do_log( "Warning - data coming through tun device: %s\n", tun_dev );

				/*if ( ( buff_len = read( tun_fd, buff, sizeof( buff ) ) ) < 0 ) {

					fprintf(stderr, "Could not read data from %s: %s\n", tun_dev, strerror(errno));

				} else {

					if ( sendto(curr_gateway_tun_sock, buff, buff_len, 0, (struct sockaddr *)&gw_addr, sizeof (struct sockaddr_in) ) < 0 ) {
						fprintf(stderr, "Cannot send to client: %s\n", strerror(errno));
					}

				}*/

			/* client sent keep alive */
			} else {

				max_sock = max_sock_min;

				list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

					gw_client = list_entry(client_pos, struct gw_client, list);

					if ( FD_ISSET( gw_client->sock, &tmp_wait_sockets ) ) {

						if ( debug_level >= 1 )
							addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));

						status = read( gw_client->sock, buff, sizeof( buff ) );

						if ( status > 0 ) {

							gw_client->last_keep_alive = get_time();

							if ( gw_client->sock > max_sock )
								max_sock = gw_client->sock;

							if ( debug_level == 3 ) {
								addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
								printf( "gateway: client %s sent keep alive on interface %s\n", str2, batman_if->dev );
							}

						} else {

							if ( status < 0 ) {

								do_log( "Error - can't read message: %s\n", strerror(errno) );

							} else {

								if ( debug_level == 3 )
									printf( "Client %s closed connection ...\n", str2 );

							}

							FD_CLR(gw_client->sock, &wait_sockets);
							close( gw_client->sock );

							list_del( client_pos );
							debugFree( client_pos, 201 );

						}

					} else {

						if ( gw_client->sock > max_sock )
							max_sock = gw_client->sock;

					}

				}

			}

		} else if ( ( res < 0 ) && (errno != EINTR) ) {

			do_log( "Error - can't select: %s\n", strerror(errno) );
			break;

		}


		/* close unresponsive client connections */
		if ( ( client_timeout + 59000 ) < get_time() ) {

			client_timeout = get_time();

			max_sock = max_sock_min;

			list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

				gw_client = list_entry(client_pos, struct gw_client, list);

				if ( ( gw_client->last_keep_alive + 120000 ) < client_timeout ) {

					FD_CLR(gw_client->sock, &wait_sockets);
					close( gw_client->sock );

					if ( debug_level == 3 ) {
						addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
						printf( "gateway: client %s timeout on interface %s\n", str2, batman_if->dev );
					}

					list_del( client_pos );
					debugFree( client_pos, 202 );

				} else {

					if ( gw_client->sock > max_sock )
						max_sock = gw_client->sock;

				}

			}

		}

	}

	/* delete tun devices on exit */
	del_dev_tun( tun_fd );

	return NULL;

}



int main(int argc, char *argv[])
{
	struct in_addr tmp_ip_holder;
	struct batman_if *batman_if;
	struct hna_node *hna_node;
	struct ifreq int_req;
	short on = 1, res, found_args = 1, netmask;
	int optchar;
	char str1[16], str2[16], *dev, *slash_ptr;
	unsigned int vis_server = 0;

	stop = 0;
	dev = NULL;
	memset(&tmp_ip_holder, 0, sizeof (struct in_addr));

	printf( "WARNING: You are using the unstable batman branch. If you are interested in *using* batman get the latest stable release !\n" );

	while ( ( optchar = getopt ( argc, argv, "a:d:hHo:g:p:r:s:vV" ) ) != -1 ) {

		switch ( optchar ) {

			case 'a':

				if ( ( slash_ptr = strchr( optarg, '/' ) ) == NULL ) {

					printf( "Invalid announced network (netmask is missing): %s\n", optarg );
					exit(EXIT_FAILURE);

				}

				*slash_ptr = '\0';

				if ( inet_pton(AF_INET, optarg, &tmp_ip_holder) < 1 ) {

					*slash_ptr = '/';
					printf( "Invalid announced network (IP is invalid): %s\n", optarg );
					exit(EXIT_FAILURE);

				}

				errno = 0;
				netmask = strtol(slash_ptr + 1, NULL , 10);

				if ( ( errno == ERANGE ) || ( errno != 0 && netmask == 0 ) ) {
					perror("strtol");
     					exit(EXIT_FAILURE);
				}

				if ( netmask < 0 || netmask > 32 ) {

					*slash_ptr = '/';
					printf( "Invalid announced network (netmask is invalid): %s\n", optarg );
     					exit(EXIT_FAILURE);

				}

				hna_node = debugMalloc(sizeof(struct hna_node), 16);
				memset(hna_node, 0, sizeof(struct hna_node));
				INIT_LIST_HEAD(&hna_node->list);

				hna_node->addr = tmp_ip_holder.s_addr;
				hna_node->netmask = netmask;

				list_add_tail(&hna_node->list, &hna_list);

				*slash_ptr = '/';
				found_args += 2;
				break;

			case 'd':

				errno = 0;
				debug_level = strtol (optarg, NULL , 10);

				if ( ( errno == ERANGE ) || ( errno != 0 && debug_level == 0 ) ) {
					perror("strtol");
      					exit(EXIT_FAILURE);
				}

				if ( debug_level < 0 || debug_level > 4 ) {
					printf( "Invalid debug level: %i\nDebug level has to be between 0 and 4.\n", debug_level );
      					exit(EXIT_FAILURE);
				}

				found_args += 2;
				break;

			case 'g':

				errno = 0;
				gateway_class = strtol(optarg, NULL , 10);

				if ( ( errno == ERANGE ) || ( errno != 0 && gateway_class == 0 ) ) {
					perror("strtol");
     					exit(EXIT_FAILURE);
				}

				if ( gateway_class < 0 || gateway_class > 11 ) {
					printf( "Invalid gateway class specified: %i.\nThe class is a value between 0 and 11.\n", gateway_class );
     					exit(EXIT_FAILURE);
				}

				found_args += 2;
				break;

			case 'H':
				verbose_usage();
				return (0);

			case 'o':

				errno = 0;
				orginator_interval = strtol (optarg, NULL , 10);

				if ( (errno == ERANGE && (orginator_interval == LONG_MAX || orginator_interval == LONG_MIN) ) || (errno != 0 && orginator_interval == 0) ) {
					perror("strtol");
     					exit(EXIT_FAILURE);
				}

				if (orginator_interval < 1) {
					printf( "Invalid orginator interval specified: %i.\nThe Interval has to be greater than 0.\n", orginator_interval );
     					exit(EXIT_FAILURE);
				}

				found_args += 2;
				break;

			case 'p':

				errno = 0;
				if ( inet_pton(AF_INET, optarg, &tmp_ip_holder) < 1 ) {

					printf( "Invalid preferred gateway IP specified: %s\n", optarg );
     					exit(EXIT_FAILURE);

				}

				pref_gateway = tmp_ip_holder.s_addr;

				found_args += 2;
				break;

			case 'r':

				errno = 0;
				routing_class = strtol (optarg, NULL , 10);

				if ( ( errno == ERANGE ) || ( errno != 0 && routing_class == 0 ) ) {
					perror("strtol");
     					exit(EXIT_FAILURE);
				}

				if (routing_class < 0 || routing_class > 3) {
					printf( "Invalid routing class specified: %i.\nThe class is a value between 0 and 3.\n", routing_class );
     					exit(EXIT_FAILURE);
				}

				found_args += 2;
				break;

			case 's':

				errno = 0;
				if ( inet_pton(AF_INET, optarg, &tmp_ip_holder) < 1 ) {

					printf( "Invalid preferred visualation server IP specified: %s\n", optarg );
     					exit(EXIT_FAILURE);

				}

				vis_server = tmp_ip_holder.s_addr;


				found_args += 2;
				break;

			case 'v':

				printf( "B.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );
				return (0);

			case 'V':

				print_animation();

				printf( "\x1B[0;0HB.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );
				printf( "\x1B[9;0H \t May the bat guide your path ...\n\n\n" );

				return (0);

			case 'h':
			default:
				usage();
				return (0);

		}

	}

	if ( ( gateway_class != 0 ) && ( routing_class != 0 ) ) {
		fprintf( stderr, "Error - routing class can't be set while gateway class is in use !\n" );
		usage();
		close_all_sockets();
		exit(EXIT_FAILURE);
	}

	if ( ( gateway_class != 0 ) && ( pref_gateway != 0 ) ) {
		fprintf( stderr, "Error - preferred gateway can't be set while gateway class is in use !\n" );
		usage();
		close_all_sockets();
		exit(EXIT_FAILURE);
	}

	if ( ( routing_class == 0 ) && ( pref_gateway != 0 ) ) {
		fprintf( stderr, "Error - preferred gateway can't be set without specifying routing class !\n" );
		usage();
		close_all_sockets();
		exit(EXIT_FAILURE);
	}

	if ( ( ( routing_class != 0 ) || ( gateway_class != 0 ) ) && ( !probe_tun() ) ) {
		close_all_sockets();
		exit( 1 );
	}

	if ( argc <= found_args ) {
		fprintf( stderr, "Error - no interface specified\n" );
		usage();
		close_all_sockets();
		exit(EXIT_FAILURE);
	}


	/* daemonize */
	if ( debug_level == 0 ) {

		if ( daemon( 0, 0 ) < 0 ) {

			printf( "Error - can't fork to background: %s\n", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		openlog( "batmand", LOG_PID, LOG_DAEMON );

	} else {

		printf( "B.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );

	}

	while ( argc > found_args ) {

		batman_if = debugMalloc(sizeof(struct batman_if), 17);
		memset(batman_if, 0, sizeof(struct batman_if));
		INIT_LIST_HEAD(&batman_if->list);
		INIT_LIST_HEAD(&batman_if->client_list);

		batman_if->dev = argv[found_args];
		batman_if->if_num = found_ifs;

		list_add_tail(&batman_if->list, &if_list);

		if ( strlen(batman_if->dev) > IFNAMSIZ - 1 ) {
			do_log( "Error - interface name too long: %s\n", batman_if->dev );
			close_all_sockets();
			exit(EXIT_FAILURE);
		}

		batman_if->udp_send_sock = socket(PF_INET, SOCK_DGRAM, 0);
		if (batman_if->udp_send_sock < 0) {
			do_log( "Error - can't create send socket: %s", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);
		}

		batman_if->udp_recv_sock = socket(PF_INET, SOCK_DGRAM, 0);
		if ( batman_if->udp_recv_sock < 0 ) {

			do_log( "Error - can't create recieve socket: %s", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		memset(&int_req, 0, sizeof (struct ifreq));
		strncpy(int_req.ifr_name, batman_if->dev, IFNAMSIZ - 1);

		if ( ioctl( batman_if->udp_recv_sock, SIOCGIFADDR, &int_req ) < 0 ) {

			do_log( "Error - can't get IP address of interface %s\n", batman_if->dev );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		batman_if->addr.sin_family = AF_INET;
		batman_if->addr.sin_port = htons(PORT);
		batman_if->addr.sin_addr.s_addr = ((struct sockaddr_in *)&int_req.ifr_addr)->sin_addr.s_addr;

		if ( ioctl( batman_if->udp_recv_sock, SIOCGIFBRDADDR, &int_req ) < 0 ) {

			do_log( "Error - can't get broadcast IP address of interface %s\n", batman_if->dev );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		batman_if->broad.sin_family = AF_INET;
		batman_if->broad.sin_port = htons(PORT);
		batman_if->broad.sin_addr.s_addr = ((struct sockaddr_in *)&int_req.ifr_broadaddr)->sin_addr.s_addr;

		if ( setsockopt( batman_if->udp_send_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof (int) ) < 0 ) {

			do_log( "Error - can't enable broadcasts: %s\n", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		if ( bind_to_iface( batman_if->udp_send_sock, batman_if->dev ) < 0 ) {

			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		if ( bind( batman_if->udp_send_sock, (struct sockaddr *)&batman_if->addr, sizeof (struct sockaddr_in) ) < 0 ) {

			do_log( "Error - can't bind send socket: %s\n", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		if ( bind_to_iface( batman_if->udp_recv_sock, batman_if->dev ) < 0 ) {

			close_all_sockets();
			exit(EXIT_FAILURE);

		}

		if ( bind( batman_if->udp_recv_sock, (struct sockaddr *)&batman_if->broad, sizeof (struct sockaddr_in) ) < 0 ) {

			do_log( "Error - can't bind receive socket: %s\n", strerror(errno) );
			close_all_sockets();
			exit(EXIT_FAILURE);

		}


		addr_to_string(batman_if->addr.sin_addr.s_addr, str1, sizeof (str1));
		addr_to_string(batman_if->broad.sin_addr.s_addr, str2, sizeof (str2));

		if ( debug_level > 0 )
			printf("Using interface %s with address %s and broadcast address %s\n", batman_if->dev, str1, str2);

		if ( gateway_class != 0 ) {

			batman_if->addr.sin_port = htons(PORT + 1);

			batman_if->tcp_gw_sock = socket(PF_INET, SOCK_STREAM, 0);

			if ( batman_if->tcp_gw_sock < 0 ) {
				do_log( "Error - can't create socket: %s", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			if ( setsockopt(batman_if->tcp_gw_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0 ) {
				do_log( "Error - can't enable reuse of address: %s\n", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			if ( bind( batman_if->tcp_gw_sock, (struct sockaddr*)&batman_if->addr, sizeof(struct sockaddr_in)) < 0 ) {
				do_log( "Error - can't bind socket: %s\n", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			if ( listen( batman_if->tcp_gw_sock, 10 ) < 0 ) {
				do_log( "Error - can't listen socket: %s\n", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			batman_if->tunnel_sock = socket(PF_INET, SOCK_DGRAM, 0);
			if ( batman_if->tunnel_sock < 0 ) {
				do_log( "Error - can't create tunnel socket: %s", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			if ( bind(batman_if->tunnel_sock, (struct sockaddr *)&batman_if->addr, sizeof (struct sockaddr_in)) < 0 ) {
				do_log( "Error - can't bind tunnel socket: %s\n", strerror(errno) );
				close_all_sockets();
				exit(EXIT_FAILURE);
			}

			batman_if->addr.sin_port = htons(PORT);

			pthread_create(&batman_if->listen_thread_id, NULL, &gw_listen, batman_if);

		}

		found_ifs++;
		found_args++;

	}

	if(vis_server)
	{
		memset(&vis_if.addr, 0, sizeof(vis_if.addr));
		vis_if.addr.sin_family = AF_INET;
		vis_if.addr.sin_port = htons(1967);
		vis_if.addr.sin_addr.s_addr = vis_server;
		/*vis_if.sock = socket( PF_INET, SOCK_DGRAM, 0);*/
		vis_if.sock = ((struct batman_if *)if_list.next)->udp_send_sock;
	} else
		memset(&vis_if, 0, sizeof(vis_if));


	if ( debug_level > 0 ) {

		printf("debug level: %i\n", debug_level);

		if ( orginator_interval != 1000 )
			printf( "orginator interval: %i\n", orginator_interval );

		if ( gateway_class > 0 )
			printf( "gateway class: %i\n", gateway_class );

		if ( routing_class > 0 )
			printf( "routing class: %i\n", routing_class );

		if ( pref_gateway > 0 ) {
			addr_to_string(pref_gateway, str1, sizeof (str1));
			printf( "preferred gateway: %s\n", str1 );
		}

		if ( vis_server > 0 ) {
			addr_to_string(vis_server, str1, sizeof (str1));
			printf( "visualisation server: %s\n", str1 );
		}

	}


	signal(SIGINT, handler);
	signal(SIGTERM, handler);

	gettimeofday(&start_time, NULL);

	srand(getpid());

	res = batman();

	close_all_sockets();

	checkLeak();

	return res;

}
