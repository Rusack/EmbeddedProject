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
sensor_types = 0
periodicity = 0
new_sensor_types = 0
new_periodicity = 0
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

    # print("connected to: " + ser.portstr)
    while True:
        line = ser.readline();
        if line:
            #print "+%s+" % line
            if line.strip()[-1] == '!':
                semaphore_publish.acquire()
                toPublish.put(line)
                #print "In publish queue : %s" % line
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
                    # print "Publish %s" % key
                    call(["mosquitto_pub",
                     "-m", "%s->%s" % (sensor_data['origin'] ,sensor_data[key]),
                     "-t", key,
                     "-h", host])


def parse_data(line):
    #print "Line to parse : %s " % line
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

"""
def process_subscriber():
    call(["mosquitto_sub",
                     "-t", "'$SYS/broker/log/#'",
                     "-h", host])
    pass
"""

def sendSerial(line):
    global toSend
    global semaphore_serial

    semaphore_serial.acquire()
    toSend.put(line)
    semaphore_serial.release()

def send_config():
    global new_sensor_types
    global new_periodicity
    global sensor_types
    global periodicity
    if sensor_types != new_sensor_types or periodicity != new_periodicity:
        sensor_types = new_sensor_types
        periodicity = new_periodicity
        print "Config %d:%d" % (new_sensor_types, new_periodicity)
        sendSerial("%c" % (new_sensor_types | new_periodicity))

def print_menu():       ## Your menu design here
    print 30 * "-" , "MENU" , 30 * "-"
    print "1. Config"
    print "2. Exit"
    print 67 * "-"

def print_config_menu():
    global new_sensor_types
    global new_periodicity
    print 29 * "-" , "CONFIG" , 29 * "-" 
    print "Sensors type :"
    print "1. [%d] Temperature" % (new_sensor_types & 0b001 == 0b001)
    print "2. [%d] Battery" % (new_sensor_types & 0b010 == 0b010)
    print "3. [%d] Accelerometer" % (new_sensor_types & 0b100 == 0b100)
    print "Misc"
    print "4. [%d] Periodicity" % (new_periodicity & 0b1000 == 0b1000 )
    print "5. Exit and save"
    print "6. Exit and discard"

def main(serialInterface) :
    thread.start_new_thread(serialLoop, (serialInterface,))
    thread.start_new_thread(publish, ())
    loop=True 
    global sensor_types
    global periodicity       
    global new_sensor_types
    global new_periodicity

    while loop:          ## While loop which will keep going until loop = False
        print_menu()    ## Displays menu
        choice = input("Enter your choice [1-2]: ")
         
        if choice==1:     
            print "Config menu"

            new_sensor_types = sensor_types
            new_periodicity = periodicity
            print_config_menu()
            loop_config = True
            while loop_config:
                print_config_menu()
                choice = input("Enter your choice [1-6]: ")
                if choice == 1:
                    new_sensor_types ^= 0b001
                elif choice == 2:
                    new_sensor_types ^= 0b010
                elif choice == 3:
                    new_sensor_types ^= 0b100
                elif choice == 4:
                    new_periodicity ^= 0b1000
                elif choice == 5:
                    send_config()
                    loop_config = False
                elif choice == 6:
                    loop_config = False

        elif choice==2:
            print "Exiting"
            ## You can add your code or functions here
            loop=False # This will make the while loop to end as not value of loop is set to False
        else:
            # Any integer inputs other than values 1-5 we print an error message
            raw_input("Wrong option selection. Enter any key to try again..")

if __name__ == "__main__":

    if len(sys.argv) < 2 :
        print "Serial Interface needed"
        exit()
    if len(sys.argv) > 2 :
        host = sys.argv[2]

    main(sys.argv[1])
