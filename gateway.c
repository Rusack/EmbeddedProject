#include "contiki.h"
#include "net/rime.h"
#include "random.h"

#include "dev/button-sensor.h"

#include "dev/leds.h"

#include "dev/serial-line.h"

#include <stdio.h>
#include <string.h>

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
  static void* received;
  static char type; 
  received = (void *)packetbuf_dataptr();
  type = ((char*)received)[0];
  printf("Type received : %d\n", type);
  if(type == 0)
  {
    process_DIO((struct DIO*)received, from);
  }
  else if (type == 2)
  {
    process_DIS(from);
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
// Send a string message to a node (testing purpose)
static void send_string_message(char* message, rimeaddr_t *dest)
{

  // search through routing table the next node to which send the message
  static struct custom_route_entry *e;
  static rimeaddr_t nextNode;

  for(e = list_head(custom_route_table); e != NULL; e = e->next) 
  {
    // if entry found
    if(rimeaddr_cmp(&e->dest, dest)) 
    {
      // get the next node
      rimeaddr_copy(&nextNode, &e->nextNode);
      break;
    }
  }
  // If entry has been found
  if (e != NULL)
  {
    packet_string.type = 3;
    packet_string.dest = *dest;

    strncpy(packet_string.message, message, 32);
    packet_string.message[31] = '\0';

    if(!runicast_is_transmitting(&runicast)) 
    {
      packetbuf_copyfrom((void*)&packet_string, sizeof(packet_string));
      runicast_send(&runicast, &nextNode, MAX_RETRANSMISSIONS);
      printf("Message sent via %d.%d!\n", nextNode.u8[0], nextNode.u8[1]);
    }
  }
  else
  {
    printf("Can't find any route to destination ! \n");
  }
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
// Do nothing
static void process_DIO(struct DIO * dio, const rimeaddr_t * from)
{
  printf("Packet DIO discarded\n");
}
// Responfd by broadcasting a DIO 
static void process_DIS(const rimeaddr_t * from)
{
  send_DIO();
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
  static char* serial_received;
  static rimeaddr_t dest;

  PROCESS_EXITHANDLER(runicast_close(&runicast);broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  parent = rimeaddr_null;

  runicast_open(&runicast, 144, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    //etimer_set(&et, CLOCK_SECOND * 1);
    //printf("test\n");
    
    //PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    // Get serial input
    //

    PROCESS_WAIT_EVENT();

    if(ev == serial_line_event_message && data != NULL) 
    {
       PROCESS_YIELD();
       serial_received = (char *)data;
       printf("received line: %s\n", serial_received);
       dest.u8[0] = serial_received[0] - '0';
       dest.u8[1] = serial_received[1] - '0';
       printf("Sending message to %d.%d\n",dest.u8[0], dest.u8[1] );
       serial_received = serial_received + 2;
       send_string_message(serial_received, &dest);

    }
    if(etimer_expired(&et))
    {
      send_DIO();
    }

    
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
