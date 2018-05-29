#!/usr/bin/python
import serial
import Queue
from threading import Semaphore
import thread
import sys
import time
from subprocess import call
import subprocess
import re

to_send = Queue.Queue()
to_publish = Queue.Queue()
semaphore_serial = Semaphore()
semaphore_publish = Semaphore()
sensor_types = 0
periodicity = 0
new_sensor_types = 0
new_periodicity = 0
config_mask_opti = 0
host = "localhost"


def run_mosquitto_log():
    connected_clients = dict()
    total_nb_clients = 0
    global sensor_types

    global config_mask_opti

    re_connect = re.compile('mosqsub\/(\d+).+(temperature|battery|accelerometer)')
    re_disconnect = re.compile('mosqsub\/(\d+).+disconnecting')
    
    # Get total number of clients
    process = subprocess.Popen(['mosquitto_sub', '-h', host, '-t', "$SYS/broker/clients/connected"], stdout=subprocess.PIPE)
    output = process.stdout.readline()
    total_nb_clients = int(output)
    process.kill()

    if total_nb_clients > 0 :
        # can't optimize, send all data
        config_mask_opti = 0b111
    else:
        config_mask_opti = 0b000
    send_config()

    # Get mosquitto logs
    process = \
     subprocess.Popen(['mosquitto_sub', '-h', host, '-t', "$SYS/broker/log/#"], stdout=subprocess.PIPE)
    while True:
        output = process.stdout.readline()
        match_connect = re_connect.search(output)
        match_disconnect = re_disconnect.search(output)

        if match_connect:
            # If someone has connected
            id_client =  match_connect.group(1)
            topic =  match_connect.group(2)
            connected_clients[id_client] = topic
            total_nb_clients = total_nb_clients + 1
        elif match_disconnect :
            # If someone has disconnected
            id_client =  match_connect.group(1)
            try:
                connected_clients.pop(id_client)
            except KeyError:
                pass
            total_nb_clients = total_nb_clients - 1
        else :
            # ignore the line, doesn't change mask
            continue

        if total_nb_clients == len(connected_clients) :
            # Can optimize 
            if 'temperature' in connected_clients.values() :
                config_mask_opti |= 0b001
            if 'battery' in connected_clients.values() :
                config_mask_opti |= 0b010
            if 'accelerometer' in connected_clients.values() :
                config_mask_opti |= 0b100
            send_config()


def serial_loop(serial_interface):
    global to_send
    global semaphore_serial

    ser = serial.Serial(
       port=serial_interface,\
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
                to_publish.put(line)
                #print "In publish queue : %s" % line
                semaphore_publish.release()

        semaphore_serial.acquire()
        try :
            text = to_send.get(block=False)
            ser.write(text + "\n")
            print "Sent : %s" % text
        except Queue.Empty:
            pass
        semaphore_serial.release()
    ser.close()


def publish():
    global to_publish
    global semaphore_publish
    global host

    while True:
        semaphore_publish.acquire()
        line = ''
        try:    
            line = to_publish.get(block=False)
        except Queue.Empty:
            pass
        semaphore_publish.release()

        if line != '':
            sensor_data = parse_data(line)
            for key in sensor_data.keys():
                if key != 'origin':
                    # print "Publish %s" % key
                    call(["mosquitto_pub",
                     "-m", "%s->%s" % (sensor_data['origin'], sensor_data[key]),
                     "-t", key,
                     "-h", host])


def parse_data(line):
    #print "Line to parse : %s " % line
    data = line.split('!')
    #print data
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


def send_serial(line):
    global to_send
    global semaphore_serial

    semaphore_serial.acquire()
    to_send.put(line)
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
        # Apply mask to take into account, number clients connected for each topic
        send_serial("%c" % ((new_sensor_types & config_mask_opti) | new_periodicity))

def print_menu():
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

def main(serial_interface) :
    thread.start_new_thread(serial_loop, (serial_interface,))
    thread.start_new_thread(publish, ())
    thread.start_new_thread(run_mosquitto_log, ())
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
            loop=False # This will make the while loop to end as not value of loop is set to False
        else:
            raw_input("Wrong option selection. Enter any key to try again..")

if __name__ == "__main__":

    if len(sys.argv) < 2 :
        print "Serial Interface needed"
        exit()
    if len(sys.argv) > 2 :
        host = sys.argv[2]

    main(sys.argv[1])
