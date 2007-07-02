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



/* Kernel Programming */
#define LINUX

#define SUCCESS 0

#define DRIVER_AUTHOR "Marek Lindner <lindner_marek@yahoo.de>"
#define DRIVER_DESC   "B.A.T.M.A.N. performance accelerator"
#define DRIVER_DEVICE "batman"


#include <linux/module.h>  /* needed by all modules */
#include <linux/kernel.h>  /* KERN_ALERT */
#include <linux/fs.h>      /* struct inode */
#include <linux/socket.h>  /* sock_create(), sock_release() */
#include <linux/net.h>     /* SOCK_RAW */
#include <linux/in.h>      /* IPPROTO_RAW */
#include <linux/ip.h>      /* ip */
#include <linux/udp.h>     /* udphdr */
#include <asm/uaccess.h>   /* get_user() */


#include <linux/inet.h>     /* SOCK_RAW */


static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);



struct orig_packet {
	struct iphdr ip;
	struct udphdr udp;
} __attribute__((packed));


static struct file_operations fops = {
	.write = device_write,
	.open = device_open,
	.release = device_release
};



static int Major;            /* Major number assigned to our device driver */
struct socket *Raw_sock = NULL;
static struct orig_packet orig_packet;
struct kiocb Kiocb;
struct sock_iocb Siocb;



int init_module( void ) {

	int retval;

	/* register our device - kernel assigns a free major number */
	if ( ( Major = register_chrdev( 0, DRIVER_DEVICE, &fops ) ) < 0 ) {

		printk( "B.A.T.M.A.N.: Registering the character device failed with %d\n", Major );
		return Major;

	}

	if ( ( retval = sock_create_kern( PF_INET, SOCK_RAW, IPPROTO_RAW, &Raw_sock ) ) < 0 ) {

		printk( "B.A.T.M.A.N.: Can't create raw socket: %i", retval );
		return retval;

	}

	/* Enable broadcast */
	sock_valbool_flag( Raw_sock->sk, SOCK_BROADCAST, 1 );

	init_sync_kiocb( &Kiocb, NULL );

	Kiocb.private = &Siocb;
	Siocb.sock = Raw_sock;
	Siocb.scm = NULL;

	printk( "B.A.T.M.A.N.: I was assigned major number %d. To talk to\n", Major );
	printk( "B.A.T.M.A.N.: the driver, create a dev file with 'mknod /dev/batman c %d 0'.\n", Major );
	printk( "B.A.T.M.A.N.: Remove the device file and module when done.\n" );

	return SUCCESS;

}



void cleanup_module( void ) {

	/* Unregister the device */
	int ret = unregister_chrdev( Major, DRIVER_DEVICE );

	if ( ret < 0 )
		printk( "B.A.T.M.A.N.: Unregistering the character device failed with %d\n", ret );

	sock_release( Raw_sock );

	printk( "B.A.T.M.A.N.: Unload complete\n" );

}



static int device_open( struct inode *inode, struct file *file ) {

	/* increment usage count */
// 	MOD_INC_USE_COUNT;
	try_module_get(THIS_MODULE);

	return SUCCESS;

}



static int device_release( struct inode *inode, struct file *file ) {

	/* decrement usage count */
// 	MOD_DEC_USE_COUNT;
	module_put(THIS_MODULE);

	return SUCCESS;

}


static ssize_t device_write( struct file *file, const char *buff, size_t len, loff_t *off ) {

	int i;
	struct msghdr msg;
	struct iovec iov;
	struct sockaddr_in addr_out;

// 	printk( KERN_INFO "device_write \n" );


	for ( i = 0; i < len && i <= sizeof(orig_packet); i++ )
		get_user( ((unsigned char *)&orig_packet)[i], buff + i );

	if ( len < sizeof(struct iphdr) + sizeof(struct udphdr) + 10 ) {

		printk( KERN_INFO "B.A.T.M.A.N.: dropping data - packet too small (%i)\n", len );

		return -EINVAL;

	}


	iov.iov_base = buff;
	iov.iov_len = len;

	memset( &addr_out, 0, sizeof(struct sockaddr_in) );
	addr_out.sin_family = AF_INET;
	addr_out.sin_port = orig_packet.udp.dest;
	addr_out.sin_addr.s_addr = orig_packet.ip.daddr;


	memset( &msg, 0, sizeof(struct msghdr) );
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	msg.msg_name = &addr_out;
	msg.msg_namelen = sizeof(addr_out);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;


	Siocb.msg = &msg;
	Siocb.size = i;

	return Raw_sock->ops->sendmsg( &Kiocb, Raw_sock, &msg, len );

}




MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);

