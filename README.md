**QMX Remote using ESP32**

A little background…I began experimenting with using a couple inexpensive ESP32
modules when they appeared on Aliexpress for less than \$5. I decided to get an
ESP32-C3 and an ESP32-S3 based small (zero sized) modules. I also had a few
ST7735 displays that were needing to be utilized, so I decided to build a small
clock that would become an IoT device. That gave me some experience with the
Arduino IDE for ESP32 and graphics on the small display. I thought it would be
fun to figure out if I could talk to my QMX+ using the AUX serial port and the
ESP32 serial port.

Hats off to Hans Summers for a great design on the QMX and his stellar
documentation for the CAT command structures and the tools to monitor the CAT
commands to the unit. That made this effort much easier.

The ESP32-S3 I used is a knock-off of the Waveshare ESP32-S3-Zero, and it has a
limited number of IO pins due to the small package size. I decided to use 2
external switches and the rotary encoder switch for function selects, and a 1.8”
120x160 ST7735 display. The basic schematic is as shown:

![](7b7e73b8cfa49b1889ab280682eb3f78.png)

The backlight is always enabled in this scheme. IO3 and IO7 could be used to
turn on and off the backlight, or to add another switch.

I did not put a battery in this set up, so it is powered from the USB-C cable
that is plugged into the ESP32-S3 board. (It draws about 30mA with the display
at full brightness).

I built the first pass on a breadboard and then put it all in a small 3D printed
enclosure with point-to-point wiring. I didn’t attempt to do a PCB for this
project.

This was my idea for the display:

VFOA TX SWR1.10 selected VFO and mode (If TX, then SWR is also shown)

14:00:00 Time

14.047.500 Freq

DIGI 10Hz Sxx mode step Smeter

T T T T T T xx:xx CW decode area and time if enabled

Here is a photo of the finished device connected to the QMX+ showing the unit
connected to the QMX+ and operating.

![](7a6264de9f843fa8ab3829e695954fb6.jpg)

**Operation summary**

The remote display will connect to the QMX+ via the AUX serial port. The serial
interface baud rate is set to 19200, with 8N1. This will need to be set up on
the QMX using a USB serial port to control the menu system. Go to Main
menu\>Configuration\>System config\>GPS & Ser. Ports: Serial 1 on AUX: ENABLED,
Serial 1 baud: 19200. (Note: I tried using higher baud settings, but that
resulted in a lot of missed data, so settled on 19200).

There are 3 controls on the display:

The rotary knob with button on press, button A (left) and button B (right).

Functions:

Rotary knob will normally adjust the frequency depending on the selected step
size. The step size is changed by a short press on the rotary knob. The step
sizes are 10Hz, 100Hz, 1kHz, 10kHz.

The band can also be changed by a double click on the rotary knob (it must be
\~300ms or less). The band will be shown in the lowest display area. The rotary
knob can be turned to select the band desired, then the knob pressed to verify
and the band will be changed to the selected. (This process needs some
refinement since it required a change to the VFOA frequency to make the change,
so previous VFOA settings are lost).

Button A is on the left and below the rotary control. It simply controls the
selected VFOA or B.

Button B is on the right and below the rotary control. It controls the selected
mode. The modes are cycled through: CW, DIGI, USB, LSB (same as QMX).

Display updates occur on a 1 second increment so there can be some visible lag
on some data presented on the display. The updates from the rotary encoder are
done during the main loop and are near real time.

**Software and IDE build**

The Arduino IDE 2.3.7 was used to build the code for this project. It will
require the board manager esp-32 by Espressif Systems be installed, and the
ST7735_LTSM library to be installed. Because of the limited pins on the
ESP32-S3-Zero package, a modification to the following file needs to be made to
provide an SPI interface on the IO pins used. This is to change from the
defaults. Here is the note in the code:

// NOTE: To map the SPI to available pins, a change to the file in this location
is necessary:

//
C:\\Users\\xxxxx\\AppData\\Local\\Arduino15\\packages\\esp32\\hardware\\esp32\\3.x.x\\variants\\waveshare_esp32_s3_zero\\pins_arduino.h:

// Mapping based on the ESP32S3 data sheet - alternate for SPI2

// static const uint8_t SS = 10; // FSPICS0

// static const uint8_t MOSI = 11; // FSPID

// static const uint8_t MISO = 13; // FSPIQ

// static const uint8_t SCK = 12; // FSPICLK

This gives the display a fast update capability and works well.

The chosen frequencies for the band-switch are as follows:

160M 1.9MHz

80M 3.6MHz

60M 5.3585MHz

40M 7.1MHz

30M 10.125MHz

20M 14.150MHz

17M 18.110MHz

15M 21.2MHz

12M 24.930MHz

10M 28.3MHz

6M 50.1MHz

These can be changed in the code if needed. As built, they are constants.

**Caveats and improvements**

The code for this was built without the use of any extensive buffer management,
and with easily accessible libraries within the Arduino IDE. That said, there is
a slight lag time during the 1 second display refresh on reading the rotary
encoder, so it may feel like it misses once in a while.

Band switching using the double-click on the encoder is not fully consistent,
but it quite functional. Just takes some getting used to. A third control switch
could be added to make this easier, but I had already built the enclosure by the
time I decided to add band switching.

The ESP32-S3 has built in Wifi, so it would be possible to create a server page
that could also show the same information as the display, but that is beyond
what I intended for this effort. Also, the S3 does not support a Bluetooth SPP
stack, so it can’t be fully wireless to the QMX with simple Bluetooth serial
modules.
