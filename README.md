# Embedded Project

## How to
1. Use contiki instant virtual machine, start cooja simulator, create one mote type (Z1) using the file node.c as firmware and another type using gateway.c. 
2. Place the gateway mote on the grid and at least one node mote, put them in range of each other and click start.

## MQTT part
1. Install Mosquitto  MQTT broker.
2. Start broker with `mosquitto` command.
3. Subscribe to a topic using command `mosquitto_sub -h localhost -t "topic" -v`.
4. Publish messages using the commande `mosquitto_pub -m "test" -t "topic"`.

## To do
* Find a way to avoid loop in the graph.
* Keep routing table of node consistant.
* MQTT part.
* Ping system to check if a node is still in range.
* Subrscriber and publish system for node network.
* Improving perfomance somehow.
* Script to interface the gateway mote with MQTT broker (send publish message from serial to MQTT broker);
* Make node configurable to choose parent with hops or signal strength.
* Sensor data sending system.
* Refactor.
* Fix serial bug (when sending a message to the gateway, the message have to be sent twice, the first time send garbage for an unknown reason).

## Uploading to hardware
0. `sudo adduser $USER dialout` and then relog (let user access USB devices)
1. `make TARGET=z1 all` (Compile)
2. `make TARGET=z1 node.upload` and `make TARGET=z1 gateway.upload` (Upload to device, to select specific device use the argument `MOTES=/dev/ttyUSB0`)
3. If needed, burn a new MAC and node id using the command `make clean && make burn-nodeid.upload nodeid=158 nodemac=158 && make z1-reset && make login` (158 can be replace with the desired ID), then reburn the program to be used.
4. To read and write into serial `~/contiki/tools/sky/serialdump-linux -b115200 /dev/ttyUSB0`. Baud rate for Z1 devices is 115200.