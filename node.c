#include <stdio.h>
#include <string.h>

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

// Sensors
#include "dev/i2cmaster.h"  // Include IC driver
#include "dev/tmp102.h"     // Include sensor driver
#include "dev/battery-sensor.h"
#include "dev/adxl345.h"

#include "leds.h"

#define MAX_RETRANSMISSIONS 10
#define SIGNAL

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
// Indicates if the node can still reach the root node
static uint8_t root_reachable = 0;
// Used to remember which data to send
static uint8_t sensor_types = 0;
static uint8_t periodicity = 0;
static uint8_t config_version = 0;

static int16_t last_temperature;
static int16_t last_battery;
static int16_t[3] last_accelerometer;

LIST(custom_route_table);
MEMB(custom_route_mem, struct custom_route_entry, MAX_ROUTE_ENTRIES);
/*---------------------------------------------------------------------------*/
/*--------------------------SENSOR DATA--------------------------------------*/
/*---------------------------------------------------------------------------*/
// math floor function
float
floor(float x)
{
  if(x >= 0.0f) {
    return (float) ((int) x);
  } else {
    return (float) ((int) x - 1);
  }
}
static int16_t get_temperature()
{
  static int16_t  tempint;
  static uint16_t tempfrac;
  static int16_t  raw;
  static uint16_t absraw;
  static int16_t  sign;
  static char     minus = ' ';

  sign = 1;
  raw = tmp102_read_temp_raw();  // Reading from the sensor
  absraw = raw;
  if (raw < 0) 
  { // Perform 2C's if sensor returned negative data
    absraw = (raw ^ 0xFFFF) + 1;
    sign = -1;
  }
  tempint  = (absraw >> 8) * sign;
  tempfrac = ((absraw>>4) % 16) * 625; // Info in 1/10000 of degree
  minus = ((tempint == 0) & (sign == -1)) ? '-'  : ' ' ;
  //printf ("Temp = %c%d.%04d\n", minus, tempint, tempfrac);
  return raw;
}
static uint16_t get_battery()
{
  static uint16_t bateria;
  static float mv;
  bateria = battery_sensor.value(0);
  mv = (bateria * 2.500 * 2) / 4096;
  //printf("Battery: %i (%ld.%03d mV)\n", bateria, (long) mv,
  //(unsigned) ((mv - floor(mv)) * 1000));
  return bateria;
}
static void get_accelerometer(int16_t* data)
{
  data[0] = accm_read_axis(X_AXIS);
  data[1] = accm_read_axis(Y_AXIS);
  data[2] = accm_read_axis(Z_AXIS);
  //printf("x: %d y: %d z: %d\n", x, y, z);
}
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

static void send_DIO_unicast(rimeaddr_t *to)
{
  packet_DIO.type = 0;
  packet_DIO.rank = rank;
  packet_DIO.parent = parent;
  packetbuf_copyfrom((void*)&packet_DIO, sizeof(packet_DIO));
  if(!runicast_is_transmitting(&runicast)) 
  {
    runicast_send(&runicast, to, MAX_RETRANSMISSIONS);
  }
}
// Indicate that a destination is reachable through this node (used her to advertise the route to the parents)
// Sent upward to the gateway
static void send_DAO(rimeaddr_t *dest, struct DAO* to_forward)
{
  packet_DAO.type = 1;
  rimeaddr_copy(&packet_DAO.dest, dest);

  if (to_forward == NULL)
  {
    rimeaddr_copy(&packet_DAO.path[0], &rimeaddr_node_addr);
    packet_DAO.hops = 0;
    // Fill array with 0
    memset(&packet_DAO.path, 0, 4*sizeof(rimeaddr_t));
  }
  else
  {
    memcpy(&packet_DAO.path, &to_forward->path, 4*sizeof(rimeaddr_t));
    rimeaddr_copy(&packet_DAO.path[to_forward->hops % 4], &rimeaddr_node_addr);
    packet_DAO.hops = to_forward->hops + 1;
  }
  
  if(!runicast_is_transmitting(&runicast)) 
  {
    packetbuf_copyfrom((void*)&packet_DAO, sizeof(packet_DAO));
    runicast_send(&runicast, &parent, MAX_RETRANSMISSIONS);
    printf("DAO sent!\n");
  }
}
// Ask for a DIO
static void send_DIS()
{
  packet_DIS.type = 2;
  packetbuf_copyfrom((void*)&packet_DIS, sizeof(packet_DIS));
  broadcast_send(&broadcast);
  printf("No parent, DIS sent\n");
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

static void send_config(uint8_t key, uint8_t value)
{
  printf("Sending config change of %d to %d \n", key, value);
  packet_config.type = key;
  packet_config.value = value;
  packet_config.version = config_version;
  packetbuf_copyfrom((void*)&packet_config, sizeof(packet_config));
  broadcast_send(&broadcast);
}

static void send_Data(int16_t * data, uint8_t to_write)
{
  packetbuf_clear();
  packet_data.type = 4;
  rimeaddr_copy(&packet_data.orig, &rimeaddr_node_addr);
  packet_data.packet_size =
   sizeof(uint8_t) + sizeof(size_t) + sizeof(rimeaddr_t) + to_write*sizeof(int16_t);
  // *2 cause to_write is a number of int16
  memcpy(&packet_data.data, data, to_write*sizeof(int16_t));

  if(!runicast_is_transmitting(&runicast)) 
  {
    packetbuf_copyfrom((void*)&packet_data, packet_data.packet_size);
    runicast_send(&runicast, &parent, 5);
    printf("Data sent via %d.%d!\n", parent.u8[0], parent.u8[1]);
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

    // Check if there is already a parent
    if(rimeaddr_cmp(&parent, &rimeaddr_null))
    {
      printf("Use first node encountered as parent \n");
    }
    #ifdef SIGNAL
    // Check if node has better signal than current parent
    else if(recv_RSS > parent_RSS)
    { 
      printf("Found closer node \n");
    }
    #else
    else if(recv_Hops  < parent_hops)
    {
      printf("Found node with less hops \n");
    }
    #endif
    else
      {return ;}

    rimeaddr_copy(&parent, addr);
    parent_RSS = recv_RSS;
    root_reachable = 1;
    printf("Change parent to %d.%d\n", 
            parent.u8[0],
            parent.u8[1]);
    // Hops way
    parent_hops = recv_Hops;
    rank = recv_Hops + 1;
    send_DAO(&rimeaddr_node_addr, NULL);
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
  //printf("RSSI of Last Packet Received is %d\n",rss);

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
  //printf("Type received : %d\n", type);
  if(type == 0)
  {
    process_DIO((struct DIO*)received, from);
  }
  else if (type == 2)
  {
    process_DIS(from);
  }
  else if (type == 5 || type == 6)
  {
    process_config(type, ((struct config*)received)->value, ((struct config*)received)->version);
  }
}

// Variables
static const struct broadcast_callbacks broadcast_call = {broadcast_recv};

static void process_DAO(struct DAO * dao, rimeaddr_t * nextNode)
{
  static struct custom_route_entry *e;
  static uint8_t i;

  printf("DAO to %d.%d via %d.%d received\n",
    dao->dest.u8[0],
    dao->dest.u8[1],
    nextNode->u8[0],
    nextNode->u8[1]);

  // Check if parent is sending a DAO
  if (rimeaddr_cmp(nextNode, &parent))
  { 
    send_DIS();
    forget_parent();
    return;
  }

  // Check for loop, if this node has already received this DAO
  for ( i = 0; i < 4; ++i)
  {
    if (rimeaddr_cmp(&dao->path[i], &rimeaddr_node_addr))
    {
      send_DIS();
      forget_parent();
      return;
    }
  }

  // update route table
  e = NULL;
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

  // forward the DAO to the parent as well (if there's a parent)
  if(!rimeaddr_cmp(&rimeaddr_null, &parent))
  {    
    static rimeaddr_t dest;
    dest = dao->dest;
    send_DAO(&dest, dao);
  }
}
static void forget_parent()
{
  root_reachable = 0;
  rimeaddr_copy(&parent, &rimeaddr_null);
  rank = 255;
}

// Check for a new parent using the received DIO
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
// Responfd by broadcasting a DIO 
static void process_DIS(const rimeaddr_t * from)
{
  if(rimeaddr_cmp(&parent, from))
  {
    printf("My parent is dead \n");
    forget_parent();
  }
  // If it's part of a network send a DIO otherwise ignore
  else if (!rimeaddr_cmp(&parent, &rimeaddr_null) && root_reachable)
  {
    send_DIO_unicast(from);
  }
}
// Process string message, get content or forward it
process_string_message(struct string_message * string_message)
{
  if(rimeaddr_cmp(&rimeaddr_node_addr, &string_message->dest))
  {
    printf("It's for me ! Message is %s\n", string_message->message);
    leds_toggle(LEDS_ALL);
  }
  else
  {
    send_string_message(string_message->message, &string_message->dest);
  }
}

static void process_sensor_data(struct sensor_data* data)
{
  packetbuf_clear();
  packetbuf_copyfrom((void*)data, data->packet_size);
  if(!runicast_is_transmitting(&runicast)) 
  {
    runicast_send(&runicast, &parent, 5);
    printf("Data sent via %d.%d!\n", parent.u8[0], parent.u8[1]);
  }
}

static void process_config(uint8_t key, uint8_t value, uint8_t version)
{
  printf("Received key : %d, value : %d, version : %d\n", key, value, version);

  // If config received is older than actual
  if (version <= config_version)
  {
    printf("Older version \n");
    return;
  }

  // Periodicity 
  if(key == 5)
  {
    // Check if change
    if (periodicity != value)
    {
      periodicity = value;
      config_version = version;
      send_config(key, value);
      printf("Changed config periodicity to %d \n", value);
    }
  }
  else if(key == 6)
  {
    if (sensor_types != value)
    {
      printf("I received sensor types : %d \n" , value);
      sensor_types = value;
      config_version = version;
      send_config(key, value);
      printf("Changed config sensor types to %d \n", value);
    }
  }
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
  //printf("Type received : %d\n", type);
  if(type == 1)
  {
    process_DAO((struct DAO*)received, from);
  }
  else if (type == 3)
  {
    process_string_message((struct string_message*)received);
  }
  else if (type == 4)
  {
    process_sensor_data((struct sensor_data*)received);
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
  if(rimeaddr_cmp(to, &parent) && retransmissions == MAX_RETRANSMISSIONS)
  {
    forget_parent(); 
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
  PROCESS_EXITHANDLER(goto exit)

  PROCESS_BEGIN();

  // Set a null address for parent
  rimeaddr_copy(&parent, &rimeaddr_null);

  runicast_open(&runicast, 144, &runicast_callbacks);
  broadcast_open(&broadcast, 129, &broadcast_call);

  /* OPTIONAL: Sender history for runicast */
  list_init(history_table);
  memb_init(&history_mem);

  // Route table
  list_init(custom_route_table);
  memb_init(&custom_route_mem);

  // Activate leds for real test
  leds_off(LEDS_ALL);

  // initialize temperature sensor 
  tmp102_init();
  // battery sensor 
  SENSORS_ACTIVATE(battery_sensor);
  // Accelerometer
  accm_init();

  while(1) 
  {
    static struct etimer DIS_et;
    static struct etimer DAO_et;
    static struct etimer status_et;

    static int16_t battery;
    static int16_t temperature;
    static int16_t accelerometer[3];

    etimer_set(&DIS_et, CLOCK_SECOND * 3 + random_rand() % (CLOCK_SECOND * 3));
    etimer_set(&DAO_et, CLOCK_SECOND * 5 + random_rand() % (CLOCK_SECOND * 5));
    etimer_set(&status_et, CLOCK_SECOND * 3 + random_rand() % (CLOCK_SECOND * 3) );

    PROCESS_WAIT_EVENT();

    if(etimer_expired(&DAO_et)) 
    {
      // If in graph
      if(!rimeaddr_cmp(&parent, &rimeaddr_null) && root_reachable)
      {
        // Advertise parent of route
        send_DAO(&rimeaddr_node_addr, NULL);
      }
      //get_battery();
      //get_accelerometer();
      etimer_reset(&DAO_et);
      printf("Apres reset");
    }
    if(etimer_expired(&DIS_et) && rimeaddr_cmp(&parent, &rimeaddr_null))
    {
        // probe for a graph
        send_DIS();
        etimer_reset(&DIS_et);
    }
    if(etimer_expired(&status_et))
    {
      printf("Timer status\n");
      if(!rimeaddr_cmp(&parent, &rimeaddr_null) && root_reachable) 
      {
        send_DIO();
        //send_config(5, periodicity);
        //send_config(6, sensor_types);

        static int16_t data[5];
        static uint8_t sensor_number;
        sensor_number = 0;
        // Get sensors value
        if (sensor_types & 0b001)
        {
          data[sensor_number] = get_temperature();
          ++sensor_number;
        }
        if (sensor_types & 0b010)
        {
          data[sensor_number] = get_battery();
          ++sensor_number;
        }
        if (sensor_types & 0b100)
        {
          get_accelerometer(&accelerometer);
          memcpy(&data[sensor_number], &accelerometer, 3*sizeof(int16_t));
          printf("Accelerometer : %d:%d:%d \n", data[sensor_number], data[sensor_number+1], data[sensor_number+2]);
          sensor_number += 3;
        }
        if(sensor_types != 0)
        {
          send_Data(&data, sensor_number);
        }
      }

      printf("I'm %d.%d and my parent is %d.%d\n",
      rimeaddr_node_addr.u8[0],
      rimeaddr_node_addr.u8[1],
      parent.u8[0],parent.u8[1]);
      etimer_reset(&status_et);
    }
    
  }

  exit :
    leds_off(LEDS_ALL);
    runicast_close(&runicast);
    broadcast_close(&broadcast);
    PROCESS_END();


}
/*---------------------------------------------------------------------------*/
