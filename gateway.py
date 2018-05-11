#!/usr/bin/python
import serial
import Queue
from threading import Semaphore
import thread
import sys
import time
from subprocess import call

toSend = Queue.Queue()
toPublish = Queue.Queue()
semaphore_serial = Semaphore()
semaphore_publish = Semaphore()
sensor_types = 1
periodicity = 1
host = "localhost"

def serialLoop(serialInterface):
	global toSend
	global semaphore_serial

	ser = serial.Serial(
	   port=serialInterface,\
	   baudrate=115200,\
	   parity=serial.PARITY_NONE,\
	   stopbits=serial.STOPBITS_ONE,\
	   bytesize=serial.EIGHTBITS,\
	   timeout=1)

	print("connected to: " + ser.portstr)
	while True:
		line = ser.readline();
		if line:
			print "+%s+" % line
			if line.strip()[-1] == '!':
				semaphore_publish.acquire()
				toPublish.put(line)
				print "In publish queue : %s" % line
				semaphore_publish.release()

		semaphore_serial.acquire()
		try :
			text = toSend.get(block=False)
			ser.write(text + "\n")
			print "Sent : %s" % text
		except Queue.Empty:
			pass
		semaphore_serial.release()
	ser.close()


def publish():
	global toPublish
	global semaphore_publish
	global host

	while True:
		semaphore_publish.acquire()
		line = ''
		try:	
			line = toPublish.get(block=False)
		except Queue.Empty:
			pass
		semaphore_publish.release()

		if line != '':
			sensor_data = parse_data(line)
			for key in sensor_data.keys():
				if key != 'origin':
					print "Publish %s" % key
					call(["mosquitto_pub",
					 "-m", "%s->%s" % (sensor_data['origin'] ,sensor_data[key]),
					 "-t", key,
					 "-h", host])


def parse_data(line):
	print "Line to parse : %s " % line
	data = line.split('!')
	print data
	result = {}
	result['origin'] = data[0]
	sensor_number = 1

	if sensor_types & 0b001:
		result['temperature'] = data[sensor_number]
		print "Temperature : %s" % result['temperature']
		sensor_number += 1
	if sensor_types & 0b010:
		result['battery'] = data[sensor_number]
		sensor_number += 1
	if sensor_types & 0b100:
		result['accelerometer'] = data[sensor_number]

	return result


def sendSerial(line):
	global toSend
	global semaphore_serial

	semaphore_serial.acquire()
	toSend.put(line)
	semaphore_serial.release()


def main(serialInterface) :
	thread.start_new_thread(serialLoop, (serialInterface,))
	thread.start_new_thread(publish, ())
	while True:
		l = raw_input()
		# two bytes : config_key, config_value
		sendSerial(l)



if __name__ == "__main__":

	if len(sys.argv) < 2 :
		print "Serial Interface needed"
		exit()
	if len(sys.argv > 2) :
		host = sys.argv[2]

	main(sys.argv[1])
