#include <stdio.h>

#include "contiki.h"
#include "net/rime.h"

#include "lib/list.h"
#include "lib/memb.h"

#include "dev/button-sensor.h"
#include "dev/leds.h"

// RSS
#include "dev/cc2420.h"
#include "dev/cc2420_const.h"

#include "common.h"


/*---------------------------------------------------------------------------*/
PROCESS(simple_node_process, "simple node");
AUTOSTART_PROCESSES(&simple_node_process);
/*---------------------------------------------------------------------------*/
/*--------------------------VARIABLES----------------------------------------*/
/*---------------------------------------------------------------------------*/
static uint8_t rank = 255;
static rimeaddr_t parent;
static signed char parent_RSS = -100;
// hops is parent rank
static uint8_t parent_hops = 100; 
LIST(custom_route_table);
MEMB(custom_route_mem, struct custom_route_entry, MAX_ROUTE_ENTRIES);
/*---------------------------------------------------------------------------*/
/*--------------------------ROUTING---------------------------------------------*/
/*---------------------------------------------------------------------------*/
// Give information about the node help other nodes choose a parent 
static void send_DIO()
{
  packet_DIO.type = 0;
  packet_DIO.rank = rank;
  packet_DIO.parent = parent;
  packetbuf_copyfrom((void*)&packet_DIO, sizeof(packet_DIO));
  broadcast_send(&broadcast);
  printf("DIO sent\n");
}
// Indicate that a destination is reachable through this node (used her to advertise the route to the parents)
// Sent upward to the gateway
static void send_DAO(rimeaddr_t *dest)
{
  packet_DAO.type = 1;
  packet_DAO.dest = *dest;
  if(!runicast_is_transmitting(&runicast)) 
  {
    packetbuf_copyfrom((void*)&packet_DAO, sizeof(packet_DAO));
    runicast_send(&runicast, &parent, MAX_RETRANSMISSIONS);
    printf("DAO sent!\n");
  }
}
/*---------------------------------------------------------------------------*/
/*--------------------------GENERAL PURPOSE----------------------------------*/
/*---------------------------------------------------------------------------*/
static void check_parent_change(const rimeaddr_t * addr, signed char recv_RSS, uint8_t recv_Hops)
{
    if(rimeaddr_cmp(addr, &parent))
    {
      // update RSS and hops
      parent_RSS = recv_RSS;
      parent_hops = recv_Hops;
      return;
    }
    // If the gateway is found
    if(recv_Hops == 0)
    {
      printf("Gateway found, has address %d.%d\n", 
            addr->u8[0],
            addr->u8[1]);
      rank = recv_Hops + 1;
    }
    // Check if there is already a parent
    else if(rimeaddr_cmp(&parent, &rimeaddr_null))
    {
      printf("Use first node encountered as parent \n");
    }
    // Check if node has better signal than current parent
    else if(recv_RSS > parent_RSS)
    { 
      printf("Found closer node \n");
    }
    else
      {return ;}

    rimeaddr_copy(&parent, addr);
    parent_RSS = recv_RSS;
    parent_hops = recv_Hops;
    printf("Change parent to %d.%d\n", 
            parent.u8[0],
            parent.u8[1]);
    rank = recv_Hops + 1;
}
static signed char get_last_rss()
{
  // RSS variables
  static signed char rss;
  static signed char rss_val;
  static signed char rss_offset;

  // Compute RSS
  rss_val = cc2420_last_rssi;
  rss_offset=-45;
  rss=rss_val + rss_offset;
  printf("RSSI of Last Packet Received is %d\n",rss);

  return rss;
}
/*---------------------------------------------------------------------------*/
/*--------------------------BROADCAST----------------------------------------*/
/*---------------------------------------------------------------------------*/
// Receive broadcast message
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  static void* received;
  static char type; 
  received = packetbuf_dataptr();
  type = ((char*)received)[0];
  printf("Type received : %d\n", type);
  if(type == 0)
  {
    process_DIO((struct DIO*)received, from);
  }
}

// Variables
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

static void process_DAO(struct DAO * dao, rimeaddr_t * nextNode)
{
  static struct custom_route_entry *e;

  printf("DAO to %d.%d via %d.%d received\n",
    dao->dest.u8[0],
    dao->dest.u8[1],
    nextNode->u8[0],
    nextNode->u8[1]);
  // update route table
  e = NULL;
  // check if already present, if so update it
  for(e = list_head(custom_route_table); e != NULL; e = e->next) {
    printf("Route entry : %d.%d via %d.%d\n", 
      e->dest.u8[0],
      e->dest.u8[1],
      e->nextNode.u8[0],
      e->nextNode.u8[1]);
    if(rimeaddr_cmp(&e->dest, &dao->dest)) {
      rimeaddr_copy(&e->nextNode, nextNode);
      printf("Update entry of routing table\n");
      break;
    }
  }
  // otherwise add it
  if(e == NULL)
  {
    e = memb_alloc(&custom_route_mem);
    rimeaddr_copy(&e->nextNode, nextNode);
    rimeaddr_copy(&e->dest, &dao->dest);
    list_push(custom_route_table, e);
    printf("Add new entry to routing table\n");
  }
}
static void process_DIO(struct DIO * dio, const rimeaddr_t *from)
{
  // Hops variable
  static uint8_t hops;
  // Number hops
  //hops = packetbuf_attr(PACKETBUF_ATTR_HOPS);
  // hops can be represented as the rank of the node, gateway has rank 0

  // Rss
  static signed char rss;

  rss = get_last_rss();
  hops = dio->rank;

  // If other node parent is this node, discard
  if(rimeaddr_cmp(&rimeaddr_node_addr, &dio->parent))
    return;

  check_parent_change(from, rss, hops);

}
/*---------------------------------------------------------------------------*/
/*--------------------------UNICAST------------------------------------------*/
/*---------------------------------------------------------------------------*/
LIST(history_table);
MEMB(history_mem, struct history_entry, NUM_HISTORY_ENTRIES);
/*---------------------------------------------------------------------------*/
static void
recv_runicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno)
{
  static char type;

  /* OPTIONAL: Sender history */
  struct history_entry *e = NULL;
  for(e = list_head(history_table); e != NULL; e = e->next) {
    if(rimeaddr_cmp(&e->addr, from)) {
      break;
    }
  }
  if(e == NULL) {
    /* Create new history entry */
    e = memb_alloc(&history_mem);
    if(e == NULL) {
      e = list_chop(history_table); /* Remove oldest at full history */
    }
    rimeaddr_copy(&e->addr, from);
    e->seq = seqno;
    list_push(history_table, e);
  } else {
    /* Detect duplicate callback */
    if(e->seq == seqno) {
      printf("runicast message received from %d.%d, seqno %d (DUPLICATE)\n",
	     from->u8[0], from->u8[1], seqno);
      return;
    }
    /* Update existing history entry */
    e->seq = seqno;
  }

  void* received = packetbuf_dataptr();
  type = ((char*)received)[0];
  printf("Type received : %d\n", type);
  if(type == 1)
  {
    process_DAO((struct DAO*)received, from);
    // forward the DAO to the parent as well
    if(!rimeaddr_cmp(&rimeaddr_null, &parent))
    {
      printf("Forward DAO from %d.%d to %d.%d\n",
        from->u8[0],
        from->u8[1],
        parent.u8[0],
        parent.u8[1]);
      
      const rimeaddr_t dest = ((struct DAO*)received)->dest;
      send_DAO(&dest);
    }
  }
  /*
  printf("runicast message received from %d.%d : \"%s\" , seqno %d\n",
	 from->u8[0], from->u8[1], (char *)packetbuf_dataptr(), seqno);
  */

}
static void
sent_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message sent to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);
}
static void
timedout_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("runicast message timed out when sending to %d.%d, retransmissions %d\n",
	 to->u8[0], to->u8[1], retransmissions);

  // timed out is used to check if parent is still at reach 
  if(rimeaddr_cmp(to, &parent))
  {
    rimeaddr_copy(&parent, &rimeaddr_null);
  }
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
							     sent_runicast,
							     timedout_runicast};
/*---------------------------------------------------------------------------*/
/*--------------------------MAIN---------------------------------------------*/
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(simple_node_process, ev, data)
{
  // Close the connections at the end of the thread
  PROCESS_EXITHANDLER(runicast_close(&runicast);broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  // Set a null address for parent
  rimeaddr_copy(&parent, &rimeaddr_null);

  runicast_open(&runicast, 144, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  /* OPTIONAL: Sender history for runicast */
  list_init(history_table);
  memb_init(&history_mem);


  while(1) {
    static struct etimer et;

    printf("I'm %d.%d and my parent is %d.%d\n",
     rimeaddr_node_addr.u8[0],
     rimeaddr_node_addr.u8[1],
     parent.u8[0],parent.u8[1]);
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    // If in graph
    if(!rimeaddr_cmp(&parent, &rimeaddr_null))
    {
      send_DAO(&rimeaddr_node_addr);
      send_DIO();
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
