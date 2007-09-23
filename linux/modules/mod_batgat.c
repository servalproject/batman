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

#define DRIVER_AUTHOR "Andreas Langer <a.langer@q-dsl.de>, Marek Lindner <lindner_marek@yahoo.de>"
#define DRIVER_DESC   "batman gateway module"
#define DRIVER_DEVICE "batgat"

#include <linux/version.h>	/* KERNEL_VERSION ... */
#include <linux/fs.h>		/* fops ...*/
#include <linux/inetdevice.h>	/* __in_dev_get_rtnl */
#include <linux/in.h>		/* sockaddr_in */
#include <linux/net.h>		/* socket */
#include <linux/string.h>	/* strlen, strstr, strncmp ... */
#include <net/sock.h>		/* sock */
#include <net/pkt_sched.h>	/* class_create, class_destroy, class_device_create */

#include "mod_batgat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	#include <linux/devfs_fs_kernel.h>
#else
	static struct class *batman_class;
#endif

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );

static int udp_server_thread(void *data);

static int register_batgat_device( struct net_device *dev, char *name );
static int unregister_batgat_device( char *name );
static int register_tun_device( struct net_device *dev, char *name );
static int unregister_tun_device( char *name );
static uint8_t get_virtual_ip( struct batgat_dev *dev, uint32_t client_addr);
static int send_data( struct socket *socket, struct sockaddr_in *client, unsigned char *buffer, int buffer_len );

static void ip2string(unsigned int sip,char *buffer);

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;            /* Major number assigned to our device driver */

static struct batgat_dev **dev_array=NULL;
static int batgat_dev_index = 0;

static struct tun_dev **tun_array=NULL;
static int tun_dev_index = 0;

static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	char *device_name = NULL, *colon_ptr = NULL;
	uint16_t command, length;
	struct net_device *reg_device = NULL;
	int ret_value = 0;

	/* cmd comes with 2 short values */
	command = cmd & 0x0000FFFF;
	length = cmd >> 16;

	if( length > IFNAMSIZ - 1 ) {
		printk( "batgat: device name is too long\n" );
		ret_value = -EFAULT;
		goto end;
	}
		
	if( command == IOCSETDEV || command == IOCREMDEV ) {

		if( !access_ok( VERIFY_READ, ( void __user* )arg, length ) ) {
			printk( "batgat: Access to memory area of arg not allowed\n" );
			ret_value = -EFAULT;
			goto end;
		}

		if( ( device_name = kmalloc( length+1, GFP_KERNEL ) ) == NULL ) {
			printk( "batgat: Allocate memory for devicename failed\n" );
			ret_value = -EFAULT;
			goto end;
		}

		__copy_from_user( device_name, ( void __user* )arg, length );
		device_name[ length ] = 0;

		if ( ( colon_ptr = strchr( device_name, ':' ) ) != NULL )
			*colon_ptr = 0;

		if( ( reg_device = dev_get_by_name( device_name ) ) == NULL ) {
			printk( "batgat: Did not find device %s\n", device_name );
			ret_value = -EFAULT;
			goto end;
		}

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

	}

	switch( command ) {

	case IOCSETDEV:
		if( strstr( device_name, "tun" ) ) {
			if( register_tun_device( reg_device, device_name )!= 0 )
				ret_value = -EFAULT;
		} else {
			if( register_batgat_device( reg_device, device_name )!= 0 )
				ret_value = -EFAULT;
		}
		break;
	case IOCREMDEV:
		if( strstr( device_name, "tun" ) ) {
			if( unregister_tun_device( device_name ) != 0 )
				ret_value = -EFAULT;
		} else {
			if( unregister_batgat_device( device_name ) != 0 )
				ret_value = -EFAULT;
		}
		break;
	default:
		printk( "batgat: ioctl %d is not supported\n",command );
		ret_value = -EFAULT;

	}

end:

	if( reg_device )
		dev_put( reg_device );

	if( device_name )
		kfree( device_name );

	return( ret_value );

}

int init_module()
{

	/* register our device - kernel assigns a free major number */
	if ( ( Major = register_chrdev( 0, DRIVER_DEVICE, &fops ) ) < 0 ) {

		printk( "batgat: Registering the character device failed with %d\n", Major );
		return Major;

	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	if ( devfs_mk_cdev( MKDEV( Major, 0 ), S_IFCHR | S_IRUGO | S_IWUGO, "batgat", 0 ) )
		printk( "batgat: Could not create /dev/batgat \n" );
#else
	batman_class = class_create( THIS_MODULE, "batgat" );

	if ( IS_ERR(batman_class) )
		printk( "batgat: Could not register class 'batgat' \n" );
	else
		class_device_create( batman_class, NULL, MKDEV( Major, 0 ), NULL, "batgat" );
#endif


	printk( "batgat: I was assigned major number %d. To talk to\n", Major );
	printk( "batgat: the driver, create a dev file with 'mknod /dev/batgat c %d 0'.\n", Major );
	printk( "batgat: Remove the device file and module when done.\n" );

	return(0);
}

void cleanup_module()
{
	int ret;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	devfs_remove( "batgat", 0 );
#else
	class_device_destroy( batman_class, MKDEV( Major, 0 ) );
	class_destroy( batman_class );
#endif

	/* Unregister the device */
	ret = unregister_chrdev( Major, DRIVER_DEVICE );

	if ( ret < 0 )
		printk( "batgat: Unregistering the character device failed with %d\n", ret );

	printk("batgat: unload module\n");
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

static int udp_server_thread(void *data)
{
	struct batgat_dev *dev_element = (struct batgat_dev*)data;
	struct sockaddr_in client;
	struct msghdr msg;
	struct iovec iov;
	
	unsigned char buffer[1500];
	int len;
	mm_segment_t oldfs;
	uint32_t ip_address;

	if( dev_element->socket->sk == NULL ) {
		return 0;
	}
	
	msg.msg_name = &client;
	msg.msg_namelen = sizeof( struct sockaddr_in );
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov    = &iov;
	msg.msg_iovlen = 1;
	
	printk( "start udp_server thread for %s\n", dev_element->name );
	daemonize( "udpserver" );
	allow_signal( SIGTERM );
	while( !signal_pending( current ) ) {

		iov.iov_base = buffer;
		iov.iov_len  = sizeof(buffer);
		oldfs = get_fs();
		set_fs( KERNEL_DS );
		len = sock_recvmsg( dev_element->socket, &msg,sizeof(buffer), 0 );
		set_fs( oldfs );

		if( len > 0 && buffer[0] == TUNNEL_IP_REQUEST ) {

			if( ( ip_address = ( uint8_t )get_virtual_ip( dev_element, client.sin_addr.s_addr ) ) == 0 ) {
				printk(KERN_ERR "batgat: don't get a virtual ip\n");
				continue;
			}

			ip_address = 169 + ( 254<<8 ) + ((uint8_t)( dev_element->index )<<16 ) + (ip_address<<24 );
			buffer[0] = TUNNEL_DATA;
			memcpy( &buffer[1], &ip_address, sizeof( ip_address ) );
			send_data( dev_element->socket, &client, buffer, len );

		} else {
			printk( "batgat: unkown packet option\n" );
		}

	}

	complete( &dev_element->thread_complete );
	return( 0 );
}

static int tun_thread(void *data)
{
	struct tun_dev *dev_element = (struct tun_dev*)data;

	printk( "start tun_server thread for %s\n", dev_element->name );
	daemonize( "tunserver" );
	allow_signal( SIGTERM );
	while( !signal_pending( current ) ) {
		
	}
	complete( &dev_element->thread_complete );
	return( 0 );
}

static int register_batgat_device( struct net_device *dev, char *name )
{
	struct in_device *in_dev = NULL;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	struct sockaddr_in sa;

	char ip[20];
	
	dev_array = ( struct batgat_dev **)krealloc( dev_array, sizeof( struct batgat_dev* )*( batgat_dev_index + 1), GFP_KERNEL );

	if( dev_array == NULL || ( dev_array[ batgat_dev_index ] = ( struct batgat_dev* )kmalloc( sizeof( struct batgat_dev ), GFP_KERNEL ) ) == NULL ) {
		printk( "batgat: can't allocate memory for dev_array\n" );
		goto error;
	}

	if( ( in_dev = __in_dev_get_rtnl( dev ) ) != NULL ) {

		for ( ifap = &in_dev->ifa_list; ( ifa = *ifap ) != NULL; ifap = &ifa->ifa_next ) {

			if ( !strcmp( name, ifa->ifa_label ) ) {
				ip2string( ifa->ifa_local,ip );
				printk( "found device %s with ip %s\n", ifa->ifa_label, ip );
				break;
			}

		}

	} else {
		printk( "batgat: device %s not found\n", name );
		goto error;
	}

	if(ifa == NULL) {
		printk( KERN_ERR "batgat: can't find interface address for %s\n", name);
		goto error;
	}

	if( sock_create_kern( PF_INET, SOCK_DGRAM, IPPROTO_UDP, &dev_array[ batgat_dev_index]->socket ) < 0 ) {
		printk( KERN_ERR "batgat: error creating udp socket for %s\n", name );
		goto error;
	}

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = ifa->ifa_local;
	sa.sin_port = htons( (unsigned short)BATMAN_PORT );

	if( dev_array[ batgat_dev_index]->socket->ops->bind( dev_array[ batgat_dev_index]->socket, (struct sockaddr *) &sa, sizeof( sa ) ) ) {
		sock_release( dev_array[ batgat_dev_index]->socket );
		goto error;
	}

	strlcpy( dev_array[ batgat_dev_index ]->name, name, strlen( name ) + 1 );
	dev_array[ batgat_dev_index ]->index = batgat_dev_index;
	
	memset(dev_array[ batgat_dev_index ]->client, 0, sizeof(struct gw_client *) * 254 );
	
	init_completion( &dev_array[ batgat_dev_index ]->thread_complete );
	dev_array[ batgat_dev_index ]->thread_pid = kernel_thread( udp_server_thread, (void*)dev_array[ batgat_dev_index ], CLONE_KERNEL );

	if( dev_array[ batgat_dev_index ]->thread_pid < 0 ) {
		printk( "batgat: can't create thread\n" );
		sock_release( dev_array[ batgat_dev_index]->socket );
		goto error;
	}
	
	batgat_dev_index++;
	return( 0 );
error:
	if( dev_array[ batgat_dev_index ] )
		kfree( dev_array[ batgat_dev_index ] );
	if( dev_array )
		kfree( dev_array );
	return( 1 );
};

static int register_tun_device( struct net_device *dev, char *name )
{
	
	tun_array = ( struct tun_dev **)krealloc( tun_array, sizeof( struct tun_dev* )*( tun_dev_index + 1 ), GFP_KERNEL);

	if( tun_array == NULL || ( tun_array[ tun_dev_index ] = ( struct tun_dev* )kmalloc( sizeof( struct tun_dev ), GFP_KERNEL ) ) == NULL ) {
		printk( "batgat: can't allocate memory for tun_array\n" );
		goto error;
	}

	if( sock_create_kern( PF_INET, SOCK_RAW, IPPROTO_RAW, &tun_array[ tun_dev_index]->socket ) < 0 ) {
		printk( KERN_ERR "batgat: error creating tun socket for %s\n", name );
		goto error;
	}

	tun_array[ tun_dev_index]->socket->sk->sk_bound_dev_if = dev->ifindex;

	strlcpy( tun_array[ tun_dev_index ]->name, name, strlen( name ) + 1 );
	
	init_completion( &tun_array[ tun_dev_index ]->thread_complete );
	tun_array[ tun_dev_index ]->thread_pid = kernel_thread( tun_thread, (void*)tun_array[ tun_dev_index ], CLONE_KERNEL );

	if( tun_array[ tun_dev_index ]->thread_pid < 0 ) {
		printk( "batgat: can't create tun thread\n" );
		sock_release( tun_array[ tun_dev_index]->socket );
		goto error;
	}
	
	tun_dev_index++;
	return( 0 );
error:
	if( tun_array[ tun_dev_index ] )
		kfree( tun_array[ tun_dev_index ] );
	if( tun_array )
		kfree( tun_array );
	return( 1 );
};

static int unregister_batgat_device( char *name )
{
	int i=0;
		
	for( ; i < batgat_dev_index; i++ ) {
		if( strncmp( name, dev_array[i]->name, strlen(name) + 1 ) == 0 ) {

			if( dev_array[i]->thread_pid ) {
				kill_proc( dev_array[i]->thread_pid, SIGTERM, 0 );
				wait_for_completion( &dev_array[i]->thread_complete );
			}

			printk("batgat: element %d free %s\n",i,dev_array[i]->name);

			if(dev_array[i]->socket)
				sock_release(dev_array[i]->socket);
			dev_array[i]->socket = NULL;

		} else {
			printk("batgat: element %d don't free given %s current %s\n",i,name,dev_array[i]->name);
		}
	}
	
	return( 0 );

};

static int unregister_tun_device( char *name )
{
	int i=0;
		
	for( ; i < tun_dev_index; i++ ) {
		if( strncmp( name, tun_array[i]->name, strlen(name) + 1 ) == 0 ) {

			if( tun_array[i]->thread_pid ) {
				kill_proc( tun_array[i]->thread_pid, SIGTERM, 0 );
				wait_for_completion( &tun_array[i]->thread_complete );
			}

			printk("batgat: element %d free %s\n",i,tun_array[i]->name);

			if(tun_array[i]->socket)
				sock_release(tun_array[i]->socket);
			tun_array[i]->socket = NULL;

		} else {
			printk("batgat: element %d don't free given %s current %s\n",i,name,tun_array[i]->name);
		}
	}
	
	return( 0 );

};

static uint8_t get_virtual_ip( struct batgat_dev *dev, uint32_t client_addr)
{
	
	uint8_t i,first_free = 0;

	/* debug vars */
	char ip[20];
	/**************/

	for (i = 1;i<254;i++) {

		if( dev->client[ i ] != NULL ) {
			if( dev->client[ i ]->address == client_addr ) {
				ip2string( client_addr, ip );
				printk("batgat: client %s already exists. return %d\n", ip, i);
				return i;
			}
		} else {
			if ( first_free == 0 )
				first_free = i;
		}

	}


	if ( first_free == 0 ) {
		/* TODO: list is full */
		return -1;

	}

	dev->client[ first_free ] = kmalloc( sizeof( struct gw_client ), GFP_KERNEL );
	dev->client[ first_free ]->address = client_addr;

	/* TODO: check syscall for time*/
	dev->client[ first_free ]->last_keep_alive = 0;

	return first_free;
}

static int send_data( struct socket *socket, struct sockaddr_in *client, unsigned char *buffer, int buffer_len )
{
	struct iovec iov;
	struct msghdr msg;
	mm_segment_t oldfs;

	int error=0,len=0;

	msg.msg_name = NULL;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	iov.iov_base = buffer;
	iov.iov_len = buffer_len;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_flags = MSG_DONTWAIT;

	error = socket->ops->connect( socket, (struct sockaddr *)client, sizeof(*client), 0 );

	if( error != 0 ) {
		printk(KERN_ERR "batgat: can't connect to socket: %d\n",error);
		return 0;
	}
	
	oldfs = get_fs();
	set_fs( KERNEL_DS );

	len = sock_sendmsg( socket, &msg, buffer_len );

	if( len < 0 )
		printk( KERN_ERR "batgat: sock_sendmsg failed: %d\n",len);

	set_fs( oldfs );
	return( 0 );
}

static void ip2string(unsigned int sip,char *buffer)
{
	sprintf(buffer,"%d.%d.%d.%d",(sip & 255), (sip >> 8) & 255, (sip >> 16) & 255, (sip >> 24) & 255);
	return;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
