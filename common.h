#ifndef COMMON
#define COMMON

#define MAX_RETRANSMISSIONS 5
#define NUM_HISTORY_ENTRIES 4
#define MAX_ROUTE_ENTRIES 15

// packet type (better replace it by enum later)
/*
0:DIO
1:DAO
2:DIS
3:String message
*/
// type is the first byte of packet, indicates to receiver the type of packet

struct DIO {
  char type;
  uint8_t rank;
  rimeaddr_t parent;
};

struct DAO {
  char type;
  rimeaddr_t dest;
};

struct DIS {
  char type;
};

struct string_message {
  char type;
  rimeaddr_t dest;
  char message[32];
};

// routing
struct custom_route_entry {
  struct custom_route_entry *next;
  rimeaddr_t dest;
  rimeaddr_t nextNode;
};

// Unicast 
/* OPTIONAL: Sender history.
 * Detects duplicate callbacks at receiving nodes.
 * Duplicates appear when ack messages are lost. */
struct history_entry {
  struct history_entry *next;
  rimeaddr_t addr;
  uint8_t seq;
};

static struct broadcast_conn broadcast;
static struct runicast_conn runicast;
static struct DIO packet_DIO;
static struct DAO packet_DAO;
static struct DIS packet_DIS;
static struct string_message packet_string;


static void process_DAO(struct DAO * dao, rimeaddr_t * nextNode);
static void process_DIO(struct DIO * dio, const rimeaddr_t *from);
static void process_DIS(const rimeaddr_t *from);

// to move at the end, create separate header file
static void send_DAO(rimeaddr_t *dest); 
static void send_DIO();
static void send_DIS();

static void send_string_message(char* message, rimeaddr_t *dest);

#endif
