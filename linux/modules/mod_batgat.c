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
#include <linux/list.h>
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

/* static struct packet_type packet = {
 * 	.type = __constant_htons(ETH_P_IP),
 * 	.func = batgat_func,
 * };
 */

/* tunnel clients */
struct gw_client {
	uint32_t addr;
	uint32_t last_keep_alive;
} gw_client[256];

uint8_t free_client = 0;


struct dev_element {
	struct list_head list;
	struct net_device *netdev;
	struct packet_type packet;
};

static struct list_head device_list;


static int
batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	char *tmp=NULL;
	int command;
	int length;
	struct dev_element *dev_entry = NULL;
	struct net_device *rm_dev = NULL;
	struct list_head *ptr = NULL;

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
				
				if( (dev_entry = kmalloc(sizeof(struct dev_element), GFP_KERNEL)) == NULL) {
					printk("B.A.T.M.A.N. GW: Allocate memory for device list\n");
					return -EFAULT;
				}
				
				dev_entry->netdev = dev_get_by_name(tmp);
				dev_entry->packet.type = __constant_htons(ETH_P_IP);
				dev_entry->packet.func = batgat_func;
				
				list_add_tail(&dev_entry->list, &device_list);
				
				if(!dev_entry->netdev)
					printk("B.A.T.M.A.N. GW: Did not find device %s\n",tmp);
				else {
					dev_entry->packet.dev = dev_entry->netdev;
					dev_add_pack(&dev_entry->packet);
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
				
				rm_dev = dev_get_by_name(tmp);
				
				list_for_each(ptr, &device_list) {
					dev_entry = list_entry(ptr, struct dev_element, list);
					if(dev_entry->netdev->ifindex == rm_dev->ifindex)
						break;
				}
				
				if(dev_entry) {
					dev_remove_pack(&dev_entry->packet);
					list_del(&dev_entry->list);
					kfree(dev_entry);
				} else
					printk("B.A.T.M.A.N. GW: Can't remove device from dev_list.\n");

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
		
	/* init device list */
	INIT_LIST_HEAD(&device_list);

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
	struct iphdr *iph = (struct iphdr*)skb_network_header(skb);
#else
	struct iphdr *iph = (struct iphdr*)skb->nh.iph;
#endif

	struct udphdr *uhdr;
	unsigned char *buffer;
	
	if(iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
		//~ uhdr = (struct udphdr *)skb_transport_header(skb);
		//~ buffer = (unsigned char*)(skb_transport_header(skb) + sizeof(struct udphdr));
		
		uhdr = (struct udphdr *)(skb->data + sizeof(struct iphdr));
		buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
		//~ uhdr = (struct udphdr *)skb->transport_header;
		//~ buffer = (unsigned char*)(uhdr + sizeof(struct udphdr));
#else
		uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
		buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
#endif

		
		printk("skb_len %d %p %p %p %p %u %u\n", skb->len, skb, buffer, uhdr, iph, (unsigned int)buffer[0], ntohs(uhdr->source));
		if(ntohs(uhdr->source) == 1967 && buffer[0] == 2) {
			printk("before send\n");
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
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)	
	unsigned char *buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
	struct udphdr *uhdr = (struct udphdr *)(skb->data + sizeof(struct iphdr));
	struct iphdr *iph = (struct iphdr*)skb_network_header(skb);
	//~ struct udphdr *uhdr = (struct udphdr *)skb_transport_header(skb);
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
#else
	unsigned char *buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
	struct udphdr *uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
	struct iphdr *iph = (struct iphdr*)skb->nh.iph;
	struct ethhdr *eth = (struct ethhdr *)skb->mac.raw;
#endif

	unsigned int tmp,size;
	unsigned char dst_hw_addr[6];

	//~ printk("%x:%x:%x:%x:%x:%x\n",eth->h_source[0],eth->h_source[1],eth->h_source[2],eth->h_source[3],eth->h_source[4],eth->h_source[5]);
	//~ return 0;
	
	
	/* TODO: address handling */
	tmp = 169 + ( 254<<8 ) + ( 3<<16 ) + ( 2<<24 );
	/* TODO: memset buffer */	
	buffer[0] = 1;
	memcpy( &buffer[1], &tmp , sizeof(unsigned int));
	
	skb->pkt_type = PACKET_OUTGOING;
	/* replace source and destination address */
	tmp = iph->saddr;
	//~ tmp = skb->nh.iph->saddr;
	iph->saddr = iph->daddr;
	//~ skb->nh.iph->saddr = skb->nh.iph->daddr;
	iph->daddr = tmp;
	//~ skb->nh.iph->daddr = tmp;
	
	/* change checksum */
	size = skb->len - iph->ihl*4;
	//~ size = skb->len - skb->nh.iph->ihl*4;
	uhdr->len = htons(size);
	
	uhdr->check = 0;
	uhdr->check = csum_tcpudp_magic(iph->saddr, iph->daddr,size, IPPROTO_UDP,csum_partial((char *)uhdr,size, 0));
	//~ uhdr->check = csum_tcpudp_magic(skb->nh.iph->saddr, skb->nh.iph->daddr,size, IPPROTO_UDP,csum_partial((char *)uhdr,size, 0));
	if (!uhdr->check)
		uhdr->check = CSUM_MANGLED_0;
	
	ip_send_check(iph);
	
	/* replace mac address for destination */
	memcpy(dst_hw_addr, eth->h_source, 6);
	if (skb->dev->hard_header)
		skb->dev->hard_header(skb,skb->dev,ntohs(skb->protocol),dst_hw_addr,skb->dev->dev_addr,skb->len);
	dev_queue_xmit(skb_clone(skb, GFP_ATOMIC));
	return 0;
}

uint8_t
get_ip_addr( uint32_t client_addr, char *ip_buff, struct gw_client *gw_client[] )
{
	return 0;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
