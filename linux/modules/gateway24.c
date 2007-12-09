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


static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;

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
	printk(KERN_INFO "ioctl\n");
	return(0);
}


MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
