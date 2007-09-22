/*
 * Copyright (C) 2006 BATMAN contributors:
 * Thomas Lopatic, Corinna 'Elektra' Aichele, Axel Neumann, Marek Lindner, Andreas Langer
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

#include <linux/if.h>

/* io controls */
#define IOCSETDEV 1
#define IOCREMDEV 2

#define TRANSPORT_PACKET_SIZE 29
#define VIP_BUFFER_SIZE 5
#define BATMAN_PORT 4306

#define TUNNEL_DATA 0x01
#define TUNNEL_IP_REQUEST 0x02
#define TUNNEL_IP_INVALID 0x03

/* tunnel clients */
struct gw_client {
	uint32_t addr;
	uint32_t last_keep_alive;
};

struct dev_element {
	struct socket *sock;
	struct packet_type packet;
	struct gw_client *gw_client[254];
	uint32_t addr;
	char name[IFNAMSIZ];
	uint8_t free;
};
