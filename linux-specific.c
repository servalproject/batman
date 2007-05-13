/*
 * Copyright (C) 2006 BATMAN contributors:
 * Thomas Lopatic
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
#include <netinet/ip.h>   /* iph */
#include <linux/if_tun.h> /* TUNSETPERSIST, ... */
#include <fcntl.h>        /* open(), O_RDWR */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include "os.h"
#include "batman-specific.h"



void add_del_route( uint32_t dest, uint8_t netmask, uint32_t router, int8_t del, int ifi, char *dev ) {

	int netlink_sock, len, my_router;
	char buf[4096], str1[16], str2[16];
	struct rtattr *rta;
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, sizeof(buf) };
	struct msghdr msg;
	struct nlmsghdr *nh;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char buff[3 * sizeof(struct rtattr) + 12];
	} req;


	inet_ntop( AF_INET, &dest, str1, sizeof (str1) );
	inet_ntop( AF_INET, &router, str2, sizeof (str2) );

	if ( router == dest ) {

		if ( dest == 0 ) {

			debug_output( 3, "%s default route via %s\n", del ? "Deleting" : "Adding", dev );
			debug_output( 4, "%s default route via %s\n", del ? "Deleting" : "Adding", dev );
			my_router = router;

		} else {

			debug_output( 3, "%s route to %s via 0.0.0.0 (%s)\n", del ? "Deleting" : "Adding", str1, dev );
			debug_output( 4, "%s route to %s via 0.0.0.0 (%s)\n", del ? "Deleting" : "Adding", str1, dev );
			my_router = 0;

		}

	} else {

		debug_output( 3, "%s route to %s/%i via %s (%s)\n", del ? "Deleting" : "Adding", str1, netmask, str2, dev );
		debug_output( 4, "%s route to %s/%i via %s (%s)\n", del ? "Deleting" : "Adding", str1, netmask, str2, dev );
		my_router = router;

	}


	memset( &nladdr, 0, sizeof(struct sockaddr_nl) );
	memset( &req, 0, sizeof(req) );
	memset( &msg, 0, sizeof(struct msghdr) );

	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = NLMSG_LENGTH( sizeof(struct rtmsg) + 3 * sizeof(struct rtattr) + 12 );
	req.nlh.nlmsg_pid = getpid();
	req.rtm.rtm_family = AF_INET;
	req.rtm.rtm_table = ( dest == 0 ? BATMAN_RT_TABLE_TUNNEL : BATMAN_RT_TABLE_DEFAULT );
	req.rtm.rtm_dst_len = netmask;

	if ( del ) {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nlh.nlmsg_type = RTM_DELROUTE;
		req.rtm.rtm_scope = RT_SCOPE_NOWHERE;

	} else {

		req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
		req.nlh.nlmsg_type = RTM_NEWROUTE;
		req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
		req.rtm.rtm_protocol = RTPROT_STATIC;
		req.rtm.rtm_type = RTN_UNICAST;

	}

	rta = (struct rtattr *)req.buff;
	rta->rta_type = RTA_DST;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + sizeof(struct rtattr), (char *)&dest, 4 );

	rta = (struct rtattr *)(req.buff + sizeof(struct rtattr) + 4);
	rta->rta_type = RTA_GATEWAY;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + 2 * sizeof(struct rtattr) + 4, (char *)&my_router, 4 );

	rta = (struct rtattr *)(req.buff + 2 * sizeof(struct rtattr) + 8);
	rta->rta_type = RTA_OIF;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + 3 * sizeof(struct rtattr) + 8, (char *)&ifi, 4 );

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
			debug_output( 0, "Error - can't %s route to %s/%i via %s: %s\n", del ? "delete" : "add", str1, netmask, str2, strerror(-((struct nlmsgerr*)NLMSG_DATA(nh))->error) );

		nh = NLMSG_NEXT( nh, len );

	}

}




void add_del_rule( uint32_t src_network, uint8_t src_netmask, uint32_t dst_network, uint8_t dst_netmask, int8_t del, int8_t rt_table ) {

	int netlink_sock,len;
	char buf[4096], str1[16], str2[16];
	struct rtattr *rta;
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, sizeof(buf) };
	struct msghdr msg;
	struct nlmsghdr *nh;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char buff[2 * sizeof(struct rtattr) + 8];
	} req;


	memset( &nladdr, 0, sizeof(struct sockaddr_nl) );
	memset( &req, 0, sizeof(req) );
	memset( &msg, 0, sizeof(struct msghdr) );

	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = NLMSG_LENGTH( sizeof(struct rtmsg) + 2 * sizeof(struct rtattr) + 8 );
	req.nlh.nlmsg_pid = getpid();
	req.rtm.rtm_family = AF_INET;
	req.rtm.rtm_table = rt_table;
	req.rtm.rtm_src_len = src_netmask;
	req.rtm.rtm_dst_len = dst_netmask;

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
	rta->rta_type = RTA_SRC;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + sizeof(struct rtattr), (char *)&src_network, 4 );

	rta = (struct rtattr *)(req.buff + sizeof(struct rtattr) + 4);
	rta->rta_type = RTA_DST;
	rta->rta_len = sizeof(struct rtattr) + 4;
	memcpy( ((char *)&req.buff) + 2 * sizeof(struct rtattr) + 4, (char *)&dst_network, 4 );

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

			inet_ntop( AF_INET, &src_network, str1, sizeof (str1) );
			inet_ntop( AF_INET, &dst_network, str2, sizeof (str2) );

			debug_output( 0, "Error - can't %s rule from %s/%i to %s/%i: %s\n", del ? "delete" : "add", str1, src_netmask, str2, dst_netmask, strerror(-((struct nlmsgerr*)NLMSG_DATA(nh))->error) );

		}

		nh = NLMSG_NEXT( nh, len );

	}

}



/* Probe for tun interface availability */
int8_t probe_tun() {

	int32_t fd;

	if ( ( fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		debug_output( 0, "Error - could not open '/dev/net/tun' ! Is the tun kernel module loaded ?\n" );
		return 0;

	}

	close( fd );

	return 1;

}



int8_t del_dev_tun( int32_t fd ) {

	if ( ioctl( fd, TUNSETPERSIST, 0 ) < 0 ) {

		debug_output( 0, "Error - can't delete tun device: %s\n", strerror(errno) );
		return -1;

	}

	close( fd );

	return 1;

}



int8_t add_dev_tun( struct batman_if *batman_if, uint32_t tun_addr, char *tun_dev, size_t tun_dev_size, int32_t *fd ) {

	int32_t tmp_fd;
	struct ifreq ifr_tun, ifr_if;
	struct sockaddr_in addr;

	/* set up tunnel device */
	memset( &ifr_tun, 0, sizeof(ifr_tun) );
	memset( &ifr_if, 0, sizeof(ifr_if) );
	ifr_tun.ifr_flags = IFF_TUN | IFF_NO_PI;

	if ( ( *fd = open( "/dev/net/tun", O_RDWR ) ) < 0 ) {

		debug_output( 0, "Error - can't create tun device (/dev/net/tun): %s\n", strerror(errno) );
		return -1;

	}

	if ( ( ioctl( *fd, TUNSETIFF, (void *) &ifr_tun ) ) < 0 ) {

		debug_output( 0, "Error - can't create tun device (TUNSETIFF): %s\n", strerror(errno) );
		close(*fd);
		return -1;

	}

	if ( ioctl( *fd, TUNSETPERSIST, 1 ) < 0 ) {

		debug_output( 0, "Error - can't create tun device (TUNSETPERSIST): %s\n", strerror(errno) );
		close(*fd);
		return -1;

	}


	tmp_fd = socket(AF_INET, SOCK_DGRAM, 0);

	if ( tmp_fd < 0 ) {
		debug_output( 0, "Error - can't create tun device (udp socket): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		return -1;
	}


	/* set ip of this end point of tunnel */
	memset( &addr, 0, sizeof(addr) );
	addr.sin_addr.s_addr = tun_addr;
	addr.sin_family = AF_INET;
	memcpy( &ifr_tun.ifr_addr, &addr, sizeof(struct sockaddr) );


	if ( ioctl( tmp_fd, SIOCSIFADDR, &ifr_tun) < 0 ) {

		debug_output( 0, "Error - can't create tun device (SIOCSIFADDR): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}


	if ( ioctl( tmp_fd, SIOCGIFFLAGS, &ifr_tun) < 0 ) {

		debug_output( 0, "Error - can't create tun device (SIOCGIFFLAGS): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	ifr_tun.ifr_flags |= IFF_UP;
	ifr_tun.ifr_flags |= IFF_RUNNING;

	if ( ioctl( tmp_fd, SIOCSIFFLAGS, &ifr_tun) < 0 ) {

		debug_output( 0, "Error - can't create tun device (SIOCSIFFLAGS): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	/* get MTU from real interface */
	strncpy( ifr_if.ifr_name, batman_if->dev, IFNAMSIZ - 1 );

	if ( ioctl( tmp_fd, SIOCGIFMTU, &ifr_if ) < 0 ) {

		debug_output( 0, "Error - can't create tun device (SIOCGIFMTU): %s\n", strerror(errno) );
		del_dev_tun( *fd );
		close( tmp_fd );
		return -1;

	}

	/* set MTU of tun interface: real MTU - 28 */
	if ( ifr_if.ifr_mtu < 100 ) {

		debug_output( 0, "Warning - MTU smaller than 100 -> can't reduce MTU anymore\n" );

	} else {

		ifr_tun.ifr_mtu = ifr_if.ifr_mtu - 28;

		if ( ioctl( tmp_fd, SIOCSIFMTU, &ifr_tun ) < 0 ) {

			debug_output( 0, "Error - can't create tun device (SIOCSIFMTU): %s\n", strerror(errno) );
			del_dev_tun( *fd );
			close( tmp_fd );
			return -1;

		}

	}


	strncpy( tun_dev, ifr_tun.ifr_name, tun_dev_size - 1 );
	close( tmp_fd );

	return 1;

}


