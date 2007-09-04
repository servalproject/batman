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

#define TRANSPORT_PACKET_SIZE 29
#define BATMAN_PORT 4306

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

static void print_ip(unsigned int sip, unsigned int dip);
static int send_vip(struct sk_buff *skb);
static uint8_t get_ip_addr( uint32_t client_addr, char *ip_buff );

static int Major;            /* Major number assigned to our device driver */

static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

/* tunnel clients */
struct gw_client {
	uint32_t addr;
	uint32_t last_keep_alive;
};

static struct gw_client *gw_client_list[256];

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
					if(tmp)
						kfree(tmp);
					return -EFAULT;
				}

				if( (dev_entry->netdev = dev_get_by_name(tmp)) == NULL ) {
					printk("B.A.T.M.A.N. GW: Did not find device %s\n",tmp);
					if(tmp)
						kfree(tmp);
					return -EFAULT;
				}
				
				dev_entry->packet.type = __constant_htons(ETH_P_IP);
				dev_entry->packet.func = batgat_func;
				
				list_add_tail(&dev_entry->list, &device_list);
				dev_entry->packet.dev = dev_entry->netdev;
				dev_add_pack(&dev_entry->packet);

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
				printk("B.A.T.M.A.N. GW: Remove device %s...", tmp);
				
				if((rm_dev = dev_get_by_name(tmp))==NULL) {
					printk("did not find device %s\n",tmp);
					if(tmp)
						kfree(tmp);
					return -EFAULT;
				}

				list_for_each(ptr, &device_list) {
					dev_entry = list_entry(ptr, struct dev_element, list);
					if(dev_entry->netdev->ifindex == rm_dev->ifindex)
						break;
				}
				
				if(dev_entry->netdev->ifindex == rm_dev->ifindex) {
					dev_remove_pack(&dev_entry->packet);

					/* we must dev_put for every call of dev_get_by_name */
					dev_put(rm_dev);
					dev_put(dev_entry->netdev);
					
					list_del(&dev_entry->list);
					kfree(dev_entry);
					printk("ok\n");
				} else {
					printk("device %s not in list\n",tmp);
					if(tmp)
						kfree(tmp);
					return -EFAULT;
				}

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
	int i = 0;
	
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
	
	/* init gw_client_list */
	for( ;i< 255;i++)
		gw_client_list[i] = NULL;

	return(0);
}

void
cleanup_module()
{
    int ret, i=0;
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
	
	for( ;i < 255;i++) {		
		if(gw_client_list[i] != NULL)
			kfree(gw_client_list[i]);
	}

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
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
#else
	struct iphdr *iph = (struct iphdr*)skb->nh.iph;
#endif

	struct udphdr *uhdr;
	unsigned char *buffer;
	int i;
	
	if(iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
		
		uhdr = (struct udphdr *)(skb->data + sizeof(struct iphdr));
		buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));

#else
		uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
		buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
#endif
		
		if(ntohs(uhdr->source) == BATMAN_PORT && buffer[0] == 2) {
			
			send_vip(skb);

		} else if(ntohs(uhdr->source) == BATMAN_PORT && buffer[0] == 1) {
			printk("will ins inet\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)

			//~ printk("tail %p end %p head %p data %p ethhdr %p iph %p uhdr %p len %d mac_len %d\n", 
				   //~ skb->tail, skb->end, skb->head, skb->data, eth, iph, uhdr, skb->len, skb->mac_len);
			//~ printk("mh %p nh %p th %p len %d\n", skb->mac_header, skb->network_header, skb->transport_header,skb->len);
			
			//~ i = 0;
			
			//~ printk("\n");
			//~ for( ; i < skb->truesize; i++ ) {
				//~ if( i == 0 )
					//~ printk("%p| ",skb->head);

				//~ if( i != 0 && i%8 == 0 )
					//~ printk("  ");
				//~ if( i != 0 && i%16 == 0 )
					//~ printk("\n%p| ", &skb->head[i]);

				//~ printk("%02x ", skb->head[i] );
			//~ }
			//~ printk("\n\n");

			skb_pull(skb,TRANSPORT_PACKET_SIZE);
			skb->network_header = skb->data;
			skb->transport_header = skb->data;
			
			//~ i = 0;
			
			//~ printk("\n");
			//~ printk("tail %p end %p head %p data %p ethhdr %p iph %p uhdr %p len %d mac_len %d\n", 
				   //~ skb->tail, skb->end, skb->head, skb->data, eth, iph, uhdr, skb->len, skb->mac_len);
			//~ printk("mh %p nh %p th %p len %d\n", skb->mac_header, skb->network_header, skb->transport_header,skb->len);
			//~ for( ; i < skb->truesize; i++ ) {
				//~ if( i == 0 )
					//~ printk("%p| ",skb->head);

				//~ if( i != 0 && i%8 == 0 )
					//~ printk("  ");
				//~ if( i != 0 && i%16 == 0 )
					//~ printk("\n%p| ", &skb->head[i]);

				//~ printk("%02x ", skb->head[i] );
			//~ }
			//~ printk("\n\n");
			
#else

#endif
			
		}
		
		
	}
	
// 	if( ((ntohl(iph->saddr)>>24)&255) == 169 || ((ntohl(iph->daddr)>>24)&255) == 169)
		print_ip(iph->saddr, iph->daddr);
// 	else
// 		printk("no valid ip %d %d\n",(ntohl(iph->saddr)>>24)&255, iph->protocol );

	kfree_skb(skb);
    return 0;
}

/* helpers */

static void
print_ip(unsigned int sip, unsigned int dip)
{
	sip = ntohl(sip);
	dip = ntohl(dip);
	
	printk("%d.%d.%d.%d -> %d.%d.%d.%d\n", (sip >> 24) & 255, (sip >> 16) & 255,(sip >> 8) & 255, (sip & 255),
	       (dip >> 24) & 255, (dip >> 16) & 255,(dip >> 8) & 255, (dip & 255));

	return;
}

static int
send_vip(struct sk_buff *skb)
{
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)	
	unsigned char *buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
	struct udphdr *uhdr = (struct udphdr *)(skb->data + sizeof(struct iphdr));
	struct iphdr *iph = (struct iphdr*)skb_network_header(skb);
	struct ethhdr *eth = (struct ethhdr *)skb_mac_header(skb);
#else
	unsigned char *buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
	struct udphdr *uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
	struct iphdr *iph = (struct iphdr*)skb->nh.iph;
	struct ethhdr *eth = (struct ethhdr *)skb->mac.raw;
#endif

	unsigned int tmp,size;
	unsigned char dst_hw_addr[6];

	//~ return 0;
	
	
	/* TODO: address handling */
	
	if( (tmp = get_ip_addr(iph->saddr, buffer)) < 0 ) {
		/* TODO: error */
		return 0;
	}
	
	tmp = 169 + ( 254<<8 ) + ((uint8_t)(skb->dev->ifindex)<<16 ) + ( buffer[0]<<24 );
	
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

static uint8_t
get_ip_addr( uint32_t client_addr, char *ip_buff )
{
	uint8_t i,first_free = 0;

	for ( i = 1; i < 255; i++ ) {

		if ( gw_client_list[i] != NULL ) {

			if ( gw_client_list[i]->addr == client_addr ) {

				ip_buff[0] = i;
				return 0;

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

	gw_client_list[first_free] = kmalloc(sizeof(struct gw_client),GFP_KERNEL);
	gw_client_list[first_free]->addr = client_addr;
	/* TODO: check syscall */
	gw_client_list[first_free]->last_keep_alive = 0;

	ip_buff[0] = first_free;
	return 0;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
