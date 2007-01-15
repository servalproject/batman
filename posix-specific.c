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



void apply_init_args( int argc, char *argv[] ) {

	struct in_addr tmp_ip_holder;
	struct batman_if *batman_if;
	struct hna_node *hna_node;
	struct ifreq int_req;
	short on = 1, found_args = 1, netmask;
	int optchar;
	char str1[16], str2[16], *slash_ptr;
	unsigned int vis_server = 0;

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
				exit(0);

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
				exit(0);

			case 'V':

				print_animation();

				printf( "\x1B[0;0HB.A.T.M.A.N.-III v%s (compability version %i)\n", SOURCE_VERSION, COMPAT_VERSION );
				printf( "\x1B[9;0H \t May the bat guide your path ...\n\n\n" );

				exit(0);

			case 'h':
			default:
				usage();
				exit(0);

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

}

