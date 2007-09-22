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
#include <linux/string.h>

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
static int tun_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt,struct net_device *orig_dev);

/* helpers */
static int is_not_valid_vip(uint32_t vip, struct iphdr *iph);
static int send_packet(uint32_t dev_addr, uint32_t dest,unsigned char *buffer,int buffer_len);
static unsigned short get_virtual_ip(unsigned int dev_addr, uint32_t client_addr);
static void raw_print(void *data, unsigned int length);
static void ip2string(unsigned int sip,char *buffer);


static struct file_operations fops = {
	.open = batgat_open,
	.release = batgat_release,
	.ioctl = batgat_ioctl,
};

static int Major;            /* Major number assigned to our device driver */

static struct dev_element **dev_array=NULL;
static int dev_index = 0;

/* testing */
struct socket *raw_sock;
struct msghdr msg;
struct iovec iov;
struct sockaddr_in addr_out;
/***********/


static int
batgat_ioctl( struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg )
{
	char *tmp = NULL, *colon_ptr;
	int command,length,i;

	struct net_device *tmp_dev = NULL;

	struct in_device *in_dev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;

	struct sockaddr_in sa;

	/* debug vars */
	unsigned char ip[20];
	/**************/


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

		if ( ( colon_ptr = strchr( tmp, ':' ) ) != NULL )
			*colon_ptr = '\0';

		if((tmp_dev = dev_get_by_name(tmp))==NULL) {
			printk("B.A.T.M.A.N. GW: Did not find device %s\n",tmp);
			goto clean_error_without;
		}

		if(command == IOCREMDEV)
			dev_put(tmp_dev);

		if ( colon_ptr != NULL )
			*colon_ptr = ':';

	} else {

		printk(KERN_ERR "B.A.T.M.A.N. GW: unknown ioctl\n");
		goto clean_error_without;

	}

	switch(command) {

		case IOCSETDEV:


			dev_array = (struct dev_element**)krealloc(dev_array,sizeof(struct dev_element*)*(dev_index + 1), GFP_KERNEL);

			if(dev_array == NULL || ( dev_array[dev_index] = (struct dev_element*)kmalloc(sizeof(struct dev_element),GFP_KERNEL)) == NULL ) {
				printk("B.A.T.M.A.N. GW: Allocate memory for device list\n");
				goto clean_error;
			}


			/* get ip address from interface */
			if((in_dev = __in_dev_get_rtnl(tmp_dev)) != NULL ) {
				
				for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL; ifap = &ifa->ifa_next) {

					if (!strcmp(tmp, ifa->ifa_label)) {
						ip2string(ifa->ifa_local,ip);
						printk("found %s with ip %s\n", ifa->ifa_label, ip);
						break;
					}

				}

			} else
				return -ENODEV;

			if(ifa == NULL) {
				printk(KERN_ERR "B.A.T.M.A.N. GW: can't find interface address for %s\n",tmp);
				goto clean_error;
			}

			if(strstr(tmp,"tun")) {

				dev_array[dev_index]->packet.type = htons(ETH_P_ALL);
				dev_array[dev_index]->packet.func = tun_func;
				dev_array[dev_index]->packet.dev = tmp_dev;
				dev_array[dev_index]->sock = NULL;

			} else {
				dev_array[dev_index]->packet.type = htons(ETH_P_IP);
				dev_array[dev_index]->packet.func = batgat_func;
				dev_array[dev_index]->packet.dev = tmp_dev;

				sa.sin_port = htons(BATMAN_PORT);
				sa.sin_family = AF_INET;
				sa.sin_addr.s_addr = ifa->ifa_local;

				/* init socket */
				if(sock_create(PF_INET,SOCK_DGRAM,IPPROTO_UDP,&dev_array[dev_index]->sock) < 0) {
					printk(KERN_ERR "B.A.T.M.A.N. GW: Error creating socket\n");
					goto clean_error;
				} else
					printk("B.A.T.M.A.N. GW: create socket for %s\n",tmp);

				if(dev_array[dev_index]->sock->ops->bind(dev_array[dev_index]->sock, (struct sockaddr*)&sa,sizeof(struct sockaddr_in)) < 0) {
					printk(KERN_ERR "B.A.T.M.A.N. GW: Error binding socket\n");
					goto clean_error;
				} else
					printk("B.A.T.M.A.N. GW: bind socket for %s\n",tmp);
			}

			dev_add_pack(&dev_array[dev_index]->packet);
			dev_array[dev_index]->free = 0;

			memset(dev_array[dev_index]->gw_client,0,sizeof(struct gw_client *)*254);
			dev_array[dev_index]->addr = ifa->ifa_local;
			strlcpy(dev_array[dev_index]->name, tmp,length+1);

			dev_index++;

			break;

		case IOCREMDEV:

			for(i=0;i<dev_index;i++) {
				if(strncmp(dev_array[i]->name,tmp,length+1) == 0) {
					printk("element %d free %s\n",i,dev_array[i]->name);
					if(dev_array[i]->sock)
						sock_release(dev_array[i]->sock);
					dev_array[i]->sock = NULL;

					dev_put(dev_array[i]->packet.dev);
					dev_remove_pack(&dev_array[i]->packet);

					dev_array[i]->free = 1;
					
				} else {
					printk("element %d don't free given %s current %s\n",i,tmp,dev_array[i]->name);
				}
			}

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
	if ( devfs_mk_cdev( MKDEV( Major, 0 ), S_IFCHR | S_IRUGO | S_IWUGO, "batgat", 0 ) )
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

	/* testing */
	
	if ( ( sock_create_kern( PF_INET, SOCK_RAW, IPPROTO_RAW, &raw_sock ) ) < 0 ) {

		printk( "B.A.T.M.A.N.: Can't create raw socket\n");
		return 1;

	}

	addr_out.sin_family = PF_INET;

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
	msg.msg_name = &addr_out;
	msg.msg_namelen = sizeof(addr_out);
	msg.msg_control = NULL;
	msg.msg_controllen = 0;

	return(0);
}

void
cleanup_module()
{
	int ret,i;

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

	for(i=0;i<dev_index;i++) {
		if(dev_array[i] != NULL) {
			printk("cleanup %s\n",dev_array[i]->name);

			if(dev_array[i]->sock)
				sock_release(dev_array[i]->sock);

			if(!dev_array[i]->free) {
				dev_remove_pack(&dev_array[i]->packet);
				dev_put(dev_array[i]->packet.dev);
			}
			
			kfree(dev_array[i]);
		}
	}

	if(!(dev_index-=i)) {
		kfree(dev_array);
		printk("cleanup array\n");
	} else {
		printk("dev_index not clean\n");
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
tun_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt,struct net_device *orig_dev)
{
	struct iphdr *iph = ip_hdr(skb);
	unsigned char *tmp_skb;

	int dev_addr = -1;
	int client_addr = -1;

	char ip1[20],ip2[20];
	ip2string(iph->saddr,ip1);
	ip2string(iph->daddr,ip2);

	printk("data from %s to tunnel, %s -> %s.....",skb->dev->name,ip1,ip2);

	dev_addr = (ntohl(iph->daddr)>>8)&255;
	client_addr = ntohl(iph->daddr)&255;

	if( ((ntohl(iph->daddr)>>24)&255) == 169 && dev_addr <= dev_index && dev_array[dev_addr]->gw_client[client_addr] != NULL ) {

		skb_push(skb,1);
		tmp_skb = (unsigned char*)skb->data;
		tmp_skb[0] = TUNNEL_DATA;
		send_packet(dev_addr,dev_array[dev_addr]->gw_client[client_addr]->addr, tmp_skb, skb->len);
		printk("forward packet\n");

	} else
		printk("drop packet\n");
	
	return 0;
}

static int
batgat_func(struct sk_buff *skb, struct net_device *dv, struct packet_type *pt,struct net_device *orig_dev)
{
	struct iphdr *iph = ip_hdr(skb);
	struct iphdr *real_iph = NULL;
	struct ethhdr *eth = (struct ethhdr*)skb_mac_header(skb);
	
	unsigned char *buffer,vip_buffer[VIP_BUFFER_SIZE];
	uint32_t ip_address,i;
	int send_dev_index = -1;

	/* debug vars */
	char ip1[20],ip2[20];

	/**************/

// 	if(strstr(dv->name, "tun")) {
// 		tun_func(skb);
// 		goto exit_batgat;
// 	}

	/* check if is a batman packet */
	if(!(ntohs(eth->h_proto) == ETH_P_IP && iph->protocol == IPPROTO_UDP && skb->pkt_type == PACKET_HOST &&
		    ntohs(((struct udphdr*)(skb->data + sizeof(struct iphdr)))->source) == BATMAN_PORT)) {

		if(((ntohl(iph->saddr)>>24)&255)==169) {
			ip2string(iph->saddr,ip1);
			ip2string(iph->daddr,ip2);
			printk("packet drop in batgat_func %s -> %s eth proto %u ip proto %u pkt type %u port %u\n",ip1,ip2,ntohs(eth->h_proto),iph->protocol,skb->pkt_type,
			       ntohs(((struct udphdr*)(skb->data + sizeof(struct iphdr)))->source));
		}
		goto exit_batgat;
	}

	buffer = (unsigned char*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr));

	switch(buffer[0]) {

		case TUNNEL_IP_REQUEST:

			for(i=0;i < dev_index;i++) {
				if(dev_array[i]->addr == iph->daddr) {
					send_dev_index = i;
					break;
				}
			}

			if(send_dev_index < 0) {
				ip2string(iph->daddr,ip1);
				printk("B.A.T.M.A.N. GW: device with %s not in dev_array\n",ip1);
				goto exit_batgat;
			}

			if((ip_address = (unsigned int)get_virtual_ip(send_dev_index, iph->saddr)) == 0) {
				printk(KERN_ERR "B.A.T.M.A.N. GW: don't get a virtual ip\n");
				goto exit_batgat;
			}

			ip_address = 169 + ( 254<<8 ) + ((uint8_t)(send_dev_index)<<16 ) + (ip_address<<24 );
			vip_buffer[0] = TUNNEL_DATA;
			memcpy(&vip_buffer[1], &ip_address, sizeof(ip_address));

			/* debug output */
			ip2string(iph->saddr,ip1);
			ip2string(ip_address,ip2);
			printk("B.A.T.M.A.N. GW: assign client %s vip %s\n", ip1, ip2);
			/****************/

			send_packet(send_dev_index,iph->saddr,vip_buffer,VIP_BUFFER_SIZE);
			break;

		case TUNNEL_DATA:

			real_iph = (struct iphdr*) (skb->data + sizeof(struct iphdr) + sizeof(struct udphdr) + 1);

			if(is_not_valid_vip(real_iph->saddr,iph))
				break;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)

// 			skb_pull(skb,TRANSPORT_PACKET_SIZE);
// 			skb->network_header = skb->data;
// 			skb->transport_header = skb->data;

			

			/* testing */
			iov.iov_base = real_iph;
			iov.iov_len = skb->len - (sizeof(struct iphdr) + sizeof(struct udphdr) + 1);

			addr_out.sin_port = 0;
			addr_out.sin_addr.s_addr = real_iph->daddr;



			i = sock_sendmsg(raw_sock,&msg,skb->len - 29);
			
			/**********/

			ip2string(real_iph->saddr, ip1);
			ip2string(real_iph->daddr, ip2);
			printk("debug: %d get data from tunnel %s data %s -> %s paket type %u\n",i,dv->name,ip1,ip2,skb->pkt_type);

#else
			/* TODO: change pointer for Kernel < 2.6.22 */

#endif

			break;
		default:
			goto exit_batgat;

	}

exit_batgat:
	kfree_skb(skb);
	return 0;
}

/* helpers */

static int
is_not_valid_vip(uint32_t vip, struct iphdr *iph)
{
	unsigned char *check_ip,send_buffer[VIP_BUFFER_SIZE];
	int send_dev_index = -1,i;
	
	check_ip = (unsigned char *)&vip;

	/* check if device in array with destination ip address */
	for(i=0;i < dev_index;i++) {
		if(dev_array[i]->addr == iph->daddr) {
			send_dev_index = i;
			break;
		}
	}

	if(send_dev_index < 0) {
		printk("B.A.T.M.A.N. GW: destination address don't match with dev_array");
		return 1;
	}
	
	if(check_ip[0] != 169 && check_ip[1] != 254)
		goto send_ip_invalid;


	if(dev_array[send_dev_index]->gw_client[check_ip[3]] != NULL && dev_array[send_dev_index]->gw_client[check_ip[3]]->addr == iph->saddr)
		return 0;

send_ip_invalid:

	send_buffer[0] = TUNNEL_IP_INVALID;
	memset(&send_buffer[1], 0, VIP_BUFFER_SIZE - 1);
	send_packet(send_dev_index,iph->saddr, send_buffer, VIP_BUFFER_SIZE);
	printk("B.A.T.M.A.N. GW: tunnel ip %d.%d.%d.%d is invalid\n",check_ip[0],check_ip[1],check_ip[2],check_ip[3]);
	return 1;

}

static int
send_packet(uint32_t dev_addr, uint32_t dest,unsigned char *buffer,int buffer_len)
{
	struct iovec iov;
	struct msghdr msg;
	mm_segment_t oldfs;
	struct sockaddr_in to;
	int error=0,len=0;

	to.sin_family = AF_INET;
	to.sin_addr.s_addr = dest;
	to.sin_port = htons( (unsigned short)BATMAN_PORT );

	msg.msg_name = NULL;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	iov.iov_base = buffer;
	iov.iov_len = buffer_len;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	msg.msg_flags = MSG_DONTWAIT;

	error = dev_array[dev_addr]->sock->ops->connect(dev_array[dev_addr]->sock,(struct sockaddr *)&to,sizeof(to),0);

	if(error != 0) {
		printk(KERN_ERR "B.A.T.M.A.N. GW: can't connect to socket: %d\n",error);
		return 0;
	}
	
	oldfs = get_fs();
	set_fs( KERNEL_DS );

 	len = sock_sendmsg( dev_array[dev_addr]->sock, &msg, buffer_len );

	if( len < 0 )
		printk( KERN_ERR "B.A.T.M.A.N. GW: sock_sendmsg failed: %d\n",len);

	set_fs( oldfs );

	return 0;
}

static unsigned short
get_virtual_ip(uint32_t dev_addr, uint32_t client_addr)
{
	
	uint8_t i,first_free = 0;

	/* debug vars */
	char ip[20];
	/**************/

 	for (i = 1;i<254;i++) {

		if(dev_array[dev_addr]->gw_client[i] != NULL) {
			if(dev_array[dev_addr]->gw_client[i]->addr == client_addr) {
				ip2string(client_addr,ip);
				printk("B.A.T.M.A.N. GW: client %s already exists. return %d\n", ip, i);
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

	dev_array[dev_addr]->gw_client[first_free] = kmalloc(sizeof(struct gw_client),GFP_KERNEL);
	dev_array[dev_addr]->gw_client[first_free]->addr = client_addr;

	/* TODO: check syscall for time*/
	dev_array[dev_addr]->gw_client[first_free]->last_keep_alive = 0;

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
