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



#define _GNU_SOURCE
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



static int8_t stop;



int8_t is_aborted() {

	return stop != 0;

}



void handler( int32_t sig ) {

	stop = 1;

}



void debug_output( int8_t debug_prio, char *format, ... ) {

	struct list_head *debug_pos;
	struct debug_level_info *debug_level_info;
	int8_t debug_prio_intern;
	va_list args;


	if ( debug_prio == 0 ) {

		if ( debug_level == 0 ) {

			va_start( args, format );
			vsyslog( LOG_ERR, format, args );
			va_end( args );

		} else if ( ( debug_level == 3 ) || ( debug_level == 4 ) ) {

			if ( debug_level == 4 )
				printf( "[%10u] ", get_time() );

			va_start( args, format );
			vprintf( format, args );
			va_end( args );

		}

		debug_prio_intern = 3;

	} else {

		debug_prio_intern = debug_prio - 1;

	}

	if ( debug_clients.clients_num[debug_prio_intern] > 0 ) {

		va_start( args, format );

		list_for_each( debug_pos, (struct list_head *)debug_clients.fd_list[debug_prio_intern] ) {

			debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

			if ( debug_prio_intern == 3 )
				dprintf( debug_level_info->fd, "[%10u] ", get_time() );

			if ( ( ( debug_level == 1 ) || ( debug_level == 2 ) ) && ( debug_level_info->fd == 1 ) && ( strcmp( format, "BOD\n" ) == 0 ) ) {

				system( "clear" );

			} else {

				vdprintf( debug_level_info->fd, format, args );

			}

		}

		va_end( args );

	}

}



void *unix_listen( void *arg ) {

	struct unix_client *unix_client;
	struct debug_level_info *debug_level_info;
	struct list_head *unix_pos, *unix_pos_tmp, *debug_pos, *debug_pos_tmp;
	struct timeval tv;
	int32_t status, max_sock;
	int8_t res;
	unsigned char buff[1500];
	fd_set wait_sockets, tmp_wait_sockets;
	socklen_t sun_size = sizeof(struct sockaddr_un);


	INIT_LIST_HEAD(&unix_if.client_list);

	FD_ZERO(&wait_sockets);
	FD_SET(unix_if.unix_sock, &wait_sockets);

	max_sock = unix_if.unix_sock;

	while ( !is_aborted() ) {

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		tmp_wait_sockets = wait_sockets;

		res = select( max_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv );

		if ( res > 0 ) {

			/* new client */
			if ( FD_ISSET( unix_if.unix_sock, &tmp_wait_sockets ) ) {

				unix_client = debugMalloc( sizeof(struct unix_client), 201 );
				memset( unix_client, 0, sizeof(struct unix_client) );

				if ( ( unix_client->sock = accept( unix_if.unix_sock, (struct sockaddr *)&unix_client->addr, &sun_size) ) == -1 ) {
					debug_output( 0, "Error - can't accept unix client: %s\n", strerror(errno) );
					continue;
				}

				INIT_LIST_HEAD(&unix_client->list);

				FD_SET(unix_client->sock, &wait_sockets);
				if ( unix_client->sock > max_sock )
					max_sock = unix_client->sock;

				list_add_tail(&unix_client->list, &unix_if.client_list);

				debug_output( 3, "Unix socket: got connection\n" );

			/* client sent data */
			} else {

				max_sock = unix_if.unix_sock;

				list_for_each_safe(unix_pos, unix_pos_tmp, &unix_if.client_list) {

					unix_client = list_entry(unix_pos, struct unix_client, list);

					if ( FD_ISSET( unix_client->sock, &tmp_wait_sockets ) ) {

						status = read( unix_client->sock, buff, sizeof( buff ) );

						if ( status > 0 ) {

							if ( unix_client->sock > max_sock )
								max_sock = unix_client->sock;

							/* debug_output( 3, "gateway: client sent data via unix socket: %s\n", buff ); */

							if ( ( buff[2] == '1' ) || ( buff[2] == '2' ) || ( buff[2] == '3' ) || ( buff[2] == '4' ) ) {

								if ( unix_client->debug_level != 0 ) {

									list_for_each_safe( debug_pos, debug_pos_tmp, (struct list_head *)debug_clients.fd_list[(int)unix_client->debug_level - '1'] ) {

										debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

										if ( debug_level_info->fd == unix_client->sock ) {

											list_del( debug_pos );
											debug_clients.clients_num[(int)unix_client->debug_level - '1']--;

											debugFree( debug_pos, 1201 );

											break;

										}

									}

								}

								if ( unix_client->debug_level != buff[2] ) {

									debug_level_info = debugMalloc( sizeof(struct debug_level_info), 202 );
									INIT_LIST_HEAD( &debug_level_info->list );
									debug_level_info->fd = unix_client->sock;
									list_add( &debug_level_info->list, (struct list_head *)debug_clients.fd_list[(int)buff[2] - '1'] );
									debug_clients.clients_num[(int)buff[2] - '1']++;

									unix_client->debug_level = (int)buff[2];

								} else {

									unix_client->debug_level = 0;

								}

							}

						} else {

							if ( status < 0 ) {

								debug_output( 0, "Error - can't read unix message: %s\n", strerror(errno) );

							} else {

								if ( unix_client->debug_level != 0 ) {

									list_for_each_safe( debug_pos, debug_pos_tmp, (struct list_head *)debug_clients.fd_list[(int)unix_client->debug_level - '1'] ) {

										debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

										if ( debug_level_info->fd == unix_client->sock ) {

											list_del( debug_pos );
											debug_clients.clients_num[(int)unix_client->debug_level - '1']--;

											debugFree( debug_pos, 1202 );

											break;

										}

									}

								}

								debug_output( 3, "Unix client closed connection ...\n" );

							}

							FD_CLR(unix_client->sock, &wait_sockets);
							close( unix_client->sock );

							list_del( unix_pos );
							debugFree( unix_pos, 1203 );

						}

					} else {

						if ( unix_client->sock > max_sock )
							max_sock = unix_client->sock;

					}

				}

			}

		} else if ( ( res < 0 ) && ( errno != EINTR ) ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
			break;

		}

	}

	list_for_each_safe(unix_pos, unix_pos_tmp, &unix_if.client_list) {

		unix_client = list_entry(unix_pos, struct unix_client, list);

		if ( unix_client->debug_level != 0 ) {

			list_for_each_safe( debug_pos, debug_pos_tmp, (struct list_head *)debug_clients.fd_list[(int)unix_client->debug_level - '1'] ) {

				debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

				if ( debug_level_info->fd == unix_client->sock ) {

					list_del( debug_pos );
					debug_clients.clients_num[(int)unix_client->debug_level - '1']--;

					debugFree( debug_pos, 1204 );

					break;

				}

			}

		}

		list_del( unix_pos );
		debugFree( unix_pos, 1205 );

	}

	return NULL;

}



void apply_init_args( int argc, char *argv[] ) {

	struct in_addr tmp_ip_holder;
	struct batman_if *batman_if;
	struct hna_node *hna_node;
	struct debug_level_info *debug_level_info;
// 	struct timeval tv;
	uint8_t found_args = 1, unix_client = 0, batch_mode = 0, batch_counter = 0;
	uint16_t netmask;
	int8_t res;

	int32_t optchar, recv_buff_len, bytes_written;
	char str1[16], str2[16], *slash_ptr, unix_string[100], buff[1500], *buff_ptr, *cr_ptr;
	uint32_t vis_server = 0;
// 	fd_set wait_sockets, tmp_wait_sockets;

	memset( &tmp_ip_holder, 0, sizeof (struct in_addr) );
	stop = 0;


	printf( "WARNING: You are using the unstable batman branch. If you are interested in *using* batman get the latest stable release !\n" );

	while ( ( optchar = getopt ( argc, argv, "a:bcd:hHo:g:p:r:s:vV" ) ) != -1 ) {

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

				if ( netmask < 1 || netmask > 32 ) {

					*slash_ptr = '/';
					printf( "Invalid announced network (netmask is invalid): %s\n", optarg );
					exit(EXIT_FAILURE);

				}

				hna_node = debugMalloc( sizeof(struct hna_node), 203 );
				memset( hna_node, 0, sizeof(struct hna_node) );
				INIT_LIST_HEAD( &hna_node->list );

				hna_node->addr = tmp_ip_holder.s_addr;
				hna_node->netmask = netmask;

				list_add_tail( &hna_node->list, &hna_list );

				*slash_ptr = '/';
				found_args += 2;
				break;

			case 'b':
				batch_mode++;
				break;

			case 'c':
				unix_client++;
				break;

			case 'd':

				errno = 0;
				debug_level = strtol (optarg, NULL , 10);

				if ( ( errno == ERANGE ) || ( errno != 0 && debug_level == 0 ) ) {
					perror("strtol");
					exit(EXIT_FAILURE);
				}

				if ( debug_level > 4 ) {
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

				if ( gateway_class > 11 ) {
					printf( "Invalid gateway class specified: %i.\nThe class is a value between 0 and 11.\n", gateway_class );
					exit(EXIT_FAILURE);
				}

				found_args += 2;
				break;

			case 'H':
				verbose_usage();
				exit(EXIT_SUCCESS);

			case 'o':

				errno = 0;
				orginator_interval = strtol (optarg, NULL , 10);

				if ( orginator_interval < 1 ) {

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

				if ( routing_class > 3 ) {

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
				exit(EXIT_SUCCESS);

			case 'V':

				print_animation();

				printf( "\x1B[0;0HB.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );
				printf( "\x1B[9;0H \t May the bat guide your path ...\n\n\n" );

				exit(EXIT_SUCCESS);

			case 'h':
			default:
				usage();
				exit(EXIT_SUCCESS);

		}

	}


	if ( ( gateway_class != 0 ) && ( routing_class != 0 ) ) {
		fprintf( stderr, "Error - routing class can't be set while gateway class is in use !\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	if ( ( gateway_class != 0 ) && ( pref_gateway != 0 ) ) {
		fprintf( stderr, "Error - preferred gateway can't be set while gateway class is in use !\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	if ( ( routing_class == 0 ) && ( pref_gateway != 0 ) ) {
		fprintf( stderr, "Error - preferred gateway can't be set without specifying routing class !\n" );
		usage();
		exit(EXIT_FAILURE);
	}

	if ( ( ( routing_class != 0 ) || ( gateway_class != 0 ) ) && ( !probe_tun() ) )
		exit(EXIT_FAILURE);

	if ( ! unix_client ) {

		if ( argc <= found_args ) {
			fprintf( stderr, "Error - no interface specified\n" );
			usage();
			restore_defaults();
			exit(EXIT_FAILURE);
		}

		for ( res = 0; res < 4; res++ ) {

			debug_clients.fd_list[res] = debugMalloc( sizeof(struct list_head), 204 );
			INIT_LIST_HEAD( (struct list_head *)debug_clients.fd_list[res] );

		}

		memset( &debug_clients.clients_num, 0, sizeof(debug_clients.clients_num) );

		/* daemonize */
		if ( debug_level == 0 ) {

			if ( daemon( 0, 0 ) < 0 ) {

				printf( "Error - can't fork to background: %s\n", strerror(errno) );
				restore_defaults();
				exit(EXIT_FAILURE);

			}

			openlog( "batmand", LOG_PID, LOG_DAEMON );

		} else {

			printf( "B.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );

			debug_clients.clients_num[ debug_level - 1 ]++;
			debug_level_info = debugMalloc( sizeof(struct debug_level_info), 205 );
			INIT_LIST_HEAD( &debug_level_info->list );
			debug_level_info->fd = 1;
			list_add( &debug_level_info->list, (struct list_head *)debug_clients.fd_list[debug_level - 1] );

		}

		FD_ZERO( &receive_wait_set );

		while ( argc > found_args ) {

			batman_if = debugMalloc( sizeof(struct batman_if), 206 );
			memset( batman_if, 0, sizeof(struct batman_if) );
			INIT_LIST_HEAD( &batman_if->list );
			INIT_LIST_HEAD( &batman_if->client_list );

			batman_if->dev = argv[found_args];
			batman_if->if_num = found_ifs;

			list_add_tail( &batman_if->list, &if_list );

			init_interface ( batman_if );

			if ( batman_if->udp_recv_sock > receive_max_sock )
				receive_max_sock = batman_if->udp_recv_sock;

			FD_SET( batman_if->udp_recv_sock, &receive_wait_set );

			addr_to_string(batman_if->addr.sin_addr.s_addr, str1, sizeof (str1));
			addr_to_string(batman_if->broad.sin_addr.s_addr, str2, sizeof (str2));

			if ( debug_level > 0 )
				printf( "Using interface %s with address %s and broadcast address %s\n", batman_if->dev, str1, str2 );

			if ( gateway_class != 0 ) {

			  init_interface_gw( batman_if );

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
		} else {
			memset(&vis_if, 0, sizeof(vis_if));
		}


		unlink( UNIX_PATH );
		unix_if.unix_sock = socket(AF_LOCAL, SOCK_STREAM, 0);

		memset( &unix_if.addr, 0, sizeof(struct sockaddr_un) );
		unix_if.addr.sun_family = AF_LOCAL;
		strcpy( unix_if.addr.sun_path, UNIX_PATH );

		if ( bind ( unix_if.unix_sock, (struct sockaddr *)&unix_if.addr, sizeof (struct sockaddr_un) ) < 0 ) {
			debug_output( 0, "Error - can't bind unix socket: %s\n", strerror(errno) );
			restore_defaults();
			exit(EXIT_FAILURE);
		}

		if ( listen( unix_if.unix_sock, 10 ) < 0 ) {
			debug_output( 0, "Error - can't listen unix socket: %s\n", strerror(errno) );
			restore_defaults();
			exit(EXIT_FAILURE);
		}

		pthread_create( &unix_if.listen_thread_id, NULL, &unix_listen, NULL );


		if ( debug_level > 0 ) {

			printf( "debug level: %i\n", debug_level );

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

	/* connect to running batmand via unix socket */
	} else {

		if ( ( debug_level == 1 ) || ( debug_level == 2 ) || ( debug_level == 3 ) || ( debug_level == 4 ) ) {

			if ( ( ( debug_level == 3 ) || ( debug_level == 4 ) ) && ( batch_mode ) )
				printf( "WARNING: Your chosen debug level (%i) does not support batch mode !\n", debug_level );

			unix_if.unix_sock = socket(AF_LOCAL, SOCK_STREAM, 0);

			memset( &unix_if.addr, 0, sizeof(struct sockaddr_un) );
			unix_if.addr.sun_family = AF_LOCAL;
			strcpy( unix_if.addr.sun_path, UNIX_PATH );

			if ( connect ( unix_if.unix_sock, (struct sockaddr *)&unix_if.addr, sizeof(struct sockaddr_un) ) < 0 ) {

				printf( "Error - can't connect to unix socket '%s': %s ! Is batmand running on this host ?\n", UNIX_PATH, strerror(errno) );
				close( unix_if.unix_sock );
				exit(EXIT_FAILURE);

			}

			snprintf( unix_string, sizeof( unix_string ), "d:%i", debug_level );

// 			FD_ZERO(&wait_sockets);
// 			FD_SET(unix_if.unix_sock, &wait_sockets);

			if ( write( unix_if.unix_sock, unix_string, strlen( unix_string ) ) < 0 ) {

				printf( "Error - can't write to unix socket: %s\n", strerror(errno) );
				close( unix_if.unix_sock );
				exit(EXIT_FAILURE);

			}

			while ( ( recv_buff_len = read( unix_if.unix_sock, buff, sizeof( buff ) ) ) > 0 ) {

				buff_ptr = buff;
				bytes_written = 0;

				while ( ( cr_ptr = strchr( buff_ptr, '\n' ) ) != NULL ) {

					*cr_ptr = '\0';

					if ( strcmp( buff_ptr, "BOD" ) == 0 ) {

						if ( batch_mode ) {

							if ( batch_counter ) {

								close( unix_if.unix_sock );
								exit(EXIT_SUCCESS);

							}

							batch_counter++;

						} else {

							system( "clear" );

						}

					} else {

						printf( "%s\n", buff_ptr );

					}

					bytes_written += strlen( buff_ptr ) + 1;
					buff_ptr = cr_ptr + 1;

				}

				if ( bytes_written != recv_buff_len )
					printf( "%s", buff_ptr );

			}

			if ( recv_buff_len < 0 ) {

				printf( "Error - can't read from unix socket: %s\n", strerror(errno) );
				close( unix_if.unix_sock );
				exit(EXIT_FAILURE);

			} else {

				printf( "Connection terminated by remote host\n" );

			}

// 			while ( 1 ) {
//
// 				if ( write( unix_if.unix_sock, unix_string, strlen( unix_string ) ) < 0 ) {
//
// 					printf( "Error - can't write to unix socket: %s\n", strerror(errno) );
// 					close( unix_if.unix_sock );
// 					exit(EXIT_FAILURE);
//
// 				}
//
// 				if ( ! batch_mode )
// 					system( "clear" );
//
// 				while ( 1 ) {
//
// 					tv.tv_sec = 1;
// 					tv.tv_usec = 0;
// 					tmp_wait_sockets = wait_sockets;
//
// 					res = select( unix_if.unix_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv );
//
// 					if ( res > 0 ) {
//
// 						if ( ( recv_buff_len = read( unix_if.unix_sock, buff, sizeof( buff ) ) ) < 0 ) {
//
// 							printf( "Error - can't read from unix socket: %s\n", strerror(errno) );
// 							close( unix_if.unix_sock );
// 							exit(EXIT_FAILURE);
//
// 						} else if ( recv_buff_len > 0 ) {
//
// 							printf( "%s", buff );
//
// 						}
//
// 					/* timeout reached */
// 					} else if ( res == 0 ) {
//
// 						break;
//
// 					}  else if ( ( res < 0 ) && ( errno != EINTR ) ) {
//
// 						printf( "Error - can't select: %s\n", strerror(errno) );
// 						close( unix_if.unix_sock );
// 						exit(EXIT_FAILURE);
//
// 					}
//
// 				}
//
// 				if ( batch_mode )
// 					break;
//
// 				sleep( 1 );
//
// 			}

			close( unix_if.unix_sock );

		}

		exit(EXIT_SUCCESS);

	}

}



void init_interface ( struct batman_if *batman_if )
{
	struct ifreq int_req;
	int16_t on = 1;

	if ( strlen( batman_if->dev ) > IFNAMSIZ - 1 ) {
		debug_output( 0, "Error - interface name too long: %s\n", batman_if->dev );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	batman_if->udp_send_sock = socket( PF_INET, SOCK_DGRAM, 0 );
	if (batman_if->udp_send_sock < 0) {
		debug_output( 0, "Error - can't create send socket: %s", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	batman_if->udp_recv_sock = socket( PF_INET, SOCK_DGRAM, 0 );
	if ( batman_if->udp_recv_sock < 0 ) {

		debug_output( 0, "Error - can't create recieve socket: %s", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

	memset( &int_req, 0, sizeof (struct ifreq) );
	strncpy( int_req.ifr_name, batman_if->dev, IFNAMSIZ - 1 );

	if ( ioctl( batman_if->udp_recv_sock, SIOCGIFADDR, &int_req ) < 0 ) {

		debug_output( 0, "Error - can't get IP address of interface %s\n", batman_if->dev );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

	batman_if->addr.sin_family = AF_INET;
	batman_if->addr.sin_port = htons(PORT);
	batman_if->addr.sin_addr.s_addr = ((struct sockaddr_in *)&int_req.ifr_addr)->sin_addr.s_addr;

	if ( ioctl( batman_if->udp_recv_sock, SIOCGIFBRDADDR, &int_req ) < 0 ) {

		debug_output( 0, "Error - can't get broadcast IP address of interface %s\n", batman_if->dev );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

	batman_if->broad.sin_family = AF_INET;
	batman_if->broad.sin_port = htons(PORT);
	batman_if->broad.sin_addr.s_addr = ((struct sockaddr_in *)&int_req.ifr_broadaddr)->sin_addr.s_addr;

	if ( setsockopt( batman_if->udp_send_sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof (int) ) < 0 ) {

		debug_output( 0, "Error - can't enable broadcasts: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

	if ( bind_to_iface( batman_if->udp_send_sock, batman_if->dev ) < 0 ) {

		restore_defaults();
		exit(EXIT_FAILURE);

	}

	if ( bind( batman_if->udp_send_sock, (struct sockaddr *)&batman_if->addr, sizeof (struct sockaddr_in) ) < 0 ) {

		debug_output( 0, "Error - can't bind send socket: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

	if ( bind_to_iface( batman_if->udp_recv_sock, batman_if->dev ) < 0 ) {

		restore_defaults();
		exit(EXIT_FAILURE);

	}

	if ( bind( batman_if->udp_recv_sock, (struct sockaddr *)&batman_if->broad, sizeof (struct sockaddr_in) ) < 0 ) {

		debug_output( 0, "Error - can't bind receive socket: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);

	}

}




void init_interface_gw ( struct batman_if *batman_if )
{
	short on = 1;

	batman_if->addr.sin_port = htons(PORT + 1);
	batman_if->tcp_gw_sock = socket(PF_INET, SOCK_STREAM, 0);

	if ( batman_if->tcp_gw_sock < 0 ) {
		debug_output( 0, "Error - can't create socket: %s", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	if ( setsockopt( batman_if->tcp_gw_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int) ) < 0 ) {
		debug_output( 0, "Error - can't enable reuse of address: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	if ( bind( batman_if->tcp_gw_sock, (struct sockaddr*)&batman_if->addr, sizeof(struct sockaddr_in) ) < 0 ) {
		debug_output( 0, "Error - can't bind socket: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	if ( listen( batman_if->tcp_gw_sock, 10 ) < 0 ) {
		debug_output( 0, "Error - can't listen socket: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	batman_if->tunnel_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if ( batman_if->tunnel_sock < 0 ) {
		debug_output( 0, "Error - can't create tunnel socket: %s", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	if ( bind( batman_if->tunnel_sock, (struct sockaddr *)&batman_if->addr, sizeof (struct sockaddr_in) ) < 0 ) {
		debug_output( 0, "Error - can't bind tunnel socket: %s\n", strerror(errno) );
		restore_defaults();
		exit(EXIT_FAILURE);
	}

	batman_if->addr.sin_port = htons(PORT);

	pthread_create( &batman_if->listen_thread_id, NULL, &gw_listen, batman_if );

}



void *client_to_gw_tun( void *arg ) {

	struct curr_gw_data *curr_gw_data = (struct curr_gw_data *)arg;
	struct sockaddr_in gw_addr, my_addr, sender_addr;
	struct timeval tv;
	int32_t res, max_sock, status, buff_len, curr_gateway_tcp_sock, curr_gateway_tun_sock, curr_gateway_tun_fd, server_keep_alive_timeout;
	uint32_t addr_len;
	char curr_gateway_tun_if[IFNAMSIZ], keep_alive_string[] = "ping\0";
	unsigned char buff[1500];
	fd_set wait_sockets, tmp_wait_sockets;


	addr_len = sizeof (struct sockaddr_in);

	memset( &gw_addr, 0, sizeof(struct sockaddr_in) );
	memset( &my_addr, 0, sizeof(struct sockaddr_in) );

	gw_addr.sin_family = AF_INET;
	gw_addr.sin_port = htons(PORT + 1);
	gw_addr.sin_addr.s_addr = curr_gw_data->orig;

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(PORT + 1);
	my_addr.sin_addr.s_addr = curr_gw_data->batman_if->addr.sin_addr.s_addr;


	/* connect to server (ask permission) */
	if ( ( curr_gateway_tcp_sock = socket(PF_INET, SOCK_STREAM, 0) ) < 0 ) {

		debug_output( 0, "Error - can't create tcp socket: %s\n", strerror(errno) );
		curr_gateway = NULL;
		debugFree( arg, 1206 );
		return NULL;

	}

	if ( connect ( curr_gateway_tcp_sock, (struct sockaddr *)&gw_addr, sizeof(struct sockaddr) ) < 0 ) {

		debug_output( 0, "Error - can't connect to gateway: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );

		curr_gw_data->gw_node->last_failure = get_time();
		curr_gw_data->gw_node->unavail_factor++;

		curr_gateway = NULL;
		debugFree( arg, 1207 );
		return NULL;

	}

	server_keep_alive_timeout = get_time();


	/* connect to server (establish udp tunnel) */
	if ( ( curr_gateway_tun_sock = socket(PF_INET, SOCK_DGRAM, 0) ) < 0 ) {

		debug_output( 0, "Error - can't create udp socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		curr_gateway = NULL;
		debugFree( arg, 1208 );
		return NULL;

	}

	if ( bind( curr_gateway_tun_sock, (struct sockaddr *)&my_addr, sizeof (struct sockaddr_in) ) < 0) {

		debug_output( 0, "Error - can't bind tunnel socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		debugFree( arg, 1209 );
		return NULL;

	}


	if ( add_dev_tun( curr_gw_data->batman_if, curr_gw_data->batman_if->addr.sin_addr.s_addr, curr_gateway_tun_if, sizeof(curr_gateway_tun_if), &curr_gateway_tun_fd ) > 0 ) {

		add_del_route( 0, 0, 0, 0, curr_gateway_tun_if, curr_gw_data->batman_if->udp_send_sock );

	} else {

		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		debugFree( arg, 1210 );
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

	while ( ( !is_aborted() ) && ( curr_gateway != NULL ) && ( ! curr_gw_data->gw_node->deleted ) ) {


		if ( server_keep_alive_timeout + 30000 < get_time() ) {

			server_keep_alive_timeout = get_time();

			if ( write( curr_gateway_tcp_sock, keep_alive_string, sizeof( keep_alive_string ) ) < 0 ) {

				debug_output( 3, "server_keepalive failed: %s\n", strerror(errno) );

				curr_gw_data->gw_node->last_failure = get_time();
				curr_gw_data->gw_node->unavail_factor++;

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

					debug_output( 3, "server message ?\n" );

				} else if ( status < 0 ) {

					debug_output( 3, "Cannot read message from gateway: %s\n", strerror(errno) );

					break;

				} else if (status == 0) {

					debug_output( 3, "Gateway closed connection - timeout ?\n" );

					curr_gw_data->gw_node->last_failure = get_time();
					curr_gw_data->gw_node->unavail_factor++;

					break;

				}

			/* udp message (tunnel data) */
			} else if ( FD_ISSET( curr_gateway_tun_sock, &tmp_wait_sockets ) ) {

				if ( ( buff_len = recvfrom( curr_gateway_tun_sock, buff, sizeof( buff ), 0, (struct sockaddr *)&sender_addr, &addr_len ) ) < 0 ) {

					debug_output( 0, "Error - can't receive packet: %s\n", strerror(errno) );

				} else {

					if ( write( curr_gateway_tun_fd, buff, buff_len ) < 0 ) {

						debug_output( 0, "Error - can't write packet: %s\n", strerror(errno) );

					}

				}

			} else if ( FD_ISSET( curr_gateway_tun_fd, &tmp_wait_sockets ) ) {

				if ( ( buff_len = read( curr_gateway_tun_fd, buff, sizeof( buff ) ) ) < 0 ) {

					debug_output( 0, "Error - couldn't read data: %s\n", strerror(errno) );

				} else {

					if ( sendto(curr_gateway_tun_sock, buff, buff_len, 0, (struct sockaddr *)&gw_addr, sizeof (struct sockaddr_in) ) < 0 ) {
						debug_output( 0, "Error - can't send to gateway: %s\n", strerror(errno) );
					}

				}

			}

		} else if ( ( res < 0 ) && (errno != EINTR) ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
			break;

		}

	}

	/* cleanup */
	add_del_route( 0, 0, 0, 1, curr_gateway_tun_if, curr_gw_data->batman_if->udp_send_sock );

	close( curr_gateway_tcp_sock );
	close( curr_gateway_tun_sock );

	del_dev_tun( curr_gateway_tun_fd );

	curr_gateway = NULL;
	debugFree( arg, 1211 );

	return NULL;

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
		debugFree( curr_gw_data, 1212 );
		curr_gateway = NULL;

	}

	return 1;

}



void restore_defaults() {

	struct list_head *if_pos, *if_pos_tmp;
	struct batman_if *batman_if;


	stop = 1;

	list_for_each_safe( if_pos, if_pos_tmp, &if_list ) {

		batman_if = list_entry( if_pos, struct batman_if, list );

		if ( batman_if->listen_thread_id != 0 ) {

			pthread_join( batman_if->listen_thread_id, NULL );
			close( batman_if->tcp_gw_sock );

		}

		close( batman_if->udp_recv_sock );
		close( batman_if->udp_send_sock );

		list_del( if_pos );
		debugFree( if_pos, 1213 );

	}

	if ( ( routing_class != 0 ) && ( curr_gateway != NULL ) )
		del_default_route();

	if ( vis_if.sock )
		close( vis_if.sock );

	if ( unix_if.unix_sock )
		close( unix_if.unix_sock );

	if ( unix_if.listen_thread_id != 0 )
		pthread_join( unix_if.listen_thread_id, NULL );

	unlink( UNIX_PATH );

	if ( debug_level == 0 )
		closelog();

}



int8_t receive_packet( unsigned char *packet_buff, int32_t packet_buff_len, int16_t *hna_buff_len, uint32_t *neigh, uint32_t timeout, struct batman_if **if_incoming ) {

	struct sockaddr_in addr;
	struct timeval tv;
	struct list_head *if_pos;
	struct batman_if *batman_if;
	uint32_t addr_len;
	int8_t res;
	fd_set tmp_wait_set = receive_wait_set;


	addr_len = sizeof(struct sockaddr_in);

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

			if ( *hna_buff_len < sizeof(struct packet) )
				return 0;

			((struct packet *)packet_buff)->seqno = ntohs( ((struct packet *)packet_buff)->seqno ); /* network to host order for our 16bit seqno.*/

			(*if_incoming) = batman_if;
			break;

		}

	}

	*neigh = addr.sin_addr.s_addr;

	return 1;

}



int8_t send_packet( unsigned char *packet_buff, int32_t packet_buff_len, struct sockaddr_in *broad, int32_t send_sock ) {

	if ( sendto( send_sock, packet_buff, packet_buff_len, 0, (struct sockaddr *)broad, sizeof(struct sockaddr_in) ) < 0 ) {

		if ( errno == 1 ) {

			debug_output( 0, "Error - can't send packet: %s.\nDoes your firewall allow outgoing packets on port %i ?\n", strerror(errno), ntohs( broad->sin_port ) );

		} else {

			debug_output( 0, "Error - can't send packet: %s.\n", strerror(errno) );

		}

		return -1;

	}

	return 0;

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
	int32_t res, status, max_sock_min, max_sock, buff_len, tun_fd;
	uint32_t addr_len, client_timeout;
	unsigned char buff[1500];
	fd_set wait_sockets, tmp_wait_sockets;


	addr_to_string(batman_if->addr.sin_addr.s_addr, gw_addr, sizeof (gw_addr));
	addr_len = sizeof (struct sockaddr_in);
	client_timeout = get_time();

	if ( inet_pton(AF_INET, tun_ip, &tmp_ip_holder) < 1 ) {

		debug_output( 0, "Error - invalid tunnel IP specified: %s\n", tun_ip );
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

				gw_client = debugMalloc( sizeof(struct gw_client), 208 );
				memset( gw_client, 0, sizeof(struct gw_client) );

				if ( ( gw_client->sock = accept(batman_if->tcp_gw_sock, (struct sockaddr *)&gw_client->addr, &sin_size) ) == -1 ) {
					debug_output( 0, "Error - can't accept client packet: %s\n", strerror(errno) );
					continue;
				}

				INIT_LIST_HEAD(&gw_client->list);
				gw_client->batman_if = batman_if;
				gw_client->last_keep_alive = get_time();

				FD_SET(gw_client->sock, &wait_sockets);
				if ( gw_client->sock > max_sock )
					max_sock = gw_client->sock;

				list_add_tail(&gw_client->list, &batman_if->client_list);

				addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
				debug_output( 3, "gateway: %s (%s) got connection from %s (internet via %s)\n", gw_addr, batman_if->dev, str2, tun_dev );

			/* tunnel activity */
			} else if ( FD_ISSET( batman_if->tunnel_sock, &tmp_wait_sockets ) ) {

				if ( ( buff_len = recvfrom( batman_if->tunnel_sock, buff, sizeof( buff ), 0, (struct sockaddr *)&addr, &addr_len ) ) < 0 ) {

					debug_output( 0, "Error - can't receive packet: %s\n", strerror(errno) );

				} else {

					if ( write( tun_fd, buff, buff_len ) < 0 ) {

						debug_output( 0, "Error - can't write packet: %s\n", strerror(errno) );

					}

				}

			/* /dev/tunX activity */
			} else if ( FD_ISSET( tun_fd, &tmp_wait_sockets ) ) {

				/* not needed - kernel knows client adress and routes traffic directly */

				debug_output( 0, "Warning - data coming through tun device: %s\n", tun_dev );

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

						addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));

						status = read( gw_client->sock, buff, sizeof( buff ) );

						if ( status > 0 ) {

							gw_client->last_keep_alive = get_time();

							if ( gw_client->sock > max_sock )
								max_sock = gw_client->sock;

							debug_output( 3, "gateway: client %s sent keep alive on interface %s\n", str2, batman_if->dev );

						} else {

							if ( status < 0 ) {

								debug_output( 0, "Error - can't read message: %s\n", strerror(errno) );

							} else {

								debug_output( 3, "Client %s closed connection ...\n", str2 );

							}

							FD_CLR(gw_client->sock, &wait_sockets);
							close( gw_client->sock );

							list_del( client_pos );
							debugFree( client_pos, 1214 );

						}

					} else {

						if ( gw_client->sock > max_sock )
							max_sock = gw_client->sock;

					}

				}

			}

		} else if ( ( res < 0 ) && (errno != EINTR) ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
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

					addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
					debug_output( 3, "gateway: client %s timeout on interface %s\n", str2, batman_if->dev );

					list_del( client_pos );
					debugFree( client_pos, 1215 );

				} else {

					if ( gw_client->sock > max_sock )
						max_sock = gw_client->sock;

				}

			}

		}

	}

	/* delete tun devices on exit */
	del_dev_tun( tun_fd );

	list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

		gw_client = list_entry(client_pos, struct gw_client, list);

		list_del( client_pos );
		debugFree( client_pos, 1216 );

	}

	return NULL;

}



void segmentation_fault( int32_t sig ) {

	restore_defaults();

	debug_output( 0, "Error - SIGSEGV received !\n" );

	exit(EXIT_FAILURE);

}



void cleanup() {

	int8_t i;
	struct debug_level_info *debug_level_info;
	struct list_head *debug_pos, *debug_pos_tmp;


	for ( i = 0; i < 4; i++ ) {

		if ( debug_clients.clients_num[i] > 0 ) {

			list_for_each_safe( debug_pos, debug_pos_tmp, (struct list_head *)debug_clients.fd_list[i] ) {

				debug_level_info = list_entry(debug_pos, struct debug_level_info, list);

				list_del( debug_pos );
				debugFree( debug_pos, 1217 );

			}

		}

		debugFree( debug_clients.fd_list[i], 1218 );

	}

}

