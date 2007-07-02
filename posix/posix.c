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



#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/times.h>


#include "../os.h"
#include "../batman.h"



#define BAT_LOGO_PRINT(x,y,z) printf( "\x1B[%i;%iH%c", y + 1, x, z )                      /* write char 'z' into column 'x', row 'y' */
#define BAT_LOGO_END(x,y) printf("\x1B[8;0H");fflush(NULL);bat_wait( x, y );              /* end of current picture */


extern struct vis_if vis_if;

static clock_t start_time;
static float system_tick;



uint32_t get_time( void ) {

	return (uint32_t)( ( (float)( times(NULL) - start_time ) * 1000 ) / system_tick );

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



void bat_wait( int32_t T, int32_t t ) {

	struct timeval time;

	time.tv_sec = T;
	time.tv_usec = ( t * 10000 );

	select( 0, NULL, NULL, NULL, &time );

	return;

}



void print_animation( void ) {

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

}



void addr_to_string( uint32_t addr, char *str, int32_t len ) {

	inet_ntop( AF_INET, &addr, str, len );

}



int32_t rand_num( int32_t limit ) {

	return ( limit == 0 ? 0 : rand() % limit );

}



int8_t is_aborted() {

	return stop != 0;

}



void handler( int32_t sig ) {

	stop = 1;

}


void del_default_route() {

	curr_gateway = NULL;

	if ( curr_gateway_thread_id != 0 )
		pthread_join( curr_gateway_thread_id, NULL );

}



int8_t add_default_route() {

	struct curr_gw_data *curr_gw_data;


	curr_gw_data = debugMalloc( sizeof(struct curr_gw_data), 207 );
	curr_gw_data->orig = curr_gateway->orig_node->orig;
	curr_gw_data->gw_node = curr_gateway;
	curr_gw_data->batman_if = curr_gateway->orig_node->batman_if;


	if ( pthread_create( &curr_gateway_thread_id, NULL, &client_to_gw_tun, curr_gw_data ) != 0 ) {

		debug_output( 0, "Error - couldn't spawn thread: %s\n", strerror(errno) );
		debugFree( curr_gw_data, 1213 );
		curr_gateway = NULL;

	}

	return 1;

}



int8_t receive_packet( unsigned char *packet_buff, int32_t packet_buff_len, int16_t *hna_buff_len, uint32_t *neigh, uint32_t timeout, struct batman_if **if_incoming ) {

	struct sockaddr_in addr;
	struct timeval tv;
	struct list_head *if_pos;
	struct batman_if *batman_if;
	uint32_t addr_len;
	int8_t res;
	fd_set tmp_wait_set;


	addr_len = sizeof(struct sockaddr_in);
	memcpy( &tmp_wait_set, &receive_wait_set, sizeof(fd_set) );

	while (1) {

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = ( timeout % 1000 ) * 1000;

		res = select( receive_max_sock + 1, &tmp_wait_set, NULL, NULL, &tv );

		if ( res >= 0 )
			break;

		if ( errno != EINTR ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
			return -1;

		}

	}

	if ( res == 0 )
		return 0;

	list_for_each( if_pos, &if_list ) {

		batman_if = list_entry( if_pos, struct batman_if, list );

		if ( FD_ISSET( batman_if->udp_recv_sock, &tmp_wait_set ) ) {

			if ( ( *hna_buff_len = recvfrom( batman_if->udp_recv_sock, packet_buff, packet_buff_len - 1, 0, (struct sockaddr *)&addr, &addr_len ) ) < 0 ) {

				debug_output( 0, "Error - can't receive packet: %s\n", strerror(errno) );
				return -1;

			}

			if ( *hna_buff_len < sizeof(struct bat_packet) )
				return 0;

			((struct bat_packet *)packet_buff)->seqno = ntohs( ((struct bat_packet *)packet_buff)->seqno ); /* network to host order for our 16bit seqno. */

			(*if_incoming) = batman_if;
			break;

		}

	}

	*neigh = addr.sin_addr.s_addr;

	return 1;

}



int8_t send_udp_packet( unsigned char *packet_buff, int32_t packet_buff_len, struct sockaddr_in *broad, int32_t send_sock ) {

	if ( sendto( send_sock, packet_buff, packet_buff_len, 0, (struct sockaddr *)broad, sizeof(struct sockaddr_in) ) < 0 ) {

		if ( errno == 1 ) {

			debug_output( 0, "Error - can't send udp packet: %s.\nDoes your firewall allow outgoing packets on port %i ?\n", strerror(errno), ntohs( broad->sin_port ) );

		} else {

			debug_output( 0, "Error - can't send udp packet: %s.\n", strerror(errno) );

		}

		return -1;

	}

	return 0;

}



int8_t send_raw_packet( unsigned char *packet_buff, int32_t packet_buff_len, struct batman_if *batman_if ) {

	memcpy( packet_buff, (unsigned char *)&batman_if->out, sizeof(struct iphdr) + sizeof(struct udphdr) );

	if ( write( batman_if->udp_send_sock, packet_buff, packet_buff_len ) < 0 ) {

		if ( errno == 1 ) {

			debug_output( 0, "Error - can't send raw packet: %s.\nDoes your firewall allow outgoing packets on port %i ?\n", strerror(errno), ntohs( batman_if->out.udp.dest ) );

		} else {

			debug_output( 0, "Error - can't send raw packet: %s.%i\n", strerror(errno), errno );

		}

		return -1;

	}

	return 0;

}



void restore_defaults() {

	struct list_head *if_pos, *if_pos_tmp;
	struct batman_if *batman_if;


	stop = 1;

	if ( routing_class > 0 )
		add_del_interface_rules( 1 );

	list_for_each_safe( if_pos, if_pos_tmp, &if_list ) {

		batman_if = list_entry( if_pos, struct batman_if, list );

		if ( batman_if->listen_thread_id != 0 ) {

			pthread_join( batman_if->listen_thread_id, NULL );
			close( batman_if->udp_tunnel_sock );

		}

		close( batman_if->udp_recv_sock );
		close( batman_if->udp_send_sock );

		if ( ( batman_if->netaddr > 0 ) && ( batman_if->netmask > 0 ) ) {

			add_del_rule( batman_if->netaddr, batman_if->netmask, BATMAN_RT_TABLE_HOSTS, BATMAN_RT_PRIO_DEFAULT + batman_if->if_num, 0, 1, 1 );
			add_del_route( batman_if->netaddr, batman_if->netmask, 0, batman_if->if_index, batman_if->dev, BATMAN_RT_TABLE_HOSTS, 2, 1 );

		}

		list_del( (struct list_head *)&if_list, if_pos, &if_list );
		debugFree( if_pos, 1214 );

	}

	if ( ( routing_class != 0 ) && ( curr_gateway != NULL ) )
		del_default_route();

	if ( vis_if.sock )
		close( vis_if.sock );

	if ( unix_if.unix_sock )
		close( unix_if.unix_sock );

	if ( unix_if.listen_thread_id != 0 )
		pthread_join( unix_if.listen_thread_id, NULL );

	if ( debug_level == 0 )
		closelog();

}



void restore_and_exit( uint8_t is_sigsegv ) {

	struct list_head *if_pos;
	struct batman_if *batman_if;
	struct orig_node *orig_node;
	struct hash_it_t *hashit = NULL;


	if ( !unix_client ) {

		/* remove tun interface first */
		stop = 1;

		list_for_each( if_pos, &if_list ) {

			batman_if = list_entry( if_pos, struct batman_if, list );

			if ( batman_if->listen_thread_id != 0 ) {

				pthread_join( batman_if->listen_thread_id, NULL );
				close( batman_if->udp_tunnel_sock );
				batman_if->listen_thread_id = 0;

			}

		}

		if ( ( routing_class != 0 ) && ( curr_gateway != NULL ) )
			del_default_route();

		/* all rules and routes were purged in segmentation_fault() */
		if ( !is_sigsegv ) {

			while ( NULL != ( hashit = hash_iterate( orig_hash, hashit ) ) ) {

				orig_node = hashit->bucket->data;

				update_routes( orig_node, NULL, NULL, 0 );

			}

		}

		restore_defaults();

	}

	if ( !is_sigsegv )
		exit(EXIT_FAILURE);

}



void segmentation_fault( int32_t sig ) {

	signal( SIGSEGV, SIG_DFL );

	debug_output( 0, "Error - SIGSEGV received, trying to clean up ... \n" );

	flush_routes_rules(0);
	flush_routes_rules(1);

	restore_and_exit(1);

	raise( SIGSEGV );

}



void cleanup() {

	int8_t i;
	struct debug_level_info *debug_level_info;
	struct list_head *debug_pos, *debug_pos_tmp;


	for ( i = 0; i < debug_level_max; i++ ) {

		if ( debug_clients.clients_num[i] > 0 ) {

			list_for_each_safe( debug_pos, debug_pos_tmp, (struct list_head *)debug_clients.fd_list[i] ) {

				debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

				list_del( (struct list_head *)debug_clients.fd_list[i], debug_pos, (struct list_head_first *)debug_clients.fd_list[i] );
				debugFree( debug_pos, 1218 );

			}

		}

		debugFree( debug_clients.fd_list[i], 1219 );
		debugFree( debug_clients.mutex[i], 1220 );

	}

	debugFree( debug_clients.fd_list, 1221 );
	debugFree( debug_clients.mutex, 1222 );
	debugFree( debug_clients.clients_num, 1223 );

}



int main( int argc, char *argv[] ) {

	int8_t res;


	/* check if user is root */
	if ( ( getuid() ) || ( getgid() ) ) {

		fprintf( stderr, "Error - you must be root to run %s !\n", argv[0] );
		exit(EXIT_FAILURE);

	}


	INIT_LIST_HEAD_FIRST( forw_list );
	INIT_LIST_HEAD_FIRST( gw_list );
	INIT_LIST_HEAD_FIRST( if_list );
	INIT_LIST_HEAD_FIRST( hna_list );


	start_time = times(NULL);
	system_tick = (float)sysconf(_SC_CLK_TCK);


	apply_init_args( argc, argv );


	srand( getpid() );

	res = batman();

	restore_defaults();
	cleanup();
	checkLeak();
	return res;

}


