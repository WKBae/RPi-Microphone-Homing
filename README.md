# RPi Microphone Homing Robot
This is the source code for a program to run on Raspberry Pi, which will be mounted on the robot made with Lego Mindstorms EV3.

## Purpose
We planned to make a pet robot as the project to submit to "Signals and Systems" subject in university. This robot will find the direction of its owner by a specific sound signal. This signal is constantly-generated sine wave of some fixed frequency, for example 15000Hz in our case and can be adjusted.

## Way to achieve goal
Direction of the sound source will be calculated by Raspberry Pi by the difference of sound level on tri-directioned microphone. Then the direcion is sent to EV3, and it will direct and move toward that direction. This behaviour is repeated for some times in a second to finally reach the owner(sound source).

We used three of ordinal USB microphones plugged onto Raspberry Pi 3.

## Sound level determination
The sound level is determined by the desired frequency band from transformed raw microphone input with FFT, using [GPU_FFT by Andrew Holme](http://www.aholme.co.uk/GPU_FFT/Main.htm). However it is somewhat noisy, so I adopted several ways to reduce noise listed below.
 * Averaging the neighbor ranges
 * Throwing out results too different compared with previous result
 * Weighted average of new values with old value applied to:
   * Frequency band calculated from each microphone
   * Final angle
 * Sound distance calculated to be too small

## Programming EV3
As the job to be done in EV3 will not be that complex, we used the programming tool provided by Lego, the block programming tool. It receives the direction and speed(not used for now) by messaging block and turns/advances toward that direction.

The messaging block part and motor control part has seperated start block so it can be run in parallel. This is to keep movement code run at fixed rate without being affected by the message sending rate of Raspberry Pi.

## Communication with Lego Mindstorms EV3
The communication between Raspberry Pi 3 and Lego Mindstorms EV3 is achieved by messaging block. Messaging block can transmit and receive messages through Bluetooth SPP profile with specific format.

SPP profile is set up on RPi 3 as [this article](https://www.raspberrypi.org/forums/viewtopic.php?p=919420), and the message format is [analyzed by gipprojects](https://gipprojects.wordpress.com/2013/11/29/using-python-and-raspberry-pi-to-communicate-with-lego-mindstorms-ev3/). We used these to send data from RPi 3 to EV3. Although gipprojects only did receiving messages from EV3, transmitting messages to EV3 can be done similar to that. (In our case, using message receive block caused error on EV3; updating the EV3 firmware solved it.)

## Installation
At this moment, there is no automated installation script and should be done manually.
 1. Clone this repository at some directory
 2. Build binary at directory FFT_Iterator and move it to /home/pi/
   1. `cd FFT\_Iterator`
   2. `make`
   3. `mv FFT\_Iterator.bin /home/pi/`
   4. `cd ../`
 3. Copy fftManager.py to /home/pi/ (`cp fftManager.py /home/pi/`)
 4. (For systemd on Raspbian Jessie) Register unit file for this script to run on startup
   1. `sudo cp fft-iterator.service /lib/systemd/system/`
   2. `sudo chown root:root /lib/systemd/system/fft-iterator.service`
   3. `sudo chmod 644 /lib/systemd/system/fft-iterator.service`
   4. `sudo systemctl daemon-reload`
   5. `sudo systemctl enable fft-iterator.service`
and then /home/pi/fftManager.py will run automatically on boot.
