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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/inetdevice.h>

#include <net/pkt_sched.h>
#include <net/udp.h>
#include <net/sock.h>

#include "mod_batgat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	#include <linux/devfs_fs_kernel.h>
#else
	static struct class *batman_class;
#endif

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );
static int batgat_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt, struct net_device *orig_dev);

/* helpers */
static int send_packet(uint32_t dest,char *buffer,int buffer_len, short tunnel);
static unsigned short get_virtual_ip(unsigned int ifindex, uint32_t client_addr);
static void raw_print(void *data, unsigned int length);
static void ip2string(unsigned int sip,char *buffer);


static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;            /* Major number assigned to our device driver */
static struct list_head device_list;
static struct list_head gw_client_list;
static struct socket *sock=NULL;

static int
batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	char *tmp = NULL;
	int command,length,i;

	struct dev_element *dev_entry = NULL;
	struct gw_element *gw_element = NULL;
	struct list_head *dev_ptr = NULL;
	struct list_head *gw_ptr = NULL;
	struct list_head *gw_ptr_tmp = NULL;
	struct net_device *tmp_dev = NULL;

	
	/* cmd comes with 2 short values */
	command = cmd & 0x0000FFFF;
	length = cmd >> 16;

	if(command == IOCSETDEV || command == IOCREMDEV) {

		if( !access_ok(VERIFY_READ, (void __user*)arg, length)) {
			printk("B.A.T.M.A.N. GW: Access to memory area of arg not allowed\n");
			return -EFAULT;
		}

		if( (tmp = kmalloc( length+1, GFP_KERNEL)) == NULL)
		{
			printk("B.A.T.M.A.N. GW: Allocate memory for devicename failed\n");
			return -EFAULT;
		}
		__copy_from_user(tmp, (void __user*)arg, length);
		tmp[length] = 0;

		if((tmp_dev = dev_get_by_name(tmp))==NULL) {
			printk("B.A.T.M.A.N. GW: Did not find device %s\n",tmp);
			goto clean_error_without;
		}

	} else {

		printk(KERN_ERR "B.A.T.M.A.N. GW: unknown ioctl\n");
		goto clean_error_without;

	}
	
	switch(command) {

		case IOCSETDEV:

			if( (dev_entry = kmalloc(sizeof(struct dev_element), GFP_KERNEL)) == NULL) {
				printk("B.A.T.M.A.N. GW: Allocate memory for device list\n");
				goto clean_error;
			}

			dev_entry->packet.type = __constant_htons(ETH_P_ALL);
			dev_entry->packet.func = batgat_func;
			dev_entry->packet.dev = tmp_dev;
			dev_entry->ifindex = tmp_dev->ifindex;

			/* insert in device list */
			list_add_tail(&dev_entry->list, &device_list);
			/* register our function for packets from device */
			dev_add_pack(&dev_entry->packet);

			dev_put(tmp_dev);
			break;

		case IOCREMDEV:

			if(!list_empty(&gw_client_list)) {

				list_for_each_safe(gw_ptr,gw_ptr_tmp,&gw_client_list) {
					gw_element = list_entry(gw_ptr, struct gw_element, list);
					if(gw_element->ifindex == tmp_dev->ifindex) {
						for(i=0;i < 255;i++) {
							if(gw_element->client[i] != NULL)
								kfree(gw_element->client[i]);
						}
						list_del(gw_ptr);
						kfree(gw_element);
						break;
					}
				}
			} else
				printk("B.A.T.M.A.N. GW: No clients to delete.\n");


			if(!list_empty(&device_list)) {
				list_for_each(dev_ptr, &device_list) {
					dev_entry = list_entry(dev_ptr, struct dev_element, list);
					if(dev_entry->ifindex == tmp_dev->ifindex) {
						dev_remove_pack(&dev_entry->packet);
						dev_put(tmp_dev);
						list_del(&dev_entry->list);
						kfree(dev_entry);
						printk("B.A.T.M.A.N. GW: Remove device %s...\n", tmp);
						break;
					}
				}
			} else
				printk("B.A.T.M.A.N. GW: No devices to delete.\n");

			break;

	}
	
	if(tmp!=NULL)
		kfree(tmp);

	return(0);

clean_error:
	dev_put(tmp_dev);
clean_error_without:
	if(tmp)
		kfree(tmp);
	
	return -EFAULT;
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
	
	/* init gw_client_list */
	INIT_LIST_HEAD(&gw_client_list);

	/* init socket */
	if(sock_create(PF_INET,SOCK_DGRAM,IPPROTO_UDP,&sock) < 0) {
		printk(KERN_ERR "B.A.T.M.A.N. GW: Error creating socket\n");
		return -EIO;
	}

	return(0);
}

void
cleanup_module()
{
	int ret, i;
	struct gw_element *gw_element = NULL;
	struct list_head *gw_ptr = NULL;
	struct list_head *gw_ptr_tmp = NULL;

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
	
	if(!list_empty(&gw_client_list)) {

		list_for_each_safe(gw_ptr,gw_ptr_tmp,&gw_client_list) {
			gw_element = list_entry(gw_ptr,struct gw_element,list);

			for(i=0;i < 255;i++) {
				if(gw_element->client[i] != NULL)
					kfree(gw_element->client[i]);
			}
			list_del(gw_ptr);
			kfree(gw_element);
		}

	}

	if(sock)
		sock_release(sock);

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
	struct iphdr *iph = ip_hdr(skb);
	struct udphdr *uhdr;

	struct dev_element *dev_entry;
	struct list_head *dev_ptr;
	struct gw_element *gw_element = NULL;
	struct list_head *gw_ptr = NULL;
	
	unsigned char *buffer,vip_buffer[VIP_BUFFER_SIZE],tunnel_buffer[1600];
	unsigned short addr_part_3, addr_part_4;

	uint32_t tmp;
	
	if(iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST) {

		uhdr = (struct udphdr *)(skb->data + sizeof(struct iphdr));
		buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));
		
		if(ntohs(uhdr->source) == BATMAN_PORT && buffer[0] == TUNNEL_IP_REQUEST) {

			if((tmp = (unsigned int)get_virtual_ip(skb->dev->ifindex, iph->saddr)) == 0) {
				printk(KERN_ERR "B.A.T.M.A.N. GW: don't get a virtual ip\n");
				goto end;
			}

			tmp = 169 + ( 254<<8 ) + ((uint8_t)(skb->dev->ifindex)<<16 ) + (tmp<<24 );
			vip_buffer[0] = TUNNEL_DATA;
			memcpy(&vip_buffer[1], &tmp, sizeof(tmp));
			send_packet(iph->saddr, vip_buffer, VIP_BUFFER_SIZE,0);
			goto end;

		} else if(ntohs(uhdr->source) == BATMAN_PORT && buffer[0] == TUNNEL_DATA) {

			/* TODO: check if source in gw_client_list */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)

			skb_pull(skb,TRANSPORT_PACKET_SIZE);
			skb->network_header = skb->data;
			skb->transport_header = skb->data;

#else
			/* TODO: change pointer for Kernel < 2.6.22 */

#endif
			goto end;
		}
		
		
	}
	
	if( ((ntohl(iph->daddr)>>24)&255) == 169) {

		addr_part_3 = (ntohl(iph->daddr)>>8)&255;
		addr_part_4 = ntohl(iph->daddr)&255;
		list_for_each(dev_ptr, &device_list) {
			dev_entry = list_entry(dev_ptr, struct dev_element, list);
			if(dev_entry->ifindex == addr_part_3)
				break;
			else
				dev_entry = NULL;
		}
	
		if(!dev_entry) {
			printk("B.A.T.M.A.N. GW: interface in dev_list with index %d not found\n", addr_part_3);
			return -1;
		}
	
		/* search if interface index exists in gw_client_list */
		list_for_each(gw_ptr, &gw_client_list) {
			gw_element = list_entry(gw_ptr, struct gw_element, list);
			if(gw_element->ifindex == addr_part_3)
				break;
			else
				gw_element = NULL;
		}

		if(!gw_element) {
			printk("B.A.T.M.A.N. GW: interface in gw_list with index %d not found\n", addr_part_3);
			return -1;
		}

		if(gw_element->client[addr_part_4] == NULL)  {
			printk("B.A.T.M.A.N. GW: client %d not found\n", addr_part_4);
			return -1;
		}

		send_packet(gw_element->client[addr_part_4]->addr, skb->data, skb->len, 1);

	}
end:
	kfree_skb(skb);
	return 0;
}

/* helpers */

static int
send_packet(uint32_t dest,char *buffer,int buffer_len, short tunnel)
{
	struct iovec iov[2];
	struct msghdr msg;
	mm_segment_t oldfs;
	struct sockaddr_in to;
	int error,len;
	unsigned char tunnel_option[1];

	tunnel_option[0] = TUNNEL_DATA;

	to.sin_family = AF_INET;
	to.sin_addr.s_addr = dest;
	to.sin_port = htons( (unsigned short)BATMAN_PORT );
	
	msg.msg_name = NULL;
	
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = iov;
	
	if(tunnel) {
		iov[0].iov_base = tunnel_option;
		iov[0].iov_len  = 1;
		iov[1].iov_base = buffer;
		iov[1].iov_len = buffer_len;
		msg.msg_iovlen = 2;
	} else {
		iov[0].iov_base = buffer;
		iov[0].iov_len = buffer_len;
		iov[1].iov_len = 0;
		msg.msg_iovlen = 1;
	}


	error = sock->ops->connect(sock,(struct sockaddr *)&to,sizeof(to),0);
	oldfs = get_fs();
	set_fs( KERNEL_DS );

	if(error != 0) {
		printk(KERN_ERR "B.A.T.M.A.N. GW: can't connect to socket: %d\n",error);
		return 0;
	}
	len = sock_sendmsg( sock, &msg, iov[0].iov_len + iov[1].iov_len );

	if( len < 0 )
		printk( KERN_ERR "B.A.T.M.A.N. GW: sock_sendmsg failed: %d\n",len);
	
	set_fs( oldfs );
	
	return 0;
}

static unsigned short
get_virtual_ip(unsigned int ifindex, uint32_t client_addr)
{
	struct gw_element *gw_element = NULL;
	struct list_head *gw_ptr = NULL;
	uint8_t i,first_free = 0;
	
	/* search if interface index exists in gw_client_list */
	list_for_each(gw_ptr, &gw_client_list) {
		gw_element = list_entry(gw_ptr, struct gw_element, list);
		if(gw_element->ifindex == ifindex)
			goto ifi_found;
	}

	/* create gw_element */
	gw_element = kmalloc(sizeof(struct gw_element), GFP_KERNEL);

	if(gw_element == NULL)
		return 0;
	gw_element->ifindex = ifindex;
	
	for(i=0;i< 255;i++)
		gw_element->client[i] = NULL;
	
	list_add_tail(&gw_element->list, &gw_client_list);

ifi_found:
	/* assign ip */

	for (i = 1;i<255;i++) {
	
	if (gw_element->client[i] != NULL) {

		if ( gw_element->client[i]->addr == client_addr )

			return i;

	} else {

		if ( first_free == 0 )
			first_free = i;

	}

	}

	if ( first_free == 0 ) {
		/* TODO: list is full */
		return -1;

	}

	gw_element->client[first_free] = kmalloc(sizeof(struct gw_client),GFP_KERNEL);
	gw_element->client[first_free]->addr = client_addr;

	/* TODO: check syscall for time*/
	gw_element->client[first_free]->last_keep_alive = 0;

	return first_free;
}

static void
raw_print(void *data, unsigned int length)
{
	unsigned char *buffer = (unsigned char *)data;
	int i;

	printk("\n");
	for(i=0;i<length;i++) {
		if( i == 0 )
			printk("%p| ",&buffer[i]);

		if( i != 0 && i%8 == 0 )
			printk("  ");
		if( i != 0 && i%16 == 0 )
			printk("\n%p| ", &buffer[i]);

		printk("%02x ", buffer[i] );
	}
	printk("\n\n");
}
	
static void
ip2string(unsigned int sip,char *buffer)
{
	sprintf(buffer,"%d.%d.%d.%d",(sip & 255), (sip >> 8) & 255, (sip >> 16) & 255, (sip >> 24) & 255);
	return;
}

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
