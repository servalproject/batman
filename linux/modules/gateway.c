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

static void udp_data_ready(struct sock *sk, int len);
static int packet_recv_thread(void *data);

static int compare_wip( void *data1, void *data2 );
static int choose_wip( void *data, int32_t size );
static int compare_vip( void *data1, void *data2 );
static int choose_vip( void *data, int32_t size );
static struct gw_client *get_ip_addr(struct sockaddr_in *client_addr);

static void bat_netdev_setup( struct net_device *dev);
static int create_bat_netdev(void);
static int bat_netdev_open( struct net_device *dev );
static int bat_netdev_close( struct net_device *dev );
static int bat_netdev_xmit( struct sk_buff *skb, struct net_device *dev );

static void cleanup_procfs(void);
static int setup_procfs(void);
static int proc_clients_read(char *buf, char **start, off_t offset, int size, int *eof, void *data);

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};


static int Major;					/* Major number assigned to our device driver */

static struct net_device *gate_device = NULL;

static struct socket *server_sock = NULL;
static struct socket *inet_sock = NULL;

static struct hashtable_t *wip_hash;
static struct hashtable_t *vip_hash;

static struct list_head free_client_list;

atomic_t data_ready_cond;
atomic_t exit_cond;

DECLARE_WAIT_QUEUE_HEAD(thread_wait);
static struct task_struct *kthread_task = NULL;

static struct proc_dir_entry *proc_dir, *clients_file;

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

	setup_procfs();
	
	/* TODO: error handling */
	vip_hash = hash_new( 128, compare_vip, choose_vip );
	wip_hash = hash_new( 128, compare_wip, choose_wip );

	INIT_LIST_HEAD(&free_client_list);

	
	return(0);
}

void cleanup_module()
{
	struct free_client_data *entry, *next;
	struct gw_client *gw_client;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	devfs_remove( "batgat", 0 );
#else
	class_device_destroy( batman_class, MKDEV( Major, 0 ) );
	class_destroy( batman_class );
#endif

	/* Unregister the device */
	unregister_chrdev( Major, DRIVER_DEVICE );


	if(kthread_task) {

		atomic_set(&exit_cond, 1);
		wake_up_interruptible(&thread_wait);
		kthread_stop(kthread_task);

	}

	if(server_sock) {

// 		server_sock->sk->sk_data_ready = server_sock->sk->sk_user_data;
		sock_release(server_sock);
		server_sock = NULL;

	}

	if(inet_sock) {
		sock_release(inet_sock);
		inet_sock = NULL;
	}

	if(gate_device) {
// 		dev_put(gate_device);
		unregister_netdev(gate_device);
	}
	
	list_for_each_entry_safe(entry, next, &free_client_list, list) {

		if(entry->gw_client != NULL) {
			gw_client = entry->gw_client;
			list_del(&entry->list);
			kfree(gw_client);
		}

	}

	cleanup_procfs();
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
	struct sockaddr_in server_addr;
	struct free_client_data *entry, *next;
	struct gw_client *gw_client;
	
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


			if( ( ret_value = create_bat_netdev() ) == 0 && !server_sock) {


				server_addr.sin_family = AF_INET;
				server_addr.sin_addr.s_addr = INADDR_ANY;
				server_addr.sin_port = htons( (unsigned short) BATMAN_PORT );

				if ( ( sock_create_kern( PF_INET, SOCK_RAW, IPPROTO_RAW, &inet_sock ) ) < 0 ) {

					DBG( "can't create raw socket");
					ret_value = -EFAULT;
					goto end;

				}
				
				if( sock_create_kern( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &server_sock ) < 0 ) {

					DBG( "can't create udp socket");
					ret_value = -EFAULT;
					goto end;

				}

				if( kernel_bind( server_sock, (struct sockaddr *) &server_addr, sizeof( server_addr ) ) ) {

					DBG( "can't bind udp server socket");
					ret_value = -EFAULT;
					sock_release(server_sock);
					server_sock = NULL;
					goto end;

				}

				server_sock->sk->sk_user_data = server_sock->sk->sk_data_ready;
				server_sock->sk->sk_data_ready = udp_data_ready;

				kthread_task = kthread_run(packet_recv_thread, NULL, "mod_batgat");

				if (IS_ERR(kthread_task)) {
					DBG( "unable to start packet receive thread");
					kthread_task = NULL;
					ret_value = -EFAULT;
					goto end;
				}

			}

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

				if( copy_to_user( ( void __user* )arg, &ioc, sizeof( ioc ) ) )

					ret_value = -EFAULT;

			}

			break;

		case IOCREMDEV:

			DBG("disconnect daemon");
			if (kthread_task) {

				atomic_set(&exit_cond, 1);
				wake_up_interruptible(&thread_wait);
				kthread_stop(kthread_task);

			}

			kthread_task = NULL;

			DBG("thread shutdown");

// 			dev_put(gate_device);

			if(gate_device) {
				unregister_netdev(gate_device);
				gate_device = NULL;
				DBG("gate shutdown");
			}

			if(server_sock) {

// 				server_sock->sk->sk_data_ready = server_sock->sk->sk_user_data;

				sock_release(server_sock);
				server_sock = NULL;

			}

			if(inet_sock) {
				sock_release(inet_sock);
				inet_sock = NULL;
			}

			list_for_each_entry_safe(entry, next, &free_client_list, list) {

				if(entry->gw_client) {
					gw_client = entry->gw_client;
					list_del(&entry->list);
					kfree(gw_client);
				}

			}

			DBG( "device unregistered successfully" );
			break;
		default:
			DBG( "ioctl %d is not supported",cmd );
			ret_value = -EFAULT;

	}

end:

	return( ret_value );

}

static void udp_data_ready(struct sock *sk, int len)
{

	void (*data_ready)(struct sock *, int) = sk->sk_user_data;
	data_ready(sk,len);
	atomic_set(&data_ready_cond, 1);
	wake_up_interruptible(&thread_wait);

}

static int packet_recv_thread(void *data)
{

	struct msghdr msg, inet_msg, resp_msg;
	struct kvec iov, inet_iov, resp_iov;
	struct iphdr *iph;
	struct gw_client *client_data;
	struct sockaddr_in client, inet_addr;
	struct free_client_data *tmp_entry;
	
	int length,ret_value;
	unsigned char buffer[1600];
	unsigned long time = jiffies;
	
	struct hash_it_t *hashit;

	msg.msg_name = &client;
	msg.msg_namelen = sizeof( struct sockaddr_in );
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;

	inet_addr.sin_family = AF_INET;
	inet_msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	inet_msg.msg_name = &inet_addr;
	inet_msg.msg_namelen = sizeof( inet_addr );
	inet_msg.msg_control = NULL;
	inet_msg.msg_controllen = 0;
	
	atomic_set(&data_ready_cond, 0);
	atomic_set(&exit_cond, 0);

	iph = ( struct iphdr*)(buffer + 1 );

	while (!kthread_should_stop() && !atomic_read(&exit_cond)) {

		wait_event_interruptible(thread_wait, atomic_read(&data_ready_cond) || atomic_read(&exit_cond));

		if (kthread_should_stop() || atomic_read(&exit_cond))
			break;

		client_data = NULL;

		iov.iov_base = buffer;
		iov.iov_len = sizeof(buffer);

		while ( ( length = kernel_recvmsg( server_sock, &msg, &iov, 1, sizeof(buffer), MSG_NOSIGNAL | MSG_DONTWAIT ) ) > 0 ) {

			if( ( jiffies - time ) / HZ > LEASE_TIME ) {

				hashit = NULL;

				while( NULL != ( hashit = hash_iterate( wip_hash, hashit ) ) ) {
					client_data = hashit->bucket->data;
					if( ( jiffies - client_data->last_keep_alive ) / HZ > LEASE_TIME ) {

						hash_remove_bucket(wip_hash, hashit);
						hash_remove_bucket(vip_hash, hashit);

						tmp_entry = kmalloc(sizeof(struct free_client_data), GFP_KERNEL);
						if(tmp_entry != NULL) {
							tmp_entry->gw_client = client_data;
							list_add(&tmp_entry->list,&free_client_list);
						} else
							DBG("can't add free gw_client to free list");

					}
				}

				time = jiffies;
			}


			if( length > 0 && buffer[0] == TUNNEL_IP_REQUEST ) {

				client_data = get_ip_addr(&client);

				if(client_data != NULL) {

					buffer[0] = TUNNEL_DATA;
					memcpy( &buffer[1], &client_data->vip_addr, sizeof( client_data->vip_addr ) );

// 					resp_msg.msg_name = &client;
// 					resp_msg.msg_namelen = sizeof(client);
// 					resp_msg.msg_control = NULL;
// 					resp_msg.msg_controllen = 0;
// 					
// 					resp_iov.iov_base = buffer;
// 					resp_iov.iov_len = length;
// 					
// 					resp_msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;

					iov.iov_base = buffer;
					iov.iov_len = length;

					if( ( ret_value = kernel_sendmsg(server_sock, &msg, &iov, 1, length ) ) < 0 )
						DBG("tunnel ip request socket return %d", ret_value);

				} else
					DBG("can't get an ip address");

			} else if( length > 0 && buffer[0] == TUNNEL_DATA ) {

				client_data = ((struct gw_client *)hash_find(wip_hash, &client.sin_addr.s_addr));

				if(client_data == NULL) {

					buffer[0] = TUNNEL_IP_INVALID;
// 					resp_msg.msg_name = &client;
// 					resp_msg.msg_namelen = sizeof( struct sockaddr_in );
// 					resp_msg.msg_control = NULL;
// 					resp_msg.msg_controllen = 0;
// 					
// 					resp_iov.iov_base = buffer;
// 					resp_iov.iov_len = length;
// 					
// 					resp_msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;

					iov.iov_base = buffer;
					iov.iov_len = length;

					if( ( ret_value = kernel_sendmsg(server_sock, &msg, &iov, 1, length ) ) < 0 )
						DBG("tunnel ip invalid socket return %d", ret_value);
					continue;

				}

				client_data->last_keep_alive = jiffies;

				inet_iov.iov_base = &buffer[1];
				inet_iov.iov_len = length - 1;

				inet_addr.sin_port = 0;
				inet_addr.sin_addr.s_addr = iph->daddr;

				if( (ret_value = kernel_sendmsg(inet_sock, &inet_msg, &inet_iov, 1, length - 1 ) ) < 0 )
					DBG("tunnel data socket return %d", ret_value);

			} else if( length > 0 && buffer[0] == TUNNEL_KEEPALIVE_REQUEST ) {

				DBG("keep alive");
				client_data = ((struct gw_client *)hash_find(wip_hash, &client.sin_addr.s_addr));
				if(client_data != NULL) {
					DBG("refresh ip");
					buffer[0] = TUNNEL_KEEPALIVE_REPLY;
					client_data->last_keep_alive = jiffies;
				} else
					buffer[0] = TUNNEL_IP_INVALID;

// 				resp_msg.msg_name = &client;
// 				resp_msg.msg_namelen = sizeof( struct sockaddr_in );
// 				resp_msg.msg_control = NULL;
// 				resp_msg.msg_controllen = 0;
// 
// 				resp_iov.iov_base = buffer;
// 				resp_iov.iov_len = length;
// 
// 				resp_msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;

				iov.iov_base = buffer;
				iov.iov_len = length;

				if( ( ret_value = kernel_sendmsg(server_sock, &msg, &iov, 1, length ) ) < 0 )
					DBG("tunnel keep alive socket return %d", ret_value);

			} else
				DBG( "recive unknown message" );

		}
		atomic_set(&data_ready_cond, 0);
	}

	while(!kthread_should_stop()) {

		if(kthread_should_stop())
			break;

		schedule();
	}

	DBG( "thread stop" );
	return 0;
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
	struct gate_priv *priv = netdev_priv( dev );
	struct sockaddr_in sa;
	struct iphdr *iph = ip_hdr( skb );
	struct iovec iov[2];
	struct msghdr msg;
	
	struct gw_client *client_data;
	unsigned char msg_number[1];

	msg_number[0] = TUNNEL_DATA;

	/* we use saddr , because hash choose and compare begin at + 4 bytes */
	client_data = ((struct gw_client *)hash_find(vip_hash, & iph->saddr )); /* daddr */

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
		kernel_sendmsg(priv->tun_socket, &msg, (struct kvec *)&iov, 2, skb->len - sizeof( struct ethhdr ) + 1);

	} else
		DBG("client not found");

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
	struct gate_priv *priv = netdev_priv(dev);
	DBG( "receive close" );

	if(priv->tun_socket)
		sock_release(priv->tun_socket);
	
	netif_stop_queue( dev );
	return( 0 );
}

static int create_bat_netdev()
{
	struct gate_priv *priv;

	if( gate_device == NULL ) {

		if( ( gate_device = alloc_netdev( sizeof( struct gate_priv ) , "gate%d", bat_netdev_setup ) ) == NULL )
			return -ENOMEM;

		if( ( register_netdev( gate_device ) ) < 0 )
			return -ENODEV;

		priv = netdev_priv( gate_device );

		if( sock_create_kern( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &priv->tun_socket ) < 0 ) {

			DBG( "can't create gate socket");
			netif_stop_queue(gate_device);
			return -EFAULT;

		}

// 		dev_hold(gate_device);


	} else {

		DBG( "bat_device for is already created" );
		return( -1 );

	}

	return( 0 );

}

/* ip handling */

static struct gw_client *get_ip_addr(struct sockaddr_in *client_addr)
{
	static uint8_t next_free_ip[4] = {169,254,0,1};
	struct free_client_data *entry, *next;
	struct gw_client *gw_client = NULL;
	struct hashtable_t *swaphash;



	gw_client = ((struct gw_client *)hash_find(wip_hash, &client_addr->sin_addr.s_addr));

	if (gw_client != NULL) {
		DBG("found client in hash");
		return gw_client;
	}

	list_for_each_entry_safe(entry, next, &free_client_list, list) {
		DBG("use free client from list");
		gw_client = entry->gw_client;
		list_del(&entry->list);
		break;
	}

	if(gw_client == NULL) {
		DBG("malloc client");
		gw_client = kmalloc( sizeof(struct gw_client), GFP_KERNEL );
		gw_client->vip_addr = 0;
	}

	gw_client->wip_addr = client_addr->sin_addr.s_addr;
	gw_client->client_port = client_addr->sin_port;
	gw_client->last_keep_alive = jiffies;

	/* TODO: check if enough space available */
	if (gw_client->vip_addr == 0) {

		gw_client->vip_addr = *(uint32_t *)next_free_ip;

		next_free_ip[3]++;

		if (next_free_ip[3] == 0)
			next_free_ip[2]++;

	}

	hash_add(wip_hash, gw_client);
	hash_add(vip_hash, gw_client);

	if (wip_hash->elements * 4 > wip_hash->size) {

		swaphash = hash_resize(wip_hash, wip_hash->size * 2);

		if (swaphash == NULL) {

			DBG( "Couldn't resize hash table" );

		}

		wip_hash = swaphash;

		swaphash = hash_resize(vip_hash, vip_hash->size * 2);

		if (swaphash == NULL) {

			DBG( "Couldn't resize hash table" );

		}

		vip_hash = swaphash;

	}

	return gw_client;

}


/* needed for hash, compares 2 struct gw_client, but only their ip-addresses. assumes that
 * the ip address is the first/second field in the struct */
int compare_wip(void *data1, void *data2)
{
	return ( memcmp( data1, data2, 4 ) );
}

int compare_vip(void *data1, void *data2)
{
	return ( memcmp( data1 + 4, data2 + 4, 4 ) );
}

/* hashfunction to choose an entry in a hash table of given size */
/* hash algorithm from http://en.wikipedia.org/wiki/Hash_table */
int choose_wip(void *data, int32_t size)
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

int choose_vip(void *data, int32_t size)
{
	unsigned char *key= data;
	uint32_t hash = 0;
	size_t i;
	for (i = 4; i < 8; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return (hash%size);

}

static void cleanup_procfs(void)
{
	if(clients_file) remove_proc_entry("PROC_FILE_CLIENTS", proc_dir);
	if(proc_dir) remove_proc_entry(PROC_ROOT_DIR, NULL);
}

static int setup_procfs(void)
{
	proc_dir = proc_mkdir(PROC_ROOT_DIR, proc_net);
	clients_file = create_proc_read_entry(PROC_FILE_CLIENTS, S_IRUGO, proc_dir, proc_clients_read, NULL);
	
	return(0);
}

static int proc_clients_read(char *buf, char **start, off_t offset, int size, int *eof, void *data)
{
	struct gw_client *client_data;
	unsigned char *wip, *vip;
	int bytes_written = 0, total_bytes = 0;

	struct hash_it_t *hashit = NULL;

	while( NULL != ( hashit = hash_iterate( wip_hash, hashit ) ) ) {

		client_data = hashit->bucket->data;
		wip = (unsigned char *)&client_data->wip_addr;
		vip = (unsigned char *)&client_data->vip_addr;
		bytes_written = snprintf(buf + total_bytes, (size - total_bytes), "%u.%u.%u.%u - %u.%u.%u.%u\n",
					wip[0],wip[1],wip[2],wip[3],vip[0],vip[1],vip[2],vip[3]);
		total_bytes += (bytes_written > (size - total_bytes) ? size - total_bytes : bytes_written);

	}
	
	*eof = 1;
	return total_bytes;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
