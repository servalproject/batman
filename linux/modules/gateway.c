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

/* Kernel Programming */
#define LINUX

#define DRIVER_AUTHOR "Andreas Langer <an.langer@gmx.de>, Marek Lindner <lindner_marek@yahoo.de>"
#define DRIVER_DESC   "batman gateway module"
#define DRIVER_DEVICE "batgat"

#include <linux/version.h>	/* KERNEL_VERSION ... */
#include <linux/fs.h>		/* fops ...*/
#include <linux/inetdevice.h>	/* __in_dev_get_rtnl */
#include <linux/in.h>		/* sockaddr_in */
#include <linux/net.h>		/* socket */
#include <linux/string.h>	/* strlen, strstr, strncmp ... */
#include <linux/ip.h>		/* iphdr */
#include <linux/if_arp.h>	/* ARPHRD_NONE */
#include <net/sock.h>		/* sock */
#include <net/pkt_sched.h>	/* class_create, class_destroy, class_device_create */

#include "gateway.h"
#include "hash.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	#include <linux/devfs_fs_kernel.h>
#else
	static struct class *batman_class;
#endif

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );

static int udp_server_thread(void *data);
static int compare_orig( void *data1, void *data2 );
static int choose_orig( void *data, int32_t size );

static void bat_netdev_setup( struct net_device *dev);
static int create_bat_netdev(void);
static int bat_netdev_open( struct net_device *dev );
static int bat_netdev_close( struct net_device *dev );
static int bat_netdev_xmit( struct sk_buff *skb, struct net_device *dev );

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;					/* Major number assigned to our device driver */

static struct net_device *Bat_device;
static struct completion Thread_complete;
static int Thread_pid;
static struct socket *Bat_socket;
static struct hashtable_t *hash;

int init_module()
{
	/* register our device - kernel assigns a free major number */
	if ( ( Major = register_chrdev( 0, DRIVER_DEVICE, &fops ) ) < 0 ) {

		DBG( "registering the character device failed with %d", Major );
		return Major;

	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	if ( devfs_mk_cdev( MKDEV( Major, 0 ), S_IFCHR | S_IRUGO | S_IWUGO, "batgat", 0 ) )
		DBG( "could not create /dev/batgat" );
#else
	batman_class = class_create( THIS_MODULE, "batgat" );

	if ( IS_ERR( batman_class ) )
		DBG( "could not register class 'batgat'" );
	else
		class_device_create( batman_class, NULL, MKDEV( Major, 0 ), NULL, "batgat" );
#endif


	DBG( "I was assigned major number %d. To talk to", Major );
	DBG( "the driver, create a dev file with 'mknod /dev/batgat c %d 0'.", Major );
	DBG( "Remove the device file and module when done." );

	hash = hash_new( 128, compare_orig, choose_orig );
	
	return(0);
}

void cleanup_module()
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	devfs_remove( "batgat", 0 );
#else
	class_device_destroy( batman_class, MKDEV( Major, 0 ) );
	class_destroy( batman_class );
#endif

	/* Unregister the device */
	unregister_chrdev( Major, DRIVER_DEVICE );

	/* TODO: cleanup gate device and thread */
	if( Thread_pid ) {

		kill_proc( Thread_pid, SIGTERM, 0 );
		wait_for_completion( &Thread_complete );

	}

	if( Bat_device )
		unregister_netdev( Bat_device );

	DBG( "unload module complete" );
	return;
}

static int batgat_open(struct inode *inode, struct file *filp)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_INC_USE_COUNT;
#else
	try_module_get(THIS_MODULE);
#endif
	return( 0 );

}

static int batgat_release(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
	return( 0 );
}


static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	
	uint8_t tmp_ip[4];
	struct batgat_ioc_args ioc;
	
	int ret_value = 0;

	if( cmd == IOCSETDEV || cmd == IOCREMDEV ) {

		if( !access_ok( VERIFY_READ, ( void __user* )arg, sizeof( ioc ) ) ) {
			DBG( "access to memory area of arg not allowed" );
			ret_value = -EFAULT;
			goto end;
		}

		if( __copy_from_user( &ioc, ( void __user* )arg, sizeof( ioc ) ) ) {
			ret_value = -EFAULT;
			goto end;
		}

	}
	
	switch( cmd ) {

		case IOCSETDEV:


			if( ( ret_value = create_bat_netdev() ) == 0 ) {

				init_completion( &Thread_complete );
				Thread_pid = kernel_thread( udp_server_thread, NULL, CLONE_KERNEL );

				if( Thread_pid < 0 ) {

					DBG( "can't create thread" );
					ret_value = -EFAULT;
					break;

				}

			}

			if( ret_value == -1 || ret_value == 0 ) {

				tmp_ip[0] = 169; tmp_ip[1] = 254; tmp_ip[2] = 0; tmp_ip[3] = 0;
				ioc.universal = *(uint32_t*)tmp_ip;
				ioc.ifindex = Bat_device->ifindex;

				strlcpy( ioc.dev_name, Bat_device->name, IFNAMSIZ - 1 );
				
				if( ret_value == -1 ) {

					ioc.exists = 1;
					ret_value = 0;
					
				}

				if( copy_to_user( ( void __user* )arg, &ioc, sizeof( ioc ) ) )

					ret_value = -EFAULT;

			}

			break;

		case IOCREMDEV:

			if( Thread_pid ) {

				kill_proc( Thread_pid, SIGTERM, 0 );
				wait_for_completion( &Thread_complete );

			}
			Thread_pid = 0;

			unregister_netdev( Bat_device );
			Bat_device = NULL;

			DBG( "device unregistered successfully" );
			break;
		default:
			DBG( "ioctl %d is not supported",cmd );
			ret_value = -EFAULT;

	}

end:

	return( ret_value );

}

static int udp_server_thread(void *data)
{

	struct sockaddr_in inet_addr, server_addr, client;
	struct msghdr msg, inet_msg;
	struct iovec iov, inet_iov;
	struct iphdr *iph;
	struct socket *inet_sock = NULL, *server_sock = NULL;
	
	unsigned char buffer[1500];
	int len;
	
	mm_segment_t oldfs;
	unsigned long time = jiffies;


	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons( (unsigned short) BATMAN_PORT );
	

	/* init inet socket to forward packets */
	if ( ( sock_create_kern( PF_INET, SOCK_RAW, IPPROTO_RAW, &inet_sock ) ) < 0 ) {

		DBG( "can't create raw socket");
		goto complete;

	}

	/* init upd socket for batman packets */
	if( sock_create( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &server_sock ) < 0 ) {

		DBG( "can't create udp socket");
		goto error_inet;

	}

	if( server_sock->ops->bind( server_sock, (struct sockaddr *) &server_addr, sizeof( server_addr ) ) ) {

		DBG( "can't bind udp server socket");
		goto error;

	}
	
	/* forward msg */
	inet_addr.sin_family = AF_INET;
	inet_msg.msg_iov = &inet_iov;
	inet_msg.msg_iovlen = 1;
	inet_msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	inet_msg.msg_name = &inet_addr;
	inet_msg.msg_namelen = sizeof( inet_addr );
	inet_msg.msg_control = NULL;
	inet_msg.msg_controllen = 0;

	/* batman communication */
	msg.msg_name = &client;
	msg.msg_namelen = sizeof( struct sockaddr_in );
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov    = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	iov.iov_base = buffer;
	iov.iov_len  = sizeof( buffer );

	daemonize( "udpserver" );
	allow_signal( SIGTERM );

	while( !signal_pending( current ) ) {

		oldfs = get_fs();
		set_fs( KERNEL_DS );
		len = sock_recvmsg( server_sock, &msg, sizeof(buffer), 0 );
		set_fs( oldfs );
		
		if( len > 0 && buffer[0] == TUNNEL_IP_REQUEST ) {

			DBG( "ip request" );

		} else if( len > 0 && buffer[0] == TUNNEL_DATA ) {

			DBG( "tunnel data" );

		} else if( len > 0 && buffer[0] == TUNNEL_KEEPALIVE_REQUEST ) {

			DBG( "keepalive request" );

		}

		if( time / HZ < LEASE_TIME )
			continue;

		/* TODO : jiffies */

		time = jiffies;

	}

error:
	sock_release( server_sock );

error_inet:	
	sock_release( inet_sock );

complete:
	complete( &Thread_complete );

	return( 0 );
}

/* bat_netdev part */

static void bat_netdev_setup( struct net_device *dev )
{
	struct gate_priv *priv;

	ether_setup(dev);
	dev->open = bat_netdev_open;
	dev->stop = bat_netdev_close;
	dev->hard_start_xmit = bat_netdev_xmit;
	dev->destructor = free_netdev;

	dev->features        |= NETIF_F_NO_CSUM;
	dev->hard_header_cache = NULL;
	dev->mtu = 1471;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->hard_header_cache = NULL;

	priv = netdev_priv( dev );
	memset( priv, 0, sizeof( struct gate_priv ) );
	
	return;
}

static int bat_netdev_xmit( struct sk_buff *skb, struct net_device *dev )
{
	struct sockaddr_in sa;
	struct gate_priv *priv = netdev_priv( dev );
	struct iphdr *iph = ip_hdr( skb );
	unsigned char buffer[1500];
	uint8_t dest[4];
	uint32_t real_dest;

	memcpy( dest, &iph->daddr, sizeof(int) );

	kfree_skb( skb );
	return( 0 );
}

static int bat_netdev_open( struct net_device *dev )
{
	DBG( "receive open" );
	netif_start_queue( dev );
	return( 0 );
}

static int bat_netdev_close( struct net_device *dev )
{
	DBG( "receive close" );
	netif_stop_queue( dev );
	return( 0 );
}

static int create_bat_netdev()
{
	struct gate_priv *priv;

	if( Bat_device == NULL ) {

		if( ( Bat_device = alloc_netdev( sizeof( struct gate_priv ) , "gate%d", bat_netdev_setup ) ) == NULL )
			return -ENOMEM;

		if( ( register_netdev( Bat_device ) ) < 0 )
			return -ENODEV;

		priv = netdev_priv( Bat_device );
		priv->tun_socket = Bat_socket;

	} else {

		DBG( "bat_device for is already created" );
		return( -1 );

	}

	return( 0 );

}


/* needed for hash, compares 2 struct orig_node, but only their ip-addresses. assumes that
 * the ip address is the first field in the struct */
static int compare_orig( void *data1, void *data2 )
{

	return ( memcmp( data1, data2, 4 ) );

}



/* hashfunction to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
static int choose_orig( void *data, int32_t size )
{

	unsigned char *key= data;
	uint32_t hash = 0;
	size_t i;

	for (i = 0; i < 4; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return (hash%size);

}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
