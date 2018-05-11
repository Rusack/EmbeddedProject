#!/usr/bin/python
import serial
from Queue import Queue
from threading import Semaphore
import thread
import sys
import time

toSend = Queue()
semaphore = Semaphore()

def serialLoop(serialInterface):
	global toSend
	global semaphore

	ser = serial.Serial(
	   port=serialInterface,\
	   baudrate=115200,\
	   parity=serial.PARITY_NONE,\
	   stopbits=serial.STOPBITS_ONE,\
	   bytesize=serial.EIGHTBITS,\
	   timeout=0)

	print("connected to: " + ser.portstr)
	while True:
		line = ser.readline();
		if line:
			print(line),

		semaphore.acquire()
		try :
			text = toSend.get(block=False)
			ser.write(text + "\n")
			print "Sent : %s" % text
		except:
			pass
		semaphore.release()


	ser.close()

def sendSerial(line):
	global toSend
	global semaphore

	semaphore.acquire()
	toSend.put(line)
	semaphore.release()


def main(serialInterface) :
	thread.start_new_thread(serialLoop, (serialInterface,))
	while True:
		l = raw_input()
		# two bytes : config_key, config_value
		sendSerial(l)
		pass



if __name__ == "__main__":

	if len(sys.argv) < 2 :
		print "Serial Interface needed"
		exit()

	main(sys.argv[1])
