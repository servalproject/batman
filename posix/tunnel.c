/*
 * Copyright (C) 2006 BATMAN contributors:
 * Marek Lindner
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



#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <net/if.h>


#include "os.h"
#include "batman.h"



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

	if ( pthread_mutex_lock( &curr_gw_mutex ) == 0 ) {

		if ( connect ( curr_gateway_tcp_sock, (struct sockaddr *)&gw_addr, sizeof(struct sockaddr) ) < 0 ) {

			debug_output( 0, "Error - can't connect to gateway: %s\n", strerror(errno) );
			close( curr_gateway_tcp_sock );

			curr_gw_data->gw_node->last_failure = get_time();
			curr_gw_data->gw_node->unavail_factor++;

			curr_gateway = NULL;
			debugFree( arg, 1207 );

			if ( pthread_mutex_unlock( &curr_gw_mutex ) != 0 )
				debug_output( 0, "Error - could not unlock mutex (client_to_gw_tun => 1): %s \n", strerror( errno ) );

			return NULL;

		}

		if ( pthread_mutex_unlock( &curr_gw_mutex ) != 0 )
			debug_output( 0, "Error - could not unlock mutex (client_to_gw_tun => 2): %s \n", strerror( errno ) );

	} else {

		debug_output( 0, "Error - could not lock mutex (client_to_gw_tun => 1): %s \n", strerror( errno ) );
		close( curr_gateway_tcp_sock );

		curr_gateway = NULL;
		debugFree( arg, 1208 );
		return NULL;

	}

	server_keep_alive_timeout = get_time();


	/* connect to server (establish udp tunnel) */
	if ( ( curr_gateway_tun_sock = socket(PF_INET, SOCK_DGRAM, 0) ) < 0 ) {

		debug_output( 0, "Error - can't create udp socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		curr_gateway = NULL;
		debugFree( arg, 1209 );
		return NULL;

	}

	if ( bind( curr_gateway_tun_sock, (struct sockaddr *)&my_addr, sizeof (struct sockaddr_in) ) < 0) {

		debug_output( 0, "Error - can't bind tunnel socket: %s\n", strerror(errno) );
		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		debugFree( arg, 1210 );
		return NULL;

	}


	if ( add_dev_tun( curr_gw_data->batman_if, curr_gw_data->batman_if->addr.sin_addr.s_addr, curr_gateway_tun_if, sizeof(curr_gateway_tun_if), &curr_gateway_tun_fd ) > 0 ) {

		add_del_route( 0, 0, 0, curr_gw_data->batman_if->if_index, curr_gateway_tun_if, BATMAN_RT_TABLE_TUNNEL, 0, 0 );

	} else {

		close( curr_gateway_tcp_sock );
		close( curr_gateway_tun_sock );
		curr_gateway = NULL;
		debugFree( arg, 1211 );
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

			if ( pthread_mutex_lock( &curr_gw_mutex ) == 0 ) {

				if ( write( curr_gateway_tcp_sock, keep_alive_string, sizeof( keep_alive_string ) ) < 0 ) {

					debug_output( 3, "server_keepalive failed: %s\n", strerror(errno) );

					curr_gw_data->gw_node->last_failure = get_time();
					curr_gw_data->gw_node->unavail_factor++;

					if ( pthread_mutex_unlock( &curr_gw_mutex ) != 0 )
						debug_output( 0, "Error - could not unlock mutex (client_to_gw_tun => 3): %s \n", strerror( errno ) );

					break;

				}

				if ( pthread_mutex_unlock( &curr_gw_mutex ) != 0 )
					debug_output( 0, "Error - could not unlock mutex (client_to_gw_tun => 4): %s \n", strerror( errno ) );

			} else {

				debug_output( 0, "Error - could not lock mutex (client_to_gw_tun => 2): %s \n", strerror( errno ) );

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
	add_del_route( 0, 0, 0, curr_gw_data->batman_if->if_index, curr_gateway_tun_if, BATMAN_RT_TABLE_TUNNEL, 0, 1 );

	close( curr_gateway_tcp_sock );
	close( curr_gateway_tun_sock );

	del_dev_tun( curr_gateway_tun_fd );

	curr_gateway = NULL;
	debugFree( arg, 1212 );

	return NULL;

}



void *gw_listen( void *arg ) {

	struct batman_if *batman_if = (struct batman_if *)arg;
	struct gw_client *gw_client;
	struct list_head *client_pos, *client_pos_tmp, *prev_list_head;
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
		memcpy( &tmp_wait_sockets, &wait_sockets, sizeof(fd_set) );

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

				prev_list_head = (struct list_head *)&batman_if->client_list;

				list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

					gw_client = list_entry(client_pos, struct gw_client, list);

					if ( FD_ISSET( gw_client->sock, &tmp_wait_sockets ) ) {

						addr_to_string( gw_client->addr.sin_addr.s_addr, str2, sizeof (str2) );

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

							list_del( prev_list_head, client_pos, &batman_if->client_list );
							debugFree( client_pos, 1215 );

						}

					} else {

						if ( gw_client->sock > max_sock )
							max_sock = gw_client->sock;

					}

					prev_list_head = &gw_client->list;

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

			prev_list_head = (struct list_head *)&batman_if->client_list;

			list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

				gw_client = list_entry(client_pos, struct gw_client, list);

				if ( ( gw_client->last_keep_alive + 120000 ) < client_timeout ) {

					FD_CLR(gw_client->sock, &wait_sockets);
					close( gw_client->sock );

					addr_to_string(gw_client->addr.sin_addr.s_addr, str2, sizeof (str2));
					debug_output( 3, "gateway: client %s timeout on interface %s\n", str2, batman_if->dev );

					list_del( prev_list_head, client_pos, &batman_if->client_list );
					debugFree( client_pos, 1216 );

				} else {

					if ( gw_client->sock > max_sock )
						max_sock = gw_client->sock;

				}

				prev_list_head = &gw_client->list;

			}

		}

	}

	/* delete tun devices on exit */
	del_dev_tun( tun_fd );

	list_for_each_safe(client_pos, client_pos_tmp, &batman_if->client_list) {

		gw_client = list_entry(client_pos, struct gw_client, list);

		list_del( (struct list_head *)&batman_if->client_list, client_pos, &batman_if->client_list );
		debugFree( client_pos, 1217 );

	}

	return NULL;

}

