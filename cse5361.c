#include <linux/module.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <net/protocol.h>
#include <net/sock.h>
#include <net/route.h>
#include <net/ip.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/semaphore.h>

#define MAX_QUEUE_SIZE	512
#define CSE536_MAJOR 234
#define IPPROTO_CSE536_PROTONUM 234

struct snt_pkt_dest
{
__be32 dest;
int bAck;
unsigned long j;
struct data_buffer * snd_buff;
}sent_pkt_queue[10];

static int snt_pkt_count = 0;

struct semaphore modify_current_time;



static char data[100];
static int debug_enable = 0;
static int current_clock = 1;
module_param(debug_enable, int, 0);
MODULE_PARM_DESC(debug_enable, "Enable module debug mode.");
struct file_operations cse536_fops;
struct task_struct *task;



__be32 dest_address;
__be32 src_address;

//data buffer to be used for storing as well as receiving
//data at this layer
struct cse536_buffer
{
	int record_id;
	int *final_clock;
	int *original_clock;
	__be32 src_ip;
	__be32 destination_ip;
	char data[236];
};

struct data_buffer
{
	int record_id;
	int final_clock;
	int original_clock;
	__be32 src_ip;
	__be32 destination_ip;
	char data[236];
};

//Circular Queue which is used to store the received packets
struct input_data_queue
{
	struct data_buffer *data_buffer_list[MAX_QUEUE_SIZE];
	int front;
	int rear;
}data_q;

static int buildandsendIPPacket(char *data, size_t length,__be32 source,__be32 destination);

int thread_function(void *data)
{
  int var;
  int i ;
  unsigned long current_time;
  var = 10;
  
  while(!kthread_should_stop())
  {
  	   for(i = 0;i < snt_pkt_count;i++)
  	   {
  	   		if(sent_pkt_queue[i].dest != 0)
  	   		{
  	   			current_time = jiffies;
  	   			if(((current_time - sent_pkt_queue[i].j) > (60* HZ) ) && !sent_pkt_queue[i].bAck)
  	   			{
  	   				printk("\ncse536:Time out occurred - total time taken till now -%lu seconds",(current_time - sent_pkt_queue[i].j)/HZ);
  	   				printk("\ncse536: packet sent at time-%lu",sent_pkt_queue[i].j);
  	   				printk("\ncse536: Current time-%lu",current_time);

  	   				printk("\ncse536:resending the packet");

  	   				buildandsendIPPacket((char*)sent_pkt_queue[i].snd_buff,sizeof(struct data_buffer),sent_pkt_queue[i].snd_buff->src_ip,sent_pkt_queue[i].snd_buff->destination_ip);

  	   				sent_pkt_queue[i].bAck = 0;
  	   				sent_pkt_queue[i].dest = 0;
  	   				sent_pkt_queue[i].j = 0;
  	   				sent_pkt_queue[i].snd_buff = NULL;
  	   			}
  	   		}
  	   }
       schedule(); 
             
   }
     /*do_exit(1);*/
  return var;

}

//Adding the received data packet into the circular Queue
int  enqueue(struct input_data_queue *Queue,struct data_buffer* buf)
{

	int pos = 0;
	
	if((Queue->rear + 1 % MAX_QUEUE_SIZE) == Queue->front) 
	{
		printk("Input Data Queue is full");
		return -1;
	}
 	pos= Queue->rear;
	Queue->data_buffer_list[pos] = buf;
	Queue->rear = (Queue->rear + 1 ) % MAX_QUEUE_SIZE;
	printk("\ncse536:enqueued succesfully front-%d,rear-%d",data_q.front,data_q.rear);
	return 0;
}

//Dequeing the received data packet to the application calling read
struct data_buffer*  dequeue(struct input_data_queue *Queue)
{
	struct data_buffer *data;
	int pos = Queue->front;
	data = Queue->data_buffer_list[pos];
	Queue->front = (Queue->front + 1 ) % MAX_QUEUE_SIZE;
	return data;
}


static int cse536_open(struct inode *inode, struct file *file)
{
	printk("\ncse536_open: successful");
	return 0;
}
static int cse536_release(struct inode *inode, struct file *file)
{
	printk("\ncse536_release: successful");
	return 0;
}
static ssize_t cse536_read(struct file *file, char *buf, size_t count,
loff_t *ptr)
{
     ssize_t retCount;
     struct data_buffer *temp = NULL;
     //struct data_buffer *read_buffer = NULL;

     printk("\ncse536:inside cse_536 read");

     //if queue is empty return 0
     if(data_q.front   == data_q.rear)
     {
	   return 0;
     }

     retCount = sizeof(struct data_buffer);

     temp = dequeue(&data_q);

     memcpy(buf,temp,sizeof(struct data_buffer));

     kfree(temp);
     temp = NULL;
         
     printk("\ncse536_read: returning %ld bytes", retCount);

     return retCount;	
		
}

static long cse536_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	printk("\ncse536_ioctl: cmd=%d, arg=%ld", cmd, arg);
	return 0;
}


static void buildIPHeader(struct iphdr *iphead,int length,__be32 source,__be32 destination)
{
	char asource[16] ={0};
	char adestination[16] = {0};

	printk("\ncse536:buildIPHeader");
	iphead->version  = 4;
	iphead->ihl      = 5;
	iphead->tos      = 0;
	iphead->frag_off = 0;
	iphead->ttl      = 64;
	iphead->daddr    = destination;
	iphead->saddr    = source;
	iphead->protocol = IPPROTO_CSE536_PROTONUM;
	iphead->id       = htons(1);
	iphead->tot_len  = htons(length);

	snprintf(asource, 16, "%pI4", &iphead->saddr); // Mind the &!
	snprintf(adestination, 16, "%pI4", &iphead->daddr); // Mind the &!
	printk("\ncse536:sending ack from %s to %s",asource,adestination);

}

static int sendIPPacket(struct sk_buff *skbuff,__be32 src,__be32 dest)
{

	int iretval = 0;
	struct rtable *rtab;
	struct net *net = &init_net;

	printk("\ncse536:sendIPPacket");
	rtab = ip_route_output(net,dest, src, 0,0);	
	skb_dst_set(skbuff, &rtab->dst);
	printk("cse536:no problem till here");
	
	iretval =  ip_local_out(skbuff);
	printk("\ncse536 sendIPPacket:%d",iretval);
	return iretval;
}

static int buildandsendIPPacket(char *data, size_t length,__be32 source,__be32 destination)
{
	struct sk_buff *skbuff;
	struct iphdr *iphead;
	int iretval = 0;
	
	unsigned char *skbdata;

	printk("\nCSE536:build and sendIPPacket");
	
	skbuff = alloc_skb(sizeof(struct iphdr) + 4096, GFP_ATOMIC);

	
	skb_reserve(skbuff, sizeof(struct iphdr) + 1500);
	skbdata = skb_put(skbuff, length);
	memcpy(skbdata, data, length);
	skb_push(skbuff, sizeof(struct iphdr));
	skb_reset_network_header(skbuff);
	iphead = ip_hdr(skbuff);
	buildIPHeader(iphead,skbuff->len,source,destination);
		
	iretval = sendIPPacket(skbuff,source,destination);
	return iretval;
    
}

static ssize_t cse536_write(struct file *file, const char *buf,
size_t count, loff_t * ppos)
{
   
   struct cse536_buffer *temp;
   struct data_buffer *snd_buff;
   char source[16] = {0};
	
   printk("\ncse536_write: sending IP Packet");
  
   temp = (struct cse536_buffer *)buf;

   snd_buff = kmalloc(sizeof(struct data_buffer), GFP_KERNEL);
   memset(snd_buff,0,sizeof(struct data_buffer));

   
   snd_buff->record_id = temp->record_id;
   printk("\ncse536:record id - %d",snd_buff->record_id);

   snd_buff->original_clock = current_clock;
   snd_buff->final_clock = 0;

   
   *(temp->original_clock) = current_clock;
   *(temp->final_clock) = 0;
 
   snd_buff->src_ip = src_address;
   snd_buff->destination_ip = temp->destination_ip;

   snprintf(source, 16, "%pI4", &snd_buff->destination_ip); // Mind the &!

   printk("\ncse536:int value -%u destination_ip ip is %s",snd_buff->src_ip,source);

   dest_address = temp->destination_ip;
   
   snd_buff->destination_ip = dest_address;
   memcpy(snd_buff->data,temp->data,sizeof(snd_buff->data));

   buildandsendIPPacket((char*)snd_buff,sizeof(struct data_buffer),snd_buff->src_ip,snd_buff->destination_ip);

   sent_pkt_queue[snt_pkt_count].j = jiffies;
   sent_pkt_queue[snt_pkt_count].bAck = 0;
   sent_pkt_queue[snt_pkt_count].dest = dest_address;
   sent_pkt_queue[snt_pkt_count].snd_buff = snd_buff;

   snt_pkt_count ++;

   if(snt_pkt_count > 9)
   {
   	 snt_pkt_count = 0;
   } 

   down(&modify_current_time);

   current_clock++;

   up(&modify_current_time);

   return -1;	
}

int cse536_rcv(struct sk_buff *skbuff)
{	
	int iretval = 0;
	char source[16] ={0};
	char destination[16] = {0};
	int i;

	struct data_buffer rcv_buff;

	struct data_buffer *rcv_data = kmalloc(sizeof(struct data_buffer), GFP_KERNEL);

	memset(rcv_data,0,sizeof(struct data_buffer));

	memcpy(rcv_data,skbuff->data,skbuff->len);

	memset(&rcv_buff,0,sizeof(rcv_buff));
	

	memcpy(&rcv_buff,skbuff->data,skbuff->len);

	snprintf(source, 16, "%pI4", &rcv_buff.src_ip); // Mind the &!

	printk("\n cse536_rcv:record_id - %d",rcv_buff.record_id);
	printk("\n cse536_rcv:int value -%u source ip is %s",rcv_buff.src_ip,source);
	printk("\ncse536 rcv:Received a Data Packet of length-%d from %s",skbuff->len,source);
	printk("\n cse536_rcv:original_clock - %d",rcv_buff.original_clock);
	printk("\n cse536_rcv:final_clock - %d",rcv_buff.final_clock);
	printk("\n cse536_rcv:received string - %s",rcv_buff.data);

	if(rcv_buff.record_id == 1)
	{

		if(current_clock < rcv_buff.original_clock)
		{
			down(&modify_current_time);

			current_clock = rcv_buff.original_clock;

			up(&modify_current_time);
		}

		rcv_buff.final_clock = current_clock;

		rcv_buff.record_id = 0;

		snprintf(source, 16, "%pI4", &rcv_buff.destination_ip); // Mind the &!
		snprintf(destination, 16, "%pI4", &rcv_buff.src_ip); // Mind the &!
		printk("\ncse536:sending ack from %s to %s",source,destination);
		buildandsendIPPacket((char*)&rcv_buff,sizeof(struct data_buffer),rcv_buff.destination_ip,rcv_buff.src_ip);
	}
	else
	{

		if(current_clock < rcv_buff.final_clock)
		{
			down(&modify_current_time);
			current_clock = rcv_buff.final_clock;
			up(&modify_current_time);
		}

		for(i = 0;i < snt_pkt_count;i++)
  	   	{
  	   		if(rcv_buff.destination_ip == sent_pkt_queue[i].dest)
  	   		{
  	   			sent_pkt_queue[i].bAck = 1;
  	   		}

  	   	}

	}
		
	iretval = enqueue(&data_q,rcv_data);
	
	if(iretval < 0)
	{
		printk("\ncse536 received packet is dropped as queue is full");
	}

	return 0;
}

void cse536_err(struct sk_buff *skb, __u32 info)
{
	printk("\nMy Error handler is called.\n");
}

//structure of our new protocol
static const struct net_protocol cse536_protocol = {
	.handler     = cse536_rcv,
	.err_handler = cse536_err,
	.no_policy   = 1,
    .netns_ok = 1,
};

//getting the local address from the ethernet interface
static void getlocaladdress(void) 
{
	struct net_device *eth0 = dev_get_by_name(&init_net, "eth0");
	struct in_device *ineth0 = in_dev_get(eth0);

	for_primary_ifa(ineth0){
		src_address = ifa->ifa_address;
  	} endfor_ifa(ineth0);
}




static int __init cse536_init(void)
{
	int ret;
	printk("cse536 module Init - debug mode is %s\n",
	debug_enable ? "enabled" : "disabled");
	
	//adding the protocol into IP Protocol stack
	inet_add_protocol(&cse536_protocol, IPPROTO_CSE536_PROTONUM);
    	getlocaladdress();
	printk(" using local address: %pI4\n", &src_address);
	
	sema_init(&modify_current_time,1);
	
	//Initializing the Queue head and tail
	data_q.front = 0;
	data_q.rear = 0;
	
	
	ret = register_chrdev(CSE536_MAJOR, "cse5361", &cse536_fops);
	if (ret < 0) {
		printk("Error registering cse536 device\n");
	goto cse536_fail1;
	}

   task = kthread_run(&thread_function,(void *)data,"sriram");
   printk(KERN_INFO"Kernel Thread : %s\n",task->comm);
	printk("cse536: registered module successfully!\n");
	/* Init processing here... */
	return 0;
	cse536_fail1:
	return ret;
}


static void __exit cse536_exit(void)
{
    inet_del_protocol(&cse536_protocol, IPPROTO_CSE536_PROTONUM);
    kthread_stop(task);
	unregister_chrdev(CSE536_MAJOR, "cse5361"); 
	printk("cse536 module Exit\n");
}
struct file_operations cse536_fops = {
	owner: THIS_MODULE,
	read: cse536_read,
	write: cse536_write,
	unlocked_ioctl: cse536_ioctl,
	open: cse536_open,
	release: cse536_release,
};
module_init(cse536_init);
module_exit(cse536_exit);

MODULE_AUTHOR("Sriram Ganapathyraman");
MODULE_DESCRIPTION("cse536 Module");
MODULE_LICENSE("GPL");
