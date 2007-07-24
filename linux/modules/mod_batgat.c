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

#include <asm/uaccess.h>

#include <linux/module.h>
#include <linux/version.h>

#include <linux/byteorder/generic.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/icmp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/skbuff.h>

#include <net/protocol.h>
#include <net/pkt_sched.h>
#include <net/udp.h>
#include <net/ip.h>
#include <net/sock.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	#include <linux/devfs_fs_kernel.h>
#else
	static struct class *batman_class;
#endif

static int batgat_open(struct inode *inode, struct file *filp);
static int batgat_release(struct inode *inode, struct file *file);
static int batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg );
static int batgat_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt, struct net_device *orig_dev);

static unsigned int i2a(char* dest,unsigned int x);
static char *inet_ntoa_r(struct in_addr in,char* buf);


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
	struct udphdr *r_uhdr,*s_uhdr;
	
	unsigned char *buffer, n_buff[5], *buf;
	int len;
	unsigned int tmp;
	struct msghdr msg;
	struct iovec iov;
	mm_segment_t oldfs;
	struct sockaddr_in to;
	struct socket *clientsocket=NULL;
	
	struct ethhdr *eth = (struct ethhdr*)skb->mac.raw;
	struct ethhdr *eh;
	struct sk_buff *nskb;
	struct iphdr *iph;
	
	if(skb->nh.iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST) {
		r_uhdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
		if(ntohs(r_uhdr->source) == 1967) {
			
			printk("%02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x\n", eth->h_source[0],eth->h_source[1],eth->h_source[2],
				eth->h_source[3],eth->h_source[4],eth->h_source[5],eth->h_dest[0],eth->h_dest[1],eth->h_dest[2],eth->h_dest[3],eth->h_dest[4],eth->h_dest[5]);
			
			/* attention !!!
			nskb = dev_alloc_skb( sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(unsigned char) * 5 );
			
			if(nskb == NULL)
				return -ENOMEM;
			
			tmp = 169 + ( 254<<8 ) + ( 3<<16 ) + ( 2<<24 );
			memcpy( n_buff, "a", 1);
			memcpy( &n_buff[1], &tmp , sizeof(unsigned int));
			
			buf = (unsigned char*)skb_put(skb, 5);
			memset(buf, 0, 5);
			memcpy(buf, n_buff, 5);
			s_uhdr = (struct udphdr*)skb_put(skb, sizeof(struct udphdr));
			memset(s_uhdr, 0, sizeof(struct udphdr));
			s_uhdr->source = r_uhdr->dest;
			s_uhdr->dest = r_uhdr->source;
			s_uhdr->len = sizeof(struct udphdr);
			s_uhdr->check = 0;
			
			iph = (struct iphdr*)skb_put(skb, sizeof(struct iphdr));
			memset(iph,0,sizeof(struct iphdr));
			iph->daddr = skb->nh.iph->saddr;
			iph->saddr = skb->nh.iph->daddr;
			iph->protocol = skb->nh.iph->protocol;
			iph->check = 0;
			iph->ttl = 50;
			iph->version = skb->nh.iph->version;
			eh = (struct ethhdr*)skb_put(skb, sizeof(struct ethhdr));
			memset(eh, 0, sizeof(struct ethhdr));
			memcpy(eh->h_source, eth->h_dest, ETH_ALEN);
			memcpy(eh->h_dest, eth->h_source, ETH_ALEN);
			eh->h_proto = eth->h_proto;
			nskb->dev = orig_dev;
			
			// kernel freeze !!
			//nskb->head = eh;
			//nskb->data = iph;
			//nskb->tail = buf;
			//nskb->end = buf + 5;
			
			nskb->mac.raw = nskb->nh.raw = nskb->data;
			if( dev_queue_xmit(nskb) < 0 )
				printk("Error xmit\n");
			else
				printk("xmit correct\n");
			*/
			
			/* the socket version runs !
			if( sock_create( PF_INET,SOCK_DGRAM,IPPROTO_UDP,&clientsocket)<0 ) {
				printk( KERN_ERR "server: Error creating clientsocket.n" );
				return -EIO;
			}

			to.sin_family = AF_INET;
			to.sin_addr.s_addr = skb->nh.iph->saddr;  
			to.sin_port = htons( (unsigned short) 1967 );
			tmp = 169 + ( 254<<8 ) + ( 3<<16 ) + ( 2<<24 );

			msg.msg_name = &to;
			msg.msg_namelen = sizeof(to);
			memcpy( n_buff, "a", 1);
			memcpy( &n_buff[1], &tmp , sizeof(unsigned int));
			
			iov.iov_base = n_buff;
			iov.iov_len  = 10;
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
			msg.msg_iov    = &iov;
			msg.msg_iovlen = 1;

			oldfs = get_fs();
			set_fs( KERNEL_DS );
			len = sock_sendmsg( clientsocket, &msg, 10 );
			set_fs( oldfs );
			
			if( len < 0 )
				printk( KERN_ERR "sock_sendmsg returned: %d\n", len);
			else
				printk("sock send %d\n", len);

			if( clientsocket )
				sock_release( clientsocket );
			return 0;
			*/
			
			
			
			/* snippets don't uncomment */
			// nskb = dev_alloc_skb( sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(int) * 2 );
			
			// if(nskb == NULL)
			//	return -ENOMEM;
			
			//iphdr = (struct iphdr *)skb_put(nskb, sizeof(struct iphdr));
			//s_uhdr = (struct udphdr *)skb_put(nskb, sizeof(struct udphdr));
			//memset(iphdr, 0, sizeof(struct iphdr));
			//memset(s_uhdr, 0, sizeof(struct udphdr));
			
			/*
			s_uhdr = (struct udphdr *)(nskb->data + (nskb->nh.iph->ihl * 4));
			nskb->nh.iph->daddr = skb->nh.iph->saddr;
			nskb->nh.iph->saddr = skb->nh.iph->daddr;
			s_uhdr->source = r_uhdr->dest;
			s_uhdr->dest = r_uhdr->source;
			
			nskb->dev = orig_dev;
			nskb->mac.raw = nskb->nh.raw = nskb->data;
			if( dev_queue_xmit(nskb) < 0 )
				printk("Error xmit\n");
			else
				printk("xmit correct\n");
			*/
		}
	}
/*

	struct udphdr *net_hdr;
	char tmp_ip[16];
	struct in_addr in;
	unsigned char *buffer;
	unsigned int tmp;
	unsigned int adr;

	if(skb->nh.iph->protocol == IPPROTO_UDP) {

		net_hdr = (struct udphdr *)(skb->data + (skb->nh.iph->ihl * 4));
		in.s_addr = skb->nh.iph->saddr;
		inet_ntoa_r(in, tmp_ip);
		// printk("%s %u %u\n", tmp_ip,ntohs(net_hdr->source),ntohs(net_hdr->dest) == 1967);
		
		if( (unsigned short)ntohs(net_hdr->source) == 1967 && skb->len == 128) {

			buffer = (unsigned char*)((skb->data + (skb->nh.iph->ihl * 4)) + sizeof(struct udphdr));
			printk("%u %u %u\n", (unsigned int)skb->data_len, skb->len, (unsigned int)buffer[0]);
			tmp = 169 + ( 254<<8 ) + ( 3<<16 ) + ( 2<<24 );
			memcpy( &buffer[1], &tmp, sizeof(unsigned int));
			
			adr = skb->nh.iph->daddr;
			skb->nh.iph->daddr = skb->nh.iph->saddr;
			skb->nh.iph->saddr = adr;
			adr = net_hdr->source;
			net_hdr->source = net_hdr->dest;
			net_hdr->dest = adr;
			skb->pkt_type = PACKET_OUTGOING;

		} else if ( (unsigned short)ntohs(net_hdr->source) == 1966 ) {

			//printk("%x\n",(unsigned int)skb->data[0]);

		}
	}
*/
	kfree_skb(skb);
    return 0;

}

static unsigned int 
i2a(char* dest,unsigned int x)
{
	register unsigned int tmp=x;
	register unsigned int len=0;
	if (x>=100) { *dest++=tmp/100+'0'; tmp=tmp%100; ++len; }
	if (x>=10) { *dest++=tmp/10+'0'; tmp=tmp%10; ++len; }
	*dest++=tmp+'0';
	return len+1;
}

static char 
*inet_ntoa_r(struct in_addr in,char* buf)
{
	unsigned int len;
	unsigned char *ip=(unsigned char*)&in;
	len=i2a(buf,ip[0]); buf[len]='.'; ++len;
	len+=i2a(buf+ len,ip[1]); buf[len]='.'; ++len;
	len+=i2a(buf+ len,ip[2]); buf[len]='.'; ++len;
	len+=i2a(buf+ len,ip[3]); buf[len]=0;
	return buf;
}


MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SUPPORTED_DEVICE(DRIVER_DEVICE);
