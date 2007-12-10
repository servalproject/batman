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
static int kernel_recvmsg(struct socket *sock, struct msghdr *msg, struct iovec *vec, size_t num, size_t size, int flags);
		
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
	init: bat_netdev_setup, name: "gate%d",
};

atomic_t gate_device_run;

static struct hashtable_t *wip_hash;
static struct hashtable_t *vip_hash;
static struct list_head free_client_list;

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

	INIT_LIST_HEAD(&free_client_list);
	atomic_set(&gate_device_run, 0);
	
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
	struct gate_priv *priv;
	
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
// 				thread_pid = kernel_thread( packet_recv_thread, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGHAND );
// 				if(thread_pid<0) {
// 					printk(KERN_INFO "unable to start packet receive thread\n");
// 					ret_value = -EFAULT;
// 					goto end;
// 				}
			}

			if( ret_value == -1 || ret_value == 0 ) {

				tmp_ip[0] = 169; tmp_ip[1] = 254; tmp_ip[2] = 0; tmp_ip[3] = 0;
				ioc.universal = *(uint32_t*)tmp_ip;
				ioc.ifindex = gate_device.ifindex;

				strncpy( ioc.dev_name, gate_device.name, IFNAMSIZ - 1 );

				DBG("name %s index %d", ioc.dev_name, ioc.ifindex);
				
				if( ret_value == -1 ) {

					ioc.exists = 1;
					ret_value = 0;

				}

				if( copy_to_user( (void *)arg, &ioc, sizeof( ioc ) ) )

					ret_value = -EFAULT;

			}

			break;

		case IOCREMDEV:

			printk(KERN_INFO "disconnect daemon\n");

// 			if(thread_pid) {
// 
// 				kill_proc(thread_pid, SIGTERM, 0 );
// 				wait_for_completion( &thread_complete );
// 
// 			}

			thread_pid = 0;

			printk(KERN_INFO "thread shutdown\n");

// 			dev_put(gate_device);

			if(atomic_read(&gate_device_run)) {

				priv = (struct gate_priv*)gate_device.priv;

				if( priv->tun_socket )
					sock_release(priv->tun_socket);

				kfree(gate_device.priv);
				gate_device.priv = NULL;
				unregister_netdev(&gate_device);
				atomic_dec(&gate_device_run);
				printk(KERN_INFO "gate shutdown\n");

			}

			list_for_each_entry_safe(entry, next, &free_client_list, list) {

				gw_client = entry->gw_client;
				list_del(&entry->list);
				kfree(gw_client);

			}

			printk(KERN_INFO "device unregistered successfully\n" );
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
	struct msghdr msg, inet_msg;
	struct iovec iov, inet_iov;
	struct iphdr *iph;
	struct gw_client *client_data;
	struct sockaddr_in client, inet_addr, server_addr;
	struct free_client_data *tmp_entry;

	static struct socket *server_sock = NULL;
	static struct socket *inet_sock = NULL;
	
	int length;
	unsigned char buffer[1600];
	
	msg.msg_name = &client;
	msg.msg_namelen = sizeof( struct sockaddr_in );
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	
	
	daemonize();
	spin_lock_irq(&current->sigmask_lock);
	siginitsetinv(&current->blocked, sigmask(SIGTERM) |sigmask(SIGKILL) | sigmask(SIGSTOP)| sigmask(SIGHUP));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	while(!signal_pending(current)) {

		client_data = NULL;

		iov.iov_base = buffer;
		iov.iov_len = sizeof(buffer);

		while ( ( length = kernel_recvmsg( server_sock, &msg, &iov, 1, sizeof(buffer), MSG_NOSIGNAL | MSG_DONTWAIT ) ) > 0 ) {
			printk(KERN_INFO "thread\n");
		}
	}

	complete(&thread_complete);
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
	kfree_skb(skb);
	return(0);
}

static int bat_netdev_open( struct net_device *dev )
{
	printk(KERN_INFO "receive open\n" );
	netif_start_queue( dev );
	return( 0 );
}

static int bat_netdev_close( struct net_device *dev )
{

	printk(KERN_INFO "receive close\n" );	
	netif_stop_queue( dev );
	return( 0 );
}

static int create_bat_netdev()
{

	struct gate_priv *priv;
	if(!atomic_read(&gate_device_run)) {

		if( ( register_netdev( &gate_device ) ) < 0 )
			return -ENODEV;

		priv = (struct gate_priv*)gate_device.priv;
		if( sock_create( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &priv->tun_socket ) < 0 ) {

			printk(KERN_INFO "can't create gate socket\n");
			netif_stop_queue(&gate_device);
			return -EFAULT;

		}

		atomic_inc(&gate_device_run);

	} else {

		printk(KERN_INFO "net device already exists\n");
		return(-1);

	}

	return( 0 );
}

static int kernel_sendmsg(struct socket *sock, struct msghdr *msg, struct iovec *vec, size_t num, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);

	msg->msg_iov = vec;
	msg->msg_iovlen = num;
	result = sock_sendmsg(sock, msg, size);
	set_fs(oldfs);
	return result;
}

static int kernel_recvmsg(struct socket *sock, struct msghdr *msg, struct iovec *vec, size_t num, size_t size, int flags)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);

	msg->msg_iov = (struct iovec *)vec, msg->msg_iovlen = num;
	result = sock_recvmsg(sock, msg, size, flags);
	set_fs(oldfs);
	return result;
}



MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
