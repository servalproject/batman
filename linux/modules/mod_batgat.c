/*
 * Copyright (C) 2006 BATMAN contributors:
 * Andreas Langer
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

#define DRIVER_AUTHOR "Andreas Langer <a.langer@q-dsl.de>"
#define DRIVER_DESC   "batman gateway module"
#define DRIVER_DEVICE "batgat"

/* io controls */
#define IOCSETDEV 1
#define IOCREMDEV 2

#include <linux/module.h>
#include <linux/version.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <net/pkt_sched.h>
#include <net/udp.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	#include <linux/devfs_fs_kernel.h>
#else
	static struct class *batman_class;
#endif

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );
static int batgat_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt, struct net_device *orig_dev);

static int send_vip(struct sk_buff *skb);


static int Major;            /* Major number assigned to our device driver */

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static struct packet_type packet = {
	.type = __constant_htons(ETH_P_IP),
	.func = batgat_func,
};

// static struct device_list {
	struct net_device *netdev;
// };

static int
batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	char *tmp=NULL;
	int command;
	int length;

	/* cmd comes with 2 short values */
	command = cmd & 0x0000FFFF;
	length = cmd >> 16;

	switch(command)
	{
		case IOCSETDEV:
			if( access_ok(VERIFY_READ, (void __user*)arg, length))
			{
				if( (tmp = kmalloc( length+1, GFP_KERNEL)) == NULL)
				{
					printk("B.A.T.M.A.N. GW: Allocate memory for devicename failed\n");
					return -EFAULT;
				}
				__copy_from_user(tmp, (void __user*)arg, length);
				tmp[length] = 0;
				printk("B.A.T.M.A.N. GW: Register device %s\n", tmp);
				netdev = dev_get_by_name(tmp);
				if(!netdev)
					printk("B.A.T.M.A.N. GW: Did not find device %s\n",tmp);
				else {
					packet.dev = netdev;
					dev_add_pack(&packet);
				}

			} else {

				printk("B.A.T.M.A.N. GW: Access to memory area of arg not allowed\n");
				return -EFAULT;

			}
		    break;
		case IOCREMDEV:
			if( access_ok(VERIFY_READ, (void __user*)arg, length))
			{
				if( (tmp = kmalloc( length+1, GFP_KERNEL)) == NULL)
				{
					printk("B.A.T.M.A.N. GW: Allocate memory for devicename failed\n");
					return -EFAULT;
				}
				__copy_from_user(tmp, (void __user*)arg, length);
				tmp[length] = 0;
				printk("B.A.T.M.A.N. GW: Remove device %s\n", tmp);
				dev_remove_pack(&packet);

			} else {

				printk("B.A.T.M.A.N. GW: Access to memory area of arg not allowed\n");
				return -EFAULT;

			}
			break;
		default:
		    return -EINVAL;
    }
	
	if(tmp!=NULL)
		kfree(tmp);

	return(0);
}

int
init_module()
{

	/* register our device - kernel assigns a free major number */
	if ( ( Major = register_chrdev( 0, DRIVER_DEVICE, &fops ) ) < 0 ) {

		printk( "B.A.T.M.A.N. GW: Registering the character device failed with %d\n", Major );
		return Major;

	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	if ( devfs_mk_cdev( MKDEV( Major, 0 ), S_IFCHR | S_IRUGO | S_IWUGO, "batgat", 0 ) ) {
		printk( "B.A.T.M.A.N. GW: Could not create /dev/batgat \n" );
#else
	batman_class = class_create( THIS_MODULE, "batgat" );

	if ( IS_ERR(batman_class) )
		printk( "B.A.T.M.A.N. GW: Could not register class 'batgat' \n" );
	else
		class_device_create( batman_class, NULL, MKDEV( Major, 0 ), NULL, "batgat" );
#endif


	printk( "B.A.T.M.A.N. GW: I was assigned major number %d. To talk to\n", Major );
	printk( "B.A.T.M.A.N. GW: the driver, create a dev file with 'mknod /dev/batgat c %d 0'.\n", Major );
	printk( "B.A.T.M.A.N. GW: Remove the device file and module when done.\n" );

	return(0);
}

void
cleanup_module()
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
		printk( "B.A.T.M.A.N. GW: Unregistering the character device failed with %d\n", ret );

	printk( "B.A.T.M.A.N. GW: Unload complete\n" );
}


static int
batgat_open(struct inode *inode, struct file *filp)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_INC_USE_COUNT;
#else
	try_module_get(THIS_MODULE);
#endif
	return(0);

}

static int 
batgat_release(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
	return(0);
}

static int
batgat_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt,struct net_device *orig_dev)
{
	struct udphdr *uhdr;
	unsigned char *buffer;

	if(skb->nh.iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST) {
		uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
		buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));

		if(ntohs(uhdr->source) == 1967 && buffer[0] == 2) {
			
			send_vip(skb);

		}
	}
	kfree_skb(skb);
    return 0;
}

/* helpers */

static int
send_vip(struct sk_buff *skb)
{
	unsigned char *buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
	struct udphdr *uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
	struct ethhdr *eth = (struct ethhdr*)skb->mac.raw;
	unsigned int tmp,size;
	unsigned char dst_hw_addr[6];

	
	/* TODO: address handling */
	tmp = 169 + ( 254<<8 ) + ( 3<<16 ) + ( 2<<24 );
	/* TODO: memset buffer */	
	buffer[0] = 1;
	memcpy( &buffer[1], &tmp , sizeof(unsigned int));
	
	skb->pkt_type = PACKET_OUTGOING;
	/* replace source and destination address */
	tmp = skb->nh.iph->saddr;
	skb->nh.iph->saddr = skb->nh.iph->daddr;
	skb->nh.iph->daddr = tmp;
	
	/* change checksum */
	size = skb->len - skb->nh.iph->ihl*4;
	uhdr->len = htons(size);
	
	uhdr->check = 0;
	uhdr->check = csum_tcpudp_magic(skb->nh.iph->saddr, skb->nh.iph->daddr,size, IPPROTO_UDP,csum_partial((char *)uhdr,size, 0));
	if (!uhdr->check)
		uhdr->check = CSUM_MANGLED_0;
	
	ip_send_check(skb->nh.iph);
	
	/* replace mac address for destination */
	memcpy(dst_hw_addr, eth->h_source, 6);
	if (skb->dev->hard_header)
		skb->dev->hard_header(skb,skb->dev,ntohs(skb->protocol),dst_hw_addr,skb->dev->dev_addr,skb->len);
	dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));
	return 0;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
