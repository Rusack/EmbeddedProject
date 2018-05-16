#ifndef COMMON
#define COMMON

#define NUM_HISTORY_ENTRIES 4
#define MAX_ROUTE_ENTRIES 15

enum packet_type {
  DIO,
  DAO,
  DIS,
  DATA,
  CONFIG
};

struct sensor_data {
  // each bit will indicate which sensor data is sent
  // 1st bit : temperatue
  // 2nd bit : battery
  // 3rd bit : accelerometer
  enum packet_type type;
  size_t packet_size;
  rimeaddr_t orig;
  int16_t data[5];
};

// packet_type is used as key for config
struct config
{
  enum packet_type type;
  uint8_t value;
  // Version number of the config
  uint8_t version;
};

struct DIO {
  enum packet_type type;
  uint8_t rank;
  rimeaddr_t parent;
};

struct DAO {
  enum packet_type type;
  rimeaddr_t dest;
  uint8_t hops;
  rimeaddr_t path[4];
};

struct DIS {
  enum packet_type type;
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

// General variables
static struct broadcast_conn broadcast;
static struct runicast_conn runicast;
static struct DIO packet_DIO;
static struct DAO packet_DAO;
static struct DIS packet_DIS;
static struct sensor_data packet_data;
static struct config packet_config;

// Packet processing
static void process_DAO(struct DAO * dao, rimeaddr_t * nextNode);
static void process_DIO(struct DIO * dio, const rimeaddr_t *from);
static void process_DIS(const rimeaddr_t *from);
static void process_sensor_data(struct sensor_data* data);
static void process_config(uint8_t key, uint8_t value, uint8_t version);

// Packet sending
static void send_DAO(rimeaddr_t *dest, struct DAO* to_forward); 
static void send_DIO();
static void send_DIS();
static void send_Data(int16_t * data, uint8_t to_write);
static void send_config(uint8_t value);

#endif
