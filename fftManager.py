#!/usr/bin/python
# -*- coding: utf-8 -*-

'''
 Copyright (c) 2017, William Bae.
 All rights reserved.
'''

import os.path
import subprocess
import time
import serial
import struct
import atexit

def exitcallback(ev3, hci, fft):
    ev3.close()
    hci.terminate()
    fft.terminate()

def main():
    try:
        hci = subprocess.Popen(("rfcomm", "watch", "hci0"))

        while True:
            print("Waiting for EV3 to connect...") #; run \"sudo rfcomm watch hci0\"")
            while not os.path.exists("/dev/rfcomm0"):
                time.sleep(0.5)
            
            ev3 = serial.Serial("/dev/rfcomm0")
            print("EV3 connection established")

            fft = subprocess.Popen(["/home/pi/FFT_Iterator.bin", "1", "2", "0", "714"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

            atexit.register(exitcallback, ev3=ev3, hci=hci, fft=fft)

            while True:
                try:
                    if fft.poll() <> None:
                        break
                    fft.stdin.write('\n')
                    fft.stdin.flush()
                    line = fft.stdout.readline()
                    angle = float(line)
                    angle = angle * -100 / 180

                    # (2) count, (2) message counter, (2) 1, (2) 0x819E > as big-endian, (1) name size, name+<null>, (2) payload size, payload
                    name = "Angle".encode('ascii')
                    body = struct.pack("<HHB", 1, 0x9E81, len(name) + 1) + name + struct.pack("<BHf", 0, 4, angle)
                    body = struct.pack("<H", len(body)) + body
                    ret = ev3.write(body)
                    if ret <> len(body):
                        break
                    
                    name = "Power".encode('ascii')
                    body = struct.pack("<HHB", 1, 0x9E81, len(name) + 1) + name + struct.pack("<BHf", 0, 4, 40.0)
                    body = struct.pack("<H", len(body)) + body
                    ret = ev3.write(body)
                    if ret <> len(body):
                        break
                    time.sleep(1)
                except:
                    break
            ev3.close()
            fft.terminate()
    except KeyboardInterrupt:
        print("Exiting...")
        pass

if __name__ == "__main__":
    main()
    
