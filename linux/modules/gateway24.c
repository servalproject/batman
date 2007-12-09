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

#include "gateway24.h"
#include "hash.h"

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );

static int bat_netdev_setup( struct net_device *dev);
static int create_bat_netdev(void);
static int bat_netdev_open( struct net_device *dev );
static int bat_netdev_close( struct net_device *dev );
static int bat_netdev_xmit( struct sk_buff *skb, struct net_device *dev );

static int packet_recv_thread(void *data);

static int kernel_sendmsg(struct socket *sock, struct msghdr *msg, struct iovec *vec, size_t num, size_t size);

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;
static int thread_pid;
static struct completion thread_complete;
spinlock_t hash_lock = SPIN_LOCK_UNLOCKED;

static struct net_device gate_device = {
	init: bat_netdev_setup,

};

static struct hashtable_t *wip_hash;
static struct hashtable_t *vip_hash;

int init_module()
{

	printk(KERN_INFO "B.A.T.M.A.N. gateway modul\n");
	
	if ( ( Major = register_chrdev( 0, DRIVER_DEVICE, &fops ) ) < 0 ) {

		DBG( "registering the character device failed with %d", Major );
		return Major;

	}

	DBG( "I was assigned major number %d. To talk to", Major );
	DBG( "the driver, create a dev file with 'mknod /dev/batgat c %d 0'.", Major );
	printk(KERN_INFO "Remove the device file and module when done." );


	printk(KERN_INFO "modul successfully loaded\n");
	return(0);
}

void cleanup_module()
{	
	unregister_chrdev( Major, DRIVER_DEVICE );
	printk(KERN_INFO "modul successfully unloaded\n" );
	return;
}

static int batgat_open(struct inode *inode, struct file *filp)
{
	MOD_INC_USE_COUNT;
	return(0);
}

static int batgat_release(struct inode *inode, struct file *file)
{
	MOD_DEC_USE_COUNT;
	return(0);
}


static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	uint8_t tmp_ip[4];
	struct batgat_ioc_args ioc;
	struct free_client_data *entry, *next;
	struct gw_client *gw_client;
	
	int ret_value = 0;

	if( cmd == IOCSETDEV || cmd == IOCREMDEV ) {

		if( !access_ok( VERIFY_READ, (void *)arg, sizeof( ioc ) ) ) {
			printk(KERN_INFO "access to memory area of arg not allowed" );
			ret_value = -EFAULT;
			goto end;
		}

		if( __copy_from_user( &ioc, (void *)arg, sizeof( ioc ) ) ) {
			ret_value = -EFAULT;
			goto end;
		}

	}
	
	switch( cmd ) {

		case IOCSETDEV:

			if( ( ret_value = create_bat_netdev() ) == 0 && !thread_pid) {
				init_completion(&thread_complete);
				thread_pid = kernel_thread( packet_recv_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND );
				if(thread_pid<0) {
					DBG( "unable to start packet receive thread");
					ret_value = -EFAULT;
					goto end;
				}
			}
/*
			if( ret_value == -1 || ret_value == 0 ) {

				tmp_ip[0] = 169; tmp_ip[1] = 254; tmp_ip[2] = 0; tmp_ip[3] = 0;
				ioc.universal = *(uint32_t*)tmp_ip;
				ioc.ifindex = gate_device->ifindex;

				strlcpy( ioc.dev_name, gate_device->name, IFNAMSIZ - 1 );

				DBG("name %s index %d", ioc.dev_name, ioc.ifindex);
				
				if( ret_value == -1 ) {

					DBG("device already exists");
					ioc.exists = 1;
					ret_value = 0;
					
				}

				if( copy_to_user( (void *)arg, &ioc, sizeof( ioc ) ) )

					ret_value = -EFAULT;

			}*/

			break;

		case IOCREMDEV:

// 			DBG("disconnect daemon");
// 
// 			if(thread_pid) {
// 
// 				kill_proc(thread_pid, SIGTERM, 0 );
// 				wait_for_completion( &thread_complete );
// 
// 			}
// 			device->thread_pid = 0;
// 
// 			DBG("thread shutdown");
// 
// // 			dev_put(gate_device);
// 
// 			if(gate_device) {
// 				unregister_netdev(gate_device);
// 				gate_device = NULL;
// 				DBG("gate shutdown");
// 			}
// 
// 			list_for_each_entry_safe(entry, next, &free_client_list, list) {
// 
// 				if(entry->gw_client) {
// 					gw_client = entry->gw_client;
// 					list_del(&entry->list);
// 					kfree(gw_client);
// 				}
// 
// 			}
// 
// 			DBG( "device unregistered successfully" );
			break;
		default:
			DBG( "ioctl %d is not supported",cmd );
			ret_value = -EFAULT;

	}

end:

		return( ret_value );
}

static int packet_recv_thread(void *data)
{
	return 0;
}

/* bat_netdev part */

static int bat_netdev_setup( struct net_device *dev )
{

	ether_setup(dev);
	dev->open = bat_netdev_open;
	dev->stop = bat_netdev_close;
	dev->hard_start_xmit = bat_netdev_xmit;
// 	dev->destructor = free_netdev;

	dev->features        |= NETIF_F_NO_CSUM;
	dev->hard_header_cache = NULL;
	dev->mtu = 1471;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->hard_header_cache = NULL;
	dev->priv = kmalloc(sizeof(struct gate_priv), GFP_KERNEL);

	if (dev->priv == NULL)
		return -ENOMEM;

	memset( dev->priv, 0, sizeof( struct gate_priv ) );
	
	return(0);
}

static int bat_netdev_xmit( struct sk_buff *skb, struct net_device *dev )
{
	struct gate_priv *priv = netdev_priv( dev );
	struct sockaddr_in sa;
	struct iphdr *iph = ip_hdr(skb);
	struct iovec iov[2];
	struct msghdr msg;
	
	struct gw_client *client_data;
	unsigned char msg_number[1];

	msg_number[0] = TUNNEL_DATA;

	/* we use saddr , because hash choose and compare begin at + 4 bytes */
	spin_lock(&hash_lock);
	client_data = ((struct gw_client *)hash_find(vip_hash, & iph->saddr )); /* daddr */
	spin_unlock(&hash_lock);

	if( client_data != NULL ) {

		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = client_data->wip_addr;
		sa.sin_port = htons( (unsigned short)BATMAN_PORT );

		msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
		msg.msg_name = &sa;
		msg.msg_namelen = sizeof(sa);
		msg.msg_control = NULL;
		msg.msg_controllen = 0;

		iov[0].iov_base = msg_number;
		iov[0].iov_len = 1;
		iov[1].iov_base = skb->data + sizeof( struct ethhdr );
		iov[1].iov_len = skb->len - sizeof( struct ethhdr );
		kernel_sendmsg(priv->tun_socket, &msg, iov, 2, skb->len - sizeof( struct ethhdr ) + 1);

	} else
		printk(KERN_INFO "client not found");

		kfree_skb( skb );
		return( 0 );
}

static int bat_netdev_open( struct net_device *dev )
{
	printk(KERN_INFO "receive open" );
	netif_start_queue( dev );
	return( 0 );
}

static int bat_netdev_close( struct net_device *dev )
{
	struct gate_priv *priv = netdev_priv(dev);
	printk(KERN_INFO "receive close" );

	if(priv->tun_socket)
		sock_release(priv->tun_socket);
	
	netif_stop_queue( dev );
	return( 0 );
}

static int create_bat_netdev()
{
	struct gate_priv *priv;

// 	if( gate_device == NULL ) {

// 		if( ( gate_device = alloc_netdev( sizeof( struct gate_priv ) , "gate%d", bat_netdev_setup ) ) == NULL )
// 			return -ENOMEM;

		if( ( register_netdev( &gate_device ) ) < 0 )
			return -ENODEV;

		priv = (struct gate_priv*)&gate_device;

		if( sock_create( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &priv->tun_socket ) < 0 ) {

			printk(KERN_INFO "can't create gate socket");
			netif_stop_queue(&gate_device);
			return -EFAULT;

		}

// 		dev_hold(gate_device);


// 	} else {
// 
// 		printk(KERN_INFO "bat_device for is already created" );
// 		return( -1 );
// 
// 	}

	return( 0 );

}

static int kernel_sendmsg(struct socket *sock, struct msghdr *msg, struct iovec *vec, size_t num, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);
	/*
	* the following is safe, since for compiler definitions of kvec and
	* iovec are identical, yielding the same in-core layout and alignment
	*/
	msg->msg_iov = vec;
	msg->msg_iovlen = num;
	result = sock_sendmsg(sock, msg, size);
	set_fs(oldfs);
	return result;
}


MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
