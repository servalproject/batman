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

uint32_t get_time( void );
void set_forwarding(int32_t state);
int32_t get_forwarding(void);
void set_rp_filter(int32_t state, char* dev);
int32_t get_rp_filter(char *dev);



void add_del_route( uint32_t dest, uint16_t netmask, uint32_t router, int8_t del, char *dev, int32_t sock );
int8_t is_aborted();
void addr_to_string( uint32_t addr, char *str, int32_t len );

int8_t receive_packet( unsigned char *packet_buff, int32_t packet_buff_len, int16_t *hna_buff_len, uint32_t *neigh, uint32_t timeout, struct batman_if **if_incoming );

int8_t send_packet( unsigned char *packet_buff, int packet_buff_len, struct sockaddr_in *broad, int send_sock );
int32_t rand_num( int32_t limit );
int8_t bind_to_iface( int32_t sock, char *dev );
int8_t probe_tun();
int8_t del_dev_tun( int32_t fd );
int8_t add_dev_tun( struct batman_if *batman_if, uint32_t dest_addr, char *tun_dev, size_t tun_dev_size, int32_t *fd );

void apply_init_args( int argc, char *argv[] );
void init_interface ( struct batman_if *batman_if );
void init_interface_gw ( struct batman_if *batman_if );

void print_animation( void );
void close_all_sockets();
void *gw_listen( void *arg );

void *client_to_gw_tun( void *arg );

void debug();
void debug_output( int8_t debug_prio, char *format, ... );
void cleanup();



#endif
