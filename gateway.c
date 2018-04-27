#include "contiki.h"
#include "net/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include <stdio.h>

#include "common.h"



/*---------------------------------------------------------------------------*/
PROCESS(gateway_node_process, "gateway node");
AUTOSTART_PROCESSES(&gateway_node_process);
/*---------------------------------------------------------------------------*/
/*--------------------------VARIABLES----------------------------------------*/
/*---------------------------------------------------------------------------*/
static uint8_t rank = 0;
static rimeaddr_t parent;
LIST(custom_route_table);
MEMB(custom_route_mem, struct custom_route_entry, MAX_ROUTE_ENTRIES);
/*---------------------------------------------------------------------------*/
/*--------------------------BROADCAST----------------------------------------*/
/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  // discard DIO messages
  void * received = (void *)packetbuf_dataptr();
  char type = ((char*)received)[0];
  printf("Type received : %d\n", type);
  if(type == 0)
  {
    process_DIO((struct DIO*)received, from);
    printf("Packet DIO discarded\n");
  }
}
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

static void send_DIO()
{
  packet_DIO.type = 0;
  packet_DIO.rank = rank;
  packet_DIO.parent = parent;
  packetbuf_copyfrom((void*)&packet_DIO, sizeof(packet_DIO));
  broadcast_send(&broadcast);
  printf("DIO sent\n");
}
static void process_DAO(struct DAO * dao, rimeaddr_t * nextNode)
{
  printf("DAO to %d.%d via %d.%d received\n",
    dao->dest.u8[0],
    dao->dest.u8[1],
    nextNode->u8[0],
    nextNode->u8[1]);
  // update route table
  struct custom_route_entry *e = NULL;
  // check if already present, if so update it
  for(e = list_head(custom_route_table); e != NULL; e = e->next) 
  {
    if(rimeaddr_cmp(&e->dest, &dao->dest)) 
    {
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

  // list all route entry
  for(e = list_head(custom_route_table); e != NULL; e = e->next) 
  {
    printf("Route entry : %d.%d via %d.%d\n", 
      e->dest.u8[0],
      e->dest.u8[1],
      e->nextNode.u8[0],
      e->nextNode.u8[1]);
  }
}
static void process_DIO(struct DIO * dio, const rimeaddr_t * from)
{
  printf("Packet DIO discarded\n");
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
  char type = ((char*)received)[0];
  printf("Type received : %d\n", type);
  if(type == 1)
  {
    process_DAO((struct DAO*)received, from);
  }
  /*
  printf("runicast message received from %d.%d : \"%s\" , seqno %d\n",
   from->u8[0], from->u8[1], received, seqno);
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
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast,
                   sent_runicast,
                   timedout_runicast};
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(gateway_node_process, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(runicast_close(&runicast);broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  parent = rimeaddr_null;

  runicast_open(&runicast, 144, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));

    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    send_DIO();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
