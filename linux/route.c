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



#include <sys/ioctl.h>
#include <arpa/inet.h>    /* inet_ntop() */
#include <errno.h>
#include <unistd.h>       /* close() */
#include <linux/if.h>     /* ifr_if, ifr_tun */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>


#include "os.h"
#include "batman.h"



/***
 *
 * route types: 0 = RTN_UNICAST, 1 = THROW, 2 = UNREACHABLE
 *
 ***/

void add_del_route( uint32_t dest, uint8_t netmask, uint32_t router, int ifi, char *dev, int8_t rt_table, int8_t route_type, int8_t del ) {

	int netlink_sock, len;
	uint32_t my_router;
	char buf[4096], str1[16], str2[16];
	struct rtattr *rta;
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, sizeof(buf) };
	struct msghdr msg;
	struct nlmsghdr *nh;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char buff[3 * ( sizeof(struct rtattr) + 4 )];
	} req;


	inet_ntop( AF_INET, &dest, str1, sizeof (str1) );
	inet_ntop( AF_INET, &router, str2, sizeof (str2) );


	if ( router == dest ) {

		if ( dest == 0 ) {

			debug_output( 3, "%s default route via %s (table %i)\n", del ? "Deleting" : "Adding", dev, rt_table );
			debug_output( 4, "%s default route via %s (table %i)\n", del ? "Deleting" : "Adding", dev, rt_table );
			my_router = router;

		} else {

			debug_output( 3, "%s route to %s via 0.0.0.0 (table %i - %s)\n", del ? "Deleting" : "Adding", str1, rt_table, dev );
			debug_output( 4, "%s route to %s via 0.0.0.0 (table %i - %s)\n", del ? "Deleting" : "Adding", str1, rt_table, dev );
			my_router = 0;

		}

	} else {

		debug_output( 3, "%s %s to %s/%i via %s (table %i - %s)\n", del ? "Deleting" : "Adding", ( route_type == 1 ? "throw route" : ( route_type == 2 ? "unreachable route" : "route" ) ), str1, netmask, str2, rt_table, dev );
		debug_output( 4, "%s %s to %s/%i via %s (table %i - %s)\n", del ? "Deleting" : "Adding", ( route_type == 1 ? "throw route" : ( route_type == 2 ? "unreachable route" : "route" ) ), str1, netmask, str2, rt_table, dev );
		my_router = router;

	}


	memset( &nladdr, 0, sizeof(struct sockaddr_nl) );
	memset( &req, 0, sizeof(req) );
	memset( &msg, 0, sizeof(struct msghdr) );

	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = NLMSG_LENGTH( sizeof(struct rtmsg) + 3 * ( sizeof(struct rtattr) + 4 ) );
	req.nlh.nlmsg_pid = getpid();
	req.rtm.rtm_family = AF_INET;
	req.rtm.rtm_table = rt_table;
	req.rtm.rtm_dst_len = netmask;

	if ( del ) {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nlh.nlmsg_type = RTM_DELROUTE;
		req.rtm.rtm_scope = RT_SCOPE_NOWHERE;

	} else {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
		req.nlh.nlmsg_type = RTM_NEWROUTE;
		req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
		req.rtm.rtm_protocol = RTPROT_STATIC;          /* may be changed to some batman specific value - see <linux/rtnetlink.h> */
		req.rtm.rtm_type = ( route_type == 1 ? RTN_THROW : ( route_type == 2 ? RTN_UNREACHABLE : RTN_UNICAST ) );

	}

	rta = (struct rtattr *)req.buff;
	rta->rta_type = RTA_DST;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + sizeof(struct rtattr), (char *)&dest, 4 );

	if ( route_type == 0 ) {

		rta = (struct rtattr *)(req.buff + sizeof(struct rtattr) + 4);
		rta->rta_type = RTA_GATEWAY;
		rta->rta_len = sizeof(struct rtattr) + 4;
		memcpy( ((char *)&req.buff) + 2 * sizeof(struct rtattr) + 4, (char *)&my_router, 4 );

		rta = (struct rtattr *)(req.buff + 2 * sizeof(struct rtattr) + 8);
		rta->rta_type = RTA_OIF;
		rta->rta_len = sizeof(struct rtattr) + 4;
		memcpy( ((char *)&req.buff) + 3 * sizeof(struct rtattr) + 8, (char *)&ifi, 4 );

	}

	if ( ( netlink_sock = socket( PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE ) ) < 0 ) {

		debug_output( 0, "Error - can't create netlink socket for routing table manipulation: %s", strerror(errno) );
		return;

	}


	if ( sendto( netlink_sock, &req, req.nlh.nlmsg_len, 0, (struct sockaddr *)&nladdr, sizeof(struct sockaddr_nl) ) < 0 ) {

		debug_output( 0, "Error - can't send message to kernel via netlink socket for routing table manipulation: %s", strerror(errno) );
		return;

	}


	msg.msg_name = (void *)&nladdr;
	msg.msg_namelen = sizeof(nladdr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	len = recvmsg( netlink_sock, &msg, 0 );
	nh = (struct nlmsghdr *)buf;

	while ( NLMSG_OK(nh, len) ) {

		if ( nh->nlmsg_type == NLMSG_DONE )
			return;

		if ( ( nh->nlmsg_type == NLMSG_ERROR ) && ( ((struct nlmsgerr*)NLMSG_DATA(nh))->error != 0 ) )
			debug_output( 0, "Error - can't %s %s to %s/%i via %s (table %i): %s\n", del ? "delete" : "add", ( route_type == 1 ? "throw route" : ( route_type == 2 ? "unreachable route" : "route" ) ), str1, netmask, str2, rt_table, strerror(-((struct nlmsgerr*)NLMSG_DATA(nh))->error) );

		nh = NLMSG_NEXT( nh, len );

	}

}




void add_del_rule( uint32_t network, uint8_t netmask, int8_t rt_table, uint32_t prio, int8_t dst_rule, int8_t del ) {

	int netlink_sock, len;
	char buf[4096], str1[16];
	struct rtattr *rta;
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, sizeof(buf) };
	struct msghdr msg;
	struct nlmsghdr *nh;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char buff[2 * ( sizeof(struct rtattr) + 4 )];
	} req;


	memset( &nladdr, 0, sizeof(struct sockaddr_nl) );
	memset( &req, 0, sizeof(req) );
	memset( &msg, 0, sizeof(struct msghdr) );

	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = NLMSG_LENGTH( sizeof(struct rtmsg) + 2 * ( sizeof(struct rtattr) + 4 ));
	req.nlh.nlmsg_pid = getpid();
	req.rtm.rtm_family = AF_INET;
	req.rtm.rtm_table = rt_table;

	if ( dst_rule )
		req.rtm.rtm_dst_len = netmask;
	else
		req.rtm.rtm_src_len = netmask;

	if ( del ) {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nlh.nlmsg_type = RTM_DELRULE;
		req.rtm.rtm_scope = RT_SCOPE_NOWHERE;

	} else {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
		req.nlh.nlmsg_type = RTM_NEWRULE;
		req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
		req.rtm.rtm_protocol = RTPROT_STATIC;
		req.rtm.rtm_type = RTN_UNICAST;

	}

	rta = (struct rtattr *)req.buff;
	rta->rta_type = ( dst_rule ? RTA_DST : RTA_SRC );
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + sizeof(struct rtattr), (char *)&network, 4 );

	if ( prio != 0 ) {

		rta = (struct rtattr *)(req.buff + sizeof(struct rtattr) + 4);
		rta->rta_type = RTA_PRIORITY;
		rta->rta_len = sizeof(struct rtattr) + 4;
		memcpy( ((char *)&req.buff) + 2 * sizeof(struct rtattr) + 4, (char *)&prio, 4 );

	}


	if ( ( netlink_sock = socket( PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE ) ) < 0 ) {

		debug_output( 0, "Error - can't create netlink socket for routing rule manipulation: %s", strerror(errno) );
		return;

	}


	if ( sendto( netlink_sock, &req, req.nlh.nlmsg_len, 0, (struct sockaddr *)&nladdr, sizeof(struct sockaddr_nl) ) < 0 ) {

		debug_output( 0, "Error - can't send message to kernel via netlink socket for routing rule manipulation: %s", strerror(errno) );
		return;

	}


	msg.msg_name = (void *)&nladdr;
	msg.msg_namelen = sizeof(nladdr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	len = recvmsg( netlink_sock, &msg, 0 );
	nh = (struct nlmsghdr *)buf;

	while ( NLMSG_OK(nh, len) ) {

		if ( nh->nlmsg_type == NLMSG_DONE )
			return;

		if ( ( nh->nlmsg_type == NLMSG_ERROR ) && ( ((struct nlmsgerr*)NLMSG_DATA(nh))->error != 0 ) ) {

			inet_ntop( AF_INET, &network, str1, sizeof (str1) );

			debug_output( 0, "Error - can't %s rule %s %s/%i: %s\n", del ? "delete" : "add", ( dst_rule ? "to" : "from" ), str1, netmask, strerror(-((struct nlmsgerr*)NLMSG_DATA(nh))->error) );

		}

		nh = NLMSG_NEXT( nh, len );

	}

}



int add_del_interface_rules( int8_t del ) {

	int32_t tmp_fd;
	uint32_t len, addr, netaddr;
	uint8_t netmask, if_count = 0;
	char *buf, *buf_ptr;
	struct ifconf ifc;
	struct ifreq *ifr, ifr_tmp;


	tmp_fd = socket( AF_INET, SOCK_DGRAM, 0 );

	if ( tmp_fd < 0 ) {
		debug_output( 0, "Error - can't %s interface rules (udp socket): %s\n", del ? "delete" : "add", strerror(errno) );
		return -1;
	}


	len = 10 * sizeof(struct ifreq); /* initial buffer size guess (10 interfaces) */

	while ( 1 ) {

		buf = debugMalloc( len, 601 );
		ifc.ifc_len = len;
		ifc.ifc_buf = buf;

		if ( ioctl( tmp_fd, SIOCGIFCONF, &ifc ) < 0 ) {

			debug_output( 0, "Error - can't %s interface rules (SIOCGIFCONF): %s\n", del ? "delete" : "add", strerror(errno) );
			close( tmp_fd );
			return -1;

		} else {

			if ( ifc.ifc_len <= len )
				break;

		}

		len += 10 * sizeof(struct ifreq);
		debugFree( buf, 1601 );

	}

	for ( buf_ptr = buf; buf_ptr < buf + ifc.ifc_len; ) {

		ifr = (struct ifreq *)buf_ptr;
		buf_ptr += ( ifr->ifr_addr.sa_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr) ) + sizeof(ifr->ifr_name);

		/* ignore if not IPv4 interface */
		if ( ifr->ifr_addr.sa_family != AF_INET )
			continue;


		memset( &ifr_tmp, 0, sizeof (struct ifreq) );
		strncpy( ifr_tmp.ifr_name, ifr->ifr_name, IFNAMSIZ - 1 );

		if ( ioctl( tmp_fd, SIOCGIFFLAGS, &ifr_tmp ) < 0 ) {

			debug_output( 0, "Error - can't get flags of interface %s (interface rules): %s\n", ifr->ifr_name, strerror(errno) );
			close( tmp_fd );
			debugFree( buf, 1602 );
			return -1;

		}

		/* ignore if not up and running */
		if ( !( ifr_tmp.ifr_flags & IFF_UP ) || !( ifr_tmp.ifr_flags & IFF_RUNNING ) )
			continue;


		if ( ioctl( tmp_fd, SIOCGIFADDR, &ifr_tmp ) < 0 ) {

			debug_output( 0, "Error - can't get IP address of interface %s (interface rules): %s\n", ifr->ifr_name, strerror(errno) );
			close( tmp_fd );
			debugFree( buf, 1603 );
			return -1;

		}

		addr = ((struct sockaddr_in *)&ifr_tmp.ifr_addr)->sin_addr.s_addr;

		if ( ioctl( tmp_fd, SIOCGIFNETMASK, &ifr_tmp ) < 0 ) {

			debug_output( 0, "Error - can't get netmask address of interface %s (interface rules): %s\n", ifr->ifr_name, strerror(errno) );
			close( tmp_fd );
			debugFree( buf, 1604 );
			return -1;

		}

		netaddr = ( ((struct sockaddr_in *)&ifr_tmp.ifr_addr)->sin_addr.s_addr & addr );
		netmask = bit_count( ((struct sockaddr_in *)&ifr_tmp.ifr_addr)->sin_addr.s_addr );

		add_del_route( netaddr, netmask, 0, 0, ifr->ifr_name, BATMAN_RT_TABLE_TUNNEL, 1, del );

		if ( is_batman_if( ifr->ifr_name ) )
			continue;

		add_del_rule( netaddr, netmask, BATMAN_RT_TABLE_TUNNEL, ( del ? 0 : BATMAN_RT_PRIO_TUNNEL + if_count ), 0, del );

		if_count++;

	}

	close( tmp_fd );
	debugFree( buf, 1605 );

	return 1;

}

