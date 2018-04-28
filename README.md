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