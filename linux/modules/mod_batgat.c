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



static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;            /* Major number assigned to our device driver */


static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	/* register net devices */
	/* create client list */
	/* start udp server thread */
	return(0);

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
	return(0);

}

static int batgat_release(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
	return(0);
}


MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
