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
#include <fcntl.h>        /* open(), O_RDWR */


#include "../os.h"
#include "../batman.h"



int8_t get_tun_ip( struct sockaddr_in *gw_addr, int32_t udp_sock, uint32_t *tun_addr ) {

	struct sockaddr_in sender_addr;
	struct timeval tv;
	unsigned char buff[100];
	int32_t res, buff_len;
	uint32_t addr_len;
	int8_t i = 12;
	fd_set wait_sockets;


	addr_len = sizeof(struct sockaddr_in);
	memset( &buff, 0, sizeof(buff) );
	buff[0] = 2;

	while ( ( !is_aborted() ) && ( curr_gateway != NULL ) && ( i > 0 ) ) {

		if ( sendto( udp_sock, buff, sizeof(buff), 0, (struct sockaddr *)gw_addr, sizeof(struct sockaddr_in) ) < 0 ) {

			debug_output( 0, "Error - can't send ip request to gateway: %s \n", strerror(errno) );

		} else {

			tv.tv_sec = 0;
			tv.tv_usec = 250;

			FD_ZERO(&wait_sockets);
			FD_SET(udp_sock, &wait_sockets);

			res = select( udp_sock + 1, &wait_sockets, NULL, NULL, &tv );

			if ( res > 0 ) {

				/* gateway message */
				if ( FD_ISSET( udp_sock, &wait_sockets ) ) {

					if ( ( buff_len = recvfrom( udp_sock, buff, sizeof(buff) - 1, 0, (struct sockaddr *)&sender_addr, &addr_len ) ) < 0 ) {

						debug_output( 0, "Error - can't receive ip request: %s \n", strerror(errno) );

					} else {

						if ( ( sender_addr.sin_addr.s_addr == gw_addr->sin_addr.s_addr ) && ( buff_len > 4 ) ) {

							memcpy( tun_addr, buff + 1, 4 );
							return 1;

						} else {

							debug_output( 0, "Error - can't receive ip request: sender IP or packet size (%i) do not match \n", buff_len );

						}

					}

				}

			} else if ( ( res < 0 ) && ( errno != EINTR ) ) {

				debug_output( 0, "Error - can't select: %s \n", strerror(errno) );
				break;

			}

		}

		i--;

	}

	if ( i == 0 )
		debug_output( 0, "Error - can't receive ip from gateway: number of maximum retries reached \n" );

	return -1;

}



void *client_to_gw_tun( void *arg ) {

	struct curr_gw_data *curr_gw_data = (struct curr_gw_data *)arg;
	struct sockaddr_in gw_addr, my_addr, sender_addr;
	struct timeval tv;
	int32_t res, max_sock, buff_len, udp_sock, tun_fd, tun_ifi, sock_opts;
	uint32_t addr_len, client_timeout = 0, got_new_ip = 0, my_tun_addr = 0;
	char tun_if[IFNAMSIZ], my_str[ADDR_STR_LEN], gw_str[ADDR_STR_LEN];
	unsigned char buff[1501];
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


	/* connect to server (establish udp tunnel) */
	if ( ( udp_sock = socket( PF_INET, SOCK_DGRAM, 0 ) ) < 0 ) {

		debug_output( 0, "Error - can't create udp socket: %s\n", strerror(errno) );
		curr_gateway = NULL;
		debugFree( arg, 1209 );
		return NULL;

	}

	if ( bind( udp_sock, (struct sockaddr *)&my_addr, sizeof (struct sockaddr_in) ) < 0 ) {

		debug_output( 0, "Error - can't bind tunnel socket: %s\n", strerror(errno) );
		close( udp_sock );
		curr_gateway = NULL;
		debugFree( arg, 1210 );
		return NULL;

	}


	/* make udp socket non blocking */
	sock_opts = fcntl( udp_sock, F_GETFL, 0 );
	fcntl( udp_sock, F_SETFL, sock_opts | O_NONBLOCK );


	if ( add_dev_tun( curr_gw_data->batman_if, 0, tun_if, sizeof(tun_if), &tun_fd, &tun_ifi ) > 0 ) {

		add_del_route( 0, 0, 0, tun_ifi, tun_if, BATMAN_RT_TABLE_TUNNEL, 0, 0 );

	} else {

		close( udp_sock );
		curr_gateway = NULL;
		debugFree( arg, 1211 );
		return NULL;

	}


	addr_to_string( curr_gw_data->orig, gw_str, sizeof(gw_str) );


	FD_ZERO(&wait_sockets);
	FD_SET(udp_sock, &wait_sockets);
	FD_SET(tun_fd, &wait_sockets);

	max_sock = ( udp_sock > tun_fd ? udp_sock : tun_fd );

	while ( ( !is_aborted() ) && ( curr_gateway != NULL ) && ( ! curr_gw_data->gw_node->deleted ) ) {

		tv.tv_sec = 0;
		tv.tv_usec = 250;

		memcpy( &tmp_wait_sockets, &wait_sockets, sizeof(fd_set) );

		res = select( max_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv );

		if ( res > 0 ) {

			/* udp message (tunnel data) */
			if ( FD_ISSET( udp_sock, &tmp_wait_sockets ) ) {

				while ( ( buff_len = recvfrom( udp_sock, buff, sizeof(buff) - 1, 0, (struct sockaddr *)&sender_addr, &addr_len ) ) > 0 ) {

					if ( ( buff_len > 1 ) && ( sender_addr.sin_addr.s_addr == gw_addr.sin_addr.s_addr ) ) {

						/* got data from gateway */
						if ( buff[0] == 1 ) {

							if ( write( tun_fd, buff + 1, buff_len - 1 ) < 0 )
								debug_output( 0, "Error - can't write packet: %s\n", strerror(errno) );

						/* gateway told us that we have no valid ip */
						} else if ( buff[0] == 3 ) {

							addr_to_string( my_tun_addr, my_str, sizeof(my_str) );
							debug_output( 3, "Gateway client - gateway (%s) says: IP (%s) is expired \n", gw_str, my_str );

							if ( get_tun_ip( &gw_addr, udp_sock, &my_tun_addr ) < 0 )
								break;

							addr_to_string( my_tun_addr, my_str, sizeof(my_str) );
							debug_output( 3, "Gateway client - got IP (%s) from gateway: %s \n", my_str, gw_str );

							if ( set_tun_addr( udp_sock, my_tun_addr, tun_if ) < 0 )
								break;

						}

					} else {

						addr_to_string( sender_addr.sin_addr.s_addr, my_str, sizeof(my_str) );
						debug_output( 0, "Error - ignoring gateway packet from %s: packet too small (%i)\n", my_str, buff_len );

					}

				}

				if ( errno != EWOULDBLOCK ) {

					debug_output( 0, "Error - gateway client can't receive packet: %s\n", strerror(errno) );
					break;

				}

				client_timeout = get_time();

			} else if ( FD_ISSET( tun_fd, &tmp_wait_sockets ) ) {

				while ( ( buff_len = read( tun_fd, buff + 1, sizeof(buff) - 2 ) ) > 0 ) {

					if ( my_tun_addr == 0 ) {

						if ( get_tun_ip( &gw_addr, udp_sock, &my_tun_addr ) < 0 )
							break;

						addr_to_string( my_tun_addr, my_str, sizeof(my_str) );
						debug_output( 3, "Gateway client - got IP (%s) from gateway: %s \n", my_str, gw_str );

						if ( set_tun_addr( udp_sock, my_tun_addr, tun_if ) < 0 )
							break;

						client_timeout = get_time();
						got_new_ip = client_timeout;

					}

					buff[0] = 1;

					/* fill in new ip - the packets in the buffer don't know it yet */
					if ( got_new_ip + 1000 > client_timeout )
						memcpy( buff + 13, (unsigned char *)&my_tun_addr, 4 );

					if ( sendto( udp_sock, buff, buff_len + 1, 0, (struct sockaddr *)&gw_addr, sizeof (struct sockaddr_in) ) < 0 )
						debug_output( 0, "Error - can't send data to gateway: %s\n", strerror(errno) );

				}

				if ( errno != EWOULDBLOCK ) {

					debug_output( 0, "Error - gateway client can't read tun data: %s\n", strerror(errno) );
					break;

				}

			}

		} else if ( ( res < 0 ) && ( errno != EINTR ) ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
			break;

		}


		/* drop unused IP */
		if ( ( my_tun_addr != 0 ) && ( ( client_timeout + 120000 ) < get_time() ) ) {

			addr_to_string( my_tun_addr, my_str, sizeof(my_str) );
			debug_output( 3, "Gateway client - releasing unused IP after timeout: %s \n", my_str );

			my_tun_addr = 0;

			if ( set_tun_addr( udp_sock, my_tun_addr, tun_if ) < 0 )
				break;

			/* kernel deletes routes after setting the interface ip to 0 */
			add_del_route( 0, 0, 0, tun_ifi, tun_if, BATMAN_RT_TABLE_TUNNEL, 0, 0 );

		}

	}

	/* cleanup */
	add_del_route( 0, 0, 0, tun_ifi, tun_if, BATMAN_RT_TABLE_TUNNEL, 0, 1 );

	close( udp_sock );

	del_dev_tun( tun_fd );

	curr_gateway = NULL;
	debugFree( arg, 1212 );

	return NULL;

}



int8_t get_ip_addr( uint32_t client_addr, char *ip_buff, struct gw_client *gw_client[] ) {

	uint8_t i, first_free = 0;

	for ( i = 1; i < 255; i++ ) {

		if ( gw_client[i] != NULL ) {

			if ( gw_client[i]->addr == client_addr ) {

				ip_buff[0] = i;
				return 1;

			}

		} else {

			if ( first_free == 0 )
				first_free = i;

		}

	}

	if ( first_free == 0 ) {

		debug_output( 0, "Error - can't get IP for client: maximum number of clients reached\n" );
		return -1;

	}

	gw_client[first_free] = debugMalloc( sizeof(struct gw_client), 208 );

	gw_client[first_free]->addr = client_addr;
	gw_client[first_free]->last_keep_alive = get_time();

	ip_buff[0] = first_free;

	return 1;

}



void *gw_listen( void *arg ) {

	struct batman_if *batman_if = (struct batman_if *)arg;
	struct timeval tv;
	struct sockaddr_in addr, client_addr;
	struct gw_client *gw_client[256];
	char gw_addr[16], str[16], tun_dev[IFNAMSIZ], ip_buff[1];
	unsigned char buff[1501];
	int32_t res, max_sock, buff_len, tun_fd, tun_ifi;
	uint32_t addr_len, client_timeout, current_time, my_tun_ip, tmp_client_ip;
	uint8_t i;
	fd_set wait_sockets, tmp_wait_sockets;


	ip_buff[0] = 0;
	my_tun_ip = 169 + ( 254<<8 ) + ( batman_if->if_num<<16 ) + ( ip_buff[0]<<24 );

	addr_len = sizeof (struct sockaddr_in);
	client_timeout = get_time();

	for ( i = 0; i < 255; i++ ) {
		gw_client[i] = NULL;
	}

	client_addr.sin_family = AF_INET;
	client_addr.sin_port = htons(PORT + 1);

	if ( add_dev_tun( batman_if, my_tun_ip, tun_dev, sizeof(tun_dev), &tun_fd, &tun_ifi ) < 0 )
		return NULL;

	add_del_route( my_tun_ip, 24, 0, tun_ifi, tun_dev, 254, 0, 0 );


	FD_ZERO(&wait_sockets);
	FD_SET(batman_if->udp_tunnel_sock, &wait_sockets);
	FD_SET(tun_fd, &wait_sockets);

	max_sock = ( batman_if->udp_tunnel_sock > tun_fd ? batman_if->udp_tunnel_sock : tun_fd );

	while ( !is_aborted() ) {

		tv.tv_sec = 0;
		tv.tv_usec = 250;
		memcpy( &tmp_wait_sockets, &wait_sockets, sizeof(fd_set) );

		res = select( max_sock + 1, &tmp_wait_sockets, NULL, NULL, &tv );

		if ( res > 0 ) {

			/* is udp packet */
			if ( FD_ISSET( batman_if->udp_tunnel_sock, &tmp_wait_sockets ) ) {

				while ( ( buff_len = recvfrom( batman_if->udp_tunnel_sock, buff, sizeof(buff) - 1, 0, (struct sockaddr *)&addr, &addr_len ) ) > 0 ) {

					if ( buff_len > 1 ) {

						if ( buff[0] == 1 ) {

							/* check whether client IP is known */
							if ( ( buff[13] != 169 ) || ( buff[14] != 254 ) || ( buff[15] != batman_if->if_num ) || ( gw_client[(uint8_t)buff[16]] == NULL ) || ( gw_client[(uint8_t)buff[16]]->addr != addr.sin_addr.s_addr ) ) {

								buff[0] = 3;
								addr_to_string( addr.sin_addr.s_addr, str, sizeof(str) );

								debug_output( 0, "Error - got packet from unknown client: %s (virtual ip %i.%i.%i.%i) \n", str, (uint8_t)buff[13], (uint8_t)buff[14], (uint8_t)buff[15], (uint8_t)buff[16] );

								if ( sendto( batman_if->udp_tunnel_sock, buff, buff_len, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in) ) < 0 )
									debug_output( 0, "Error - can't send invalid ip information to client (%s): %s \n", str, strerror(errno) );

								continue;

							}

							if ( write( tun_fd, buff + 1, buff_len - 1 ) < 0 )
								debug_output( 0, "Error - can't write packet: %s\n", strerror(errno) );

						} else if ( buff[0] == 2 ) {

							if ( get_ip_addr( addr.sin_addr.s_addr, ip_buff, gw_client ) > 0 ) {

								tmp_client_ip = my_tun_ip + ( ip_buff[0]<<24 );
								memcpy( buff + 1, (char *)&tmp_client_ip, 4 );

								if ( sendto( batman_if->udp_tunnel_sock, buff, 100, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_in) ) < 0 ) {

									addr_to_string( addr.sin_addr.s_addr, str, sizeof (str) );
									debug_output( 0, "Error - can't send requested ip to client (%s): %s \n", str, strerror(errno) );

								} else {

									addr_to_string( tmp_client_ip, str, sizeof(str) );
									addr_to_string( addr.sin_addr.s_addr, gw_addr, sizeof(gw_addr) );
									debug_output( 3, "Gateway - assigned %s to client: %s \n", str, gw_addr );

								}

							}

						}

					}

				}

				if ( errno != EWOULDBLOCK ) {

					debug_output( 0, "Error - gateway can't receive packet: %s\n", strerror(errno) );
					break;

				}

			/* /dev/tunX activity */
			} else if ( FD_ISSET( tun_fd, &tmp_wait_sockets ) ) {

				while ( ( buff_len = read( tun_fd, buff + 1, sizeof(buff) - 2 ) ) > 0 ) {

					ip_buff[0] = buff[20];

					if ( gw_client[(uint8_t)ip_buff[0]] != NULL ) {

						client_addr.sin_addr.s_addr = gw_client[(uint8_t)ip_buff[0]]->addr;
						gw_client[(uint8_t)ip_buff[0]]->last_keep_alive = get_time();

						/* addr_to_string( client_addr.sin_addr.s_addr, str, sizeof(str) );
						tmp_client_ip = buff[17] + ( buff[18]<<8 ) + ( buff[19]<<16 ) + ( buff[20]<<24 );
						addr_to_string( tmp_client_ip, gw_addr, sizeof(gw_addr) );
						debug_output( 3, "Gateway - packet resolved: %s for client %s \n", str, gw_addr ); */

						buff[0] = 1;

						if ( sendto( batman_if->udp_tunnel_sock, buff, buff_len + 1, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr_in) ) < 0 )
							debug_output( 0, "Error - can't send data to client (%s): %s \n", str, strerror(errno) );

					} else {

						tmp_client_ip = buff[17] + ( buff[18]<<8 ) + ( buff[19]<<16 ) + ( buff[20]<<24 );
						addr_to_string( tmp_client_ip, gw_addr, sizeof(gw_addr) );
						debug_output( 3, "Gateway - could not resolve packet: %s \n", gw_addr );

					}

				}

				if ( errno != EWOULDBLOCK ) {

					debug_output( 0, "Error - gateway can't read tun data: %s\n", strerror(errno) );
					break;

				}

			}

		} else if ( ( res < 0 ) && ( errno != EINTR ) ) {

			debug_output( 0, "Error - can't select: %s\n", strerror(errno) );
			break;

		}


		/* close unresponsive client connections (free unused IPs) */
		current_time = get_time();

		if ( ( client_timeout + 60000 ) < current_time ) {

			client_timeout = get_time();

			for ( i = 1; i < 255; i++ ) {

				if ( gw_client[i] != NULL ) {

					if ( ( gw_client[i]->last_keep_alive + 120000 ) < current_time ) {

						debugFree( gw_client[i], 1216 );
						gw_client[i] = NULL;

					}

				}

			}

		}

	}

	/* delete tun device and routes on exit */
	add_del_route( my_tun_ip, 24, 0, tun_ifi, tun_dev, 254, 0, 1 );

	del_dev_tun( tun_fd );


	for ( i = 1; i < 255; i++ ) {

		if ( gw_client[i] != NULL )
			debugFree( gw_client[i], 1217 );

	}


	return NULL;

}

