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

#ifndef _BATMAN_OS_H
#define _BATMAN_OS_H

#include "batman-specific.h"

unsigned int get_time(void);
void set_forwarding(int);
int get_forwarding(void);
void set_rp_filter(int state, char* dev);
int get_rp_filter(char *dev);



void add_del_route( unsigned int dest, unsigned int netmask, unsigned int router, int del, char *dev, int sock );
int is_aborted();
void addr_to_string(unsigned int addr, char *str, int len);

int receive_packet(unsigned char *packet_buff, int packet_buff_len, int *hna_buff_len, unsigned int *neigh, unsigned int timeout, struct batman_if **if_incoming);

int send_packet(unsigned char *buff, int len, struct sockaddr_in *broad, int sock);
int rand_num(int limit);
int bind_to_iface( int udp_recv_sock, char *dev );
int probe_tun();
int del_dev_tun( int fd );
int add_dev_tun( struct batman_if *batman_if, unsigned int dest_addr, char *tun_dev, size_t tun_dev_size, int *fd );

void apply_init_args( int argc, char *argv[] );
void init_interface ( struct batman_if *batman_if );
void init_interface_gw ( struct batman_if *batman_if );

int print_animation( void );
void close_all_sockets();
void *gw_listen( void *arg );

void *client_to_gw_tun( void *arg );

void debug();
void debug_output( short debug_prio, char *format, ... );



#endif
