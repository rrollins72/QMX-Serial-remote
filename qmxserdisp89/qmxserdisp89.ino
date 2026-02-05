///////////////////////////////////////////////////////////////////////
//	QMX Remote display
// This sketch using an ESP32-S3 processor and ST7735 display receiving
// serial data on TX/RX UART2 and using a rotary encoder to change frequency
// of the QRP Labs QMX+ tranceiver. It was done as an experiment to see how
// well the ESP32-S3 could handle the load, and it proved that is it quite
// capable.
// A ESP32-S3 Zero platform was used. Readily available for about $5 from Aliexpres
// The display is a 1.8" 160x120 TFT LCD with a ST7735 driver. 
// The Arduino IDE was used for this development. 
//	The esp32 board manager needs to be installed. The Waveshare ESP32-S3-Zero board is selected.
//	The driver for the display is the ST7735_LTSM by Gavin Lyons. Fonts are from that installation.
//		This driver is much faster than the ucg driver, so is necessary for this implementation 
//
//Operation summary
//The remote display will connect to the QMX+ via the AUX serial port. The serial interface baud rate is set to 19200, with 8N1. 
//This will need to be set up on the QMX using a USB serial port to control the menu system.
//Go to Main menu>Configuration>System config>GPS & Ser. Ports: Serial 1 on AUX: ENABLED, Serial 1 baud: 19200.
//(Note: I tried using higher baud settings, but that resulted in a lot of missed data, so settled on 19200).
//
//There are 3 controls on the display:
//The rotary knob with button on press, button A (left) and button B (right).
//Functions:
//Rotary knob will normally adjust the frequency depending on the selected step size. 
//The step size is changed by a short press on the rotary knob. The step sizes are 10Hz, 100Hz, 1kHz, 10kHz. 
//The band can also be changed by a double click on the rotary knob (it must be ~300ms or less). 
//The band will be shown in the lowest display area. 
//The rotary knob can be turned to select the band desired, then the knob pressed to verify 
//and the band will be changed to the selected. 
//(This process needs some refinement since it required a change to the VFOA frequency to make the change, 
//so previous VFOA settings are lost).
//Button A is on the left and below the rotary control. It simply controls the selected VFOA or B. 
//Button B is on the right and below the rotary control. It controls the selected mode. 
//The modes are cycled through: CW, DIGI, USB, LSB (same as QMX).
//
//Display updates occur on a 1 second increment so there can be some visible lag on some data presented on the display. 
//The updates from the rotary encoder are done during the main loop and are near real time. 
//The current build is set up for displaying MST from the clock, but this can be turned off or adjusted
//
//////////////////////////////////////////////////////////////////////////
//
// The signals on the ST7735 and hook up to the ESP32-S3 Zero are as follows:
// ST7735   S3
//  GND     GND (pin 2)
//  VDD     5V(pin 1) The display has a 3.3V regulator on it-needs 5v.
//  SCL     SCK (GP12)
//  SDA     MOSI (GP11)
//  RST     GP9 
//  DC(RS)  GP8      
//  CS(SS)  GP10 
//  BKLT    to 3.3V on pin 1
//
/////////************** NOTE *******************************************
//  NOTE: To map the SPI to available pins, a change to the file in this location is necessary: 
//  C:\Users\xxxxx\AppData\Local\Arduino15\packages\esp32\hardware\esp32\3.x.x\variants\waveshare_esp32_s3_zero\pins_arduino.h:
//   Mapping based on the ESP32S3 data sheet - alternate for SPI2
//    static const uint8_t SS = 10;    // FSPICS0
//    static const uint8_t MOSI = 11;  // FSPID
//    static const uint8_t MISO = 13;  // FSPIQ
//    static const uint8_t SCK = 12;   // FSPICLK
//
// IF THERE IS AN UPDATE TO THE ESP32 PACKAGE THEN THIS WILL CHANGE, so need to be aware of it
//
/////////////////////////////////////////////////////////////////////////
//
/////////****************************************************************
// The serial interface will be based on the OnReceive demo structure-it is clean works well
// See QMX CAT document for information
////////////////////////////////////////
//

// libraries for display
#include "ST7789_LTSM.hpp"
// Included Fonts 
#include <fonts_LTSM/FontArialRound_LTSM.hpp>
#include <fonts_LTSM/FontRetro_LTSM.hpp>
#include <fonts_LTSM/FontGroTesk_LTSM.hpp>
#include <fonts_LTSM/FontGroTeskBig_LTSM.hpp>
#include <fonts_LTSM/FontSevenSeg_LTSM.hpp>
#include <fonts_LTSM/FontMint_LTSM.hpp>

////Display initialize
ST7789_LTSM myTFT;
bool bhardwareSPI = true; // true for hardware spi, false for software

/////// EC-11 rotary and buttons use ezButton
#include <ezButton.h>  // the library to use for SW button and other buttons
#include <Arduino.h>  // included for general serial use

///// Defines for IO pins used
#define CLK_PIN 1  // ESP32 pin gp1
#define DT_PIN 2   // ESP32 pin gp2
#define SW_PIN 4   // ESP32 pin gp4
#define BUTTON1 5  // GP5   general purpose button inputs
#define BUTTON2 6  // GP6

#define DIRECTION_CW 0   // clockwise direction
#define DIRECTION_CCW 1  // counter-clockwise direction

#define BAUD      19200 // Should be a reasonable speed for comms?
#define RXPIN     44     // GPIO 44 => RX for Serial2
#define TXPIN     43     // GPIO 43 => TX for Serial2

//////////////////// general declarations for global use and constants ///////////////////
// Rotary control globals
volatile int counter = 0;
volatile int direction = DIRECTION_CW;
volatile unsigned long last_time;  // for debouncing
volatile int statecnt = 0;
int prev_counter = 0;
// The ezButton library is helpful for button management
ezButton button(SW_PIN);	// create ezButton objects that attach to pins;
ezButton button1(BUTTON1);	//button for vfo selection
ezButton button2(BUTTON2);	//button for mode selection

// for S3 zero with RGB LED the IO is 21
#define RGB_BUILTIN 21
#define DEBUG 0

// Timer controls
unsigned long prevmils = 0;
unsigned long prevmils1 = 0;

/*************************************/
// volatile declaration will avoid any compiler optimization when reading variable values
volatile size_t received_bytes = 0;
unsigned int fifoFull = 38;   // max data to receive
char Rcvbuf1[40];	// buffer to receive data
bool Rcvbufavail = 0;
bool Serconnavail = 0;	//set when a serial connection is valid
char FreqA[12] = "00014047500";
char FreqB[12] = "00007050000";
char Frcv[2] = "0";
char Ftrx[2] = "0";
char freqbuf[15];
char freqbuf1[15];
char timedat[9] = "12:00:00";     //time data hh:mm:ss
char timedat1[3];     // for am or pm
char timehr[3];       //HR portion
int timehrdat;
int timelocalhr;
int rxtxchk = 0;   //receive/transmit state rx=0, tx=1													  
// For displaying local time offset from UTC from TM command
const int timelocal = -7;     //GMT offset for local MST
const bool TwelveHour = 1;	// For displaying 12 or 24 hour format
char smeterdat[6] = "00.1";   //smeter data in db xx.x 
char swrdat[6] = "1.00";      //swr reading x.xx:1												  
char lcddat[20] = {};     //lcd data-second line starts at 15 (16 bytes in)
//char infodat[36] = {};	  //info response 
char modedat[4] = {};     //mode info 1=LSB, 2=USB, 3=CW, 6=DIGI (FSK), 7=CWR
unsigned int modex = 2;	  // mode number as above
unsigned int frecvfo = 0; //receive vfo =0 for A, 1 for B, 3 for split
unsigned int ftrxvfo = 0; //same as receive vfo
unsigned int vfoaorb = 0;	//0=VFOA 1=VFOB
long Vfreqa = 0;
long Vfreqb = 0;
long stepset = 10;	//Step size in Hz for rotary step

int bandsel = 5;	//11 bands. 0=160m..10=6m 5=20m
bool bandselproc = 0;	//if in band select process

unsigned long btntimer1 = 0;
unsigned long btntimer2 = 0;
unsigned int btnstate = 0;

// Display constants
const char modLSB[] = "LSB ";
const char modUSB[] = "USB ";
const char modCW[] = "CW  ";
const char modCWR[] = "CWR ";
const char modDIG[] = "DIGI ";
const char vfoAstr[] = "VFOA";
const char vfoBstr[] = "VFOB";
const char vfoSplit[] = "SPLIT";
const char stepHZ[] = "Hz  ";
const char stepkHZ[]= "kHz";
const char txstr[] = "TX";
const char rxstr[] = "RX";

// band set strings
const char bandtxt[11][10] = {"160m band"," 80m band"," 60m band"," 40m band"," 30m band"," 20m band"," 17m band"," 15m band"," 12m band"," 10m band","  6m band"};
//const char band160set[] = "00001900000";	//160M	1.9MHz
//const char band80set[] = "00003600000";		//80M	3.6MHz
//const char band60set[] = "00005358500";		//60M	5.3585MHz
//const char band40set[] = "00007100000";		//40M	7.1MHz
//const char band30set[] = "00010125000";		//30M	10.125MHz
//const char band20set[] = "00014150000";		//20M	14.150MHz
//const char band17set[] = "00018110000";		//17M	18.110MHz
//const char band15set[] = "00021200000";		//15M	21.2MHz
//const char band12set[] = "00024930000";		//12M	24.930MHz
//const char band10set[] = "00028300000";		//10M	28.3MHz
//const char band6set[] = "00050100000";		//6M	50.1MHz
const char bandsets[11][12] = {"00001900000","00003600000","00005358500","00007100000","00010125000","00014150000","00018110000","00021200000","00024930000","00028300000","00050100000" };

// Command constants

const char LCcmd[] = "LC;";   //LCD command-receive 35 chars
const char FAcmd[] = "FA;";   //Freq VFOA command-receive 14 chars, send 14 chars
const char FBcmd[] = "FB;";   //Freq VFOB command-receive 14 chars, send 14 chars
const char FRcmd[] = "FR;";   //Receive VFO command-receive 3 chars, send 3 chars
const char FTcmd[] = "FT;";   //Transmit VFO command-receive 3 chars, send 3 chars
const char MDcmd[] = "MD;";   //Mode command-receive 4 chars or send 4 chars
const char SMcmd[] = "SM;";   //Smeter command-receive 6 chars
const char TMcmd[] = "TM;";   //Time command-receive 9 chars
const char TQcmd[] = "TQ;";   //Tx/Rx command-receive 3 chars
const char SWcmd[] = "SW;";   //SWR during transmit-receive 6 chars

///////////////////////////////////////////////////////////////
// This is called from the main loop to detect rotation changes
// There is sufficient main loop timing to make this work
///////////////////////////////////////////////////////////////
void rotloop () {
    if (statecnt == 0){
      if ((digitalRead(CLK_PIN) == LOW)&&(digitalRead(DT_PIN) == LOW)){
        statecnt = 1;
      }
    }
    if (statecnt == 1){
      if (digitalRead(CLK_PIN) == HIGH){
        statecnt = 2;
        last_time = millis();
      }
    }
    if (statecnt == 2){
      if ((millis() - last_time) > 2){
          if ((digitalRead(CLK_PIN) == HIGH)&&(digitalRead(DT_PIN) == HIGH)){
          // the encoder is rotating in counter-clockwise direction => decrease the counter
          counter--;
          direction = DIRECTION_CCW;
          statecnt = 0;
          }
          if ((digitalRead(CLK_PIN) == HIGH)&&(digitalRead(DT_PIN) == LOW)) {
          // the encoder is rotating in counter-clockwise direction => decrease the counter
          counter++;
          direction = DIRECTION_CW;
          statecnt = 0;
          }         
      }
    }
}

////////////// Top line of the display ///////////////////
///////// Print time if receiving data from QMX, otherwise just show no connection /////
void Topline (){
  if (Serconnavail) {
    myTFT.setFont(FontGroTesk);
    myTFT.setTextColor(myTFT.C_WHITE, myTFT.C_BLUE);
    myTFT.setCursor(0,5);
    if (vfoaorb == 0) myTFT.print(vfoAstr);	//which vfo to show
    if (vfoaorb == 1) myTFT.print(vfoBstr);
    myTFT.setCursor(120,5);
    if (rxtxchk == 0) {
      myTFT.print(rxstr);
      myTFT.setCursor(160,5);
      myTFT.print("         ");
    }    
    else if (rxtxchk == 1) {
      myTFT.print(txstr);
      myTFT.setCursor(160,5);
      myTFT.print(" SWR ");
      myTFT.print(swrdat);
    } 						
    myTFT.setFont(FontMint);
    if (!TwelveHour) {
      myTFT.setCursor(35,35);
      myTFT.print (timedat);
    }
    else {
  // This converts the hour to local time for display
        timehrdat = atoi(timehr);     //to convert to local time for display
        timelocalhr = timehrdat+timelocal;  // Local offset from UTC
        if (timelocalhr < 0) timelocalhr = timelocalhr+24;
        if (TwelveHour) {
          if (timelocalhr >= 12) {
            if (timelocalhr > 12) timelocalhr = timelocalhr-12;
          timedat1[0] = 'p';
          }
          else {
            if (timelocalhr == 0) timelocalhr = 12;	
            timedat1[0] = 'a';
          }
          timedat1[1] = 'm';
          timedat1[2] = 0;
        }
        itoa (timelocalhr,timehr,10);
        if (timelocalhr < 10) {   // can pad with a 0 or space if leading 0 not desired
          if (TwelveHour) timedat[0] = ' ';
          else timedat[0] = '0';
          timedat[1] = timehr[0];
        }
        else {
          timedat[0] = timehr[0];
          timedat[1] = timehr[1];
        }
      myTFT.setCursor(50,40);
      myTFT.print (timedat);
      myTFT.print ("  ");
      myTFT.setFont(FontGroTesk);
      myTFT.setCursor(250,40);
      myTFT.print (timedat1);
    }
  }
  else {
    myTFT.setFont(FontGroTesk);
    myTFT.setCursor(50,40);
    myTFT.print ("*******        ");
    myTFT.setCursor(50,40);
    myTFT.print ("no coms ");
  }
}

//////////// lower display area, for mode and step and Smeter //////////
void Botline (){
	myTFT.setFont(FontGroTesk);
	myTFT.setCursor(2,165);
  switch (modex){
      case 1:
          myTFT.print (modLSB);
          break;
      case 2:
          myTFT.print (modUSB);
          break;
      case 3:
          myTFT.print (modCW);
          break;
      case 6:
          myTFT.print (modDIG);
          break;
      case 7:
          myTFT.print (modCWR);
          break;
  }
  myTFT.setCursor(100,165);
  switch (stepset){
    case 1:
      myTFT.print("  ");    
      myTFT.print(stepset);
      break;
    case 10:
      myTFT.print(" ");   
      myTFT.print(stepset);
      break;
    case 100:
      myTFT.print(stepset);
      break;  
    case 1000:
      myTFT.print("  ");      
      myTFT.print(stepset/1000);
      break;
    case 10000:
      myTFT.print(" ");
      myTFT.print(stepset/1000);
      break;
    case 100000:
      myTFT.print(stepset/1000);
      break;
  }
  myTFT.setCursor(160,165);
  if (stepset < 1000) myTFT.print(stepHZ);
  else myTFT.print (stepkHZ);
  myTFT.setCursor(230,165);
  myTFT.print ("S");
  myTFT.print (smeterdat);
}

/////////////// Bottom line where QMX LCD info is displayed //////
void LCbotline() {
	myTFT.setFont(FontGroTesk);
	myTFT.setCursor(0,200);
	if (bandselproc){
		myTFT.print(bandtxt[bandsel]);
		myTFT.print("     ");
	} else  myTFT.print(lcddat);
}

/////////// When a frequency change occurs, send from here ///////
void FreqChgsend(){
  if (vfoaorb == 0) {  //VFOA
      sprintf(freqbuf,"%ld", Vfreqa);
      Serial2.write("FA");
      Serial2.write(freqbuf);
      Serial2.write(";");
      Serial2.write(FBcmd);
      delay(10);						   
  }
  if (vfoaorb == 1) { //VFOB
      sprintf(freqbuf,"%ld", Vfreqb);
      Serial2.write("FB");
      Serial2.write(freqbuf);
      Serial2.write(";");
      Serial2.write(FAcmd);
      delay(10);
  }
  Serial2.write("FR");
    if (!vfoaorb) Serial2.write("0");
    else Serial2.write("1");
    Serial2.write(";");
}

/////////// Frequency display update  ///////////
void Frequpdate(){
  long lvalue = 1;
  if (vfoaorb == 0) lvalue = Vfreqa;  //VFOA
  else if (vfoaorb == 1) lvalue = Vfreqb;  //VFOB
  sprintf(freqbuf1, "%ld", lvalue);
  if (lvalue >= 10000000){
  strncpy(freqbuf, freqbuf1, 2);
  freqbuf[2] = '.'; // Insert the character
  strcpy(freqbuf + 3, freqbuf1 + 2); // Copy elements after the insertion point
  strncpy(freqbuf1, freqbuf, 6);
  freqbuf1[6] = '.';
  strcpy( freqbuf1+7,freqbuf+6);
  }
  else if (lvalue < 10000000){
  strncpy(freqbuf, freqbuf1, 1);
  freqbuf[1] = '.'; // Insert the character
  strcpy(freqbuf + 2, freqbuf1 + 1); // Copy elements after the insertion point
  strncpy(freqbuf1, freqbuf, 6);
  freqbuf1[5] = '.';
  strcpy( freqbuf1+6,freqbuf+5);    
  }
	myTFT.setFont(FontGroTeskBig);
	myTFT.setTextColor(myTFT.C_WHITE, myTFT.C_BLUE);
  myTFT.setCursor(0,85);
  if (lvalue < 10000000) myTFT.print("0");
  myTFT.print(freqbuf1);	//which vfo to show
}

///////////////////////////////////////////////////////////////////////
// This is a UART callback function that will be activated on UART RX events
///////////////////////////////////////////////////////////////////////
void onReceiveFunction(void) {
  size_t available = Serial2.available();
  received_bytes = received_bytes + available;
//  Serial.printf("%d bytes rcvd: ", available);
  int i=0;
  while (available--) {
	Rcvbuf1[i] = (char)Serial2.read();
//    Serial.print(Rcvbuf1[i]);
    i++;
  }
  Rcvbufavail = 1;
//  Serial.println();
  ParseRcv();
}
///////////////////////////////////////////
///////// Parse the received data    //////
// Note: Since the arrays are not large, not using a for loop is much faster
////////////////////////////////////////////////////////////////////////////
void ParseRcv (){
	switch (Rcvbuf1[0])
	{
      case 'F':	//FA,FB,FR or FT strings
      	if (Rcvbuf1[1] == 'A'){
          FreqA[0] = Rcvbuf1[2];
          FreqA[1] = Rcvbuf1[3];
          FreqA[2] = Rcvbuf1[4];
          FreqA[3] = Rcvbuf1[5];
          FreqA[4] = Rcvbuf1[6];
          FreqA[5] = Rcvbuf1[7];
          FreqA[6] = Rcvbuf1[8];
          FreqA[7] = Rcvbuf1[9];
          FreqA[8] = Rcvbuf1[10];
          FreqA[9] = Rcvbuf1[11];
          FreqA[10] = Rcvbuf1[12];
          FreqA[11] = 0;
          Vfreqa = strtol(FreqA, NULL, 10);
        }
   		  else if (Rcvbuf1[1] == 'B'){
          FreqB[0] = Rcvbuf1[2];
          FreqB[1] = Rcvbuf1[3];
          FreqB[2] = Rcvbuf1[4];
          FreqB[3] = Rcvbuf1[5];
          FreqB[4] = Rcvbuf1[6];
          FreqB[5] = Rcvbuf1[7];
          FreqB[6] = Rcvbuf1[8];
          FreqB[7] = Rcvbuf1[9];
          FreqB[8] = Rcvbuf1[10];
          FreqB[9] = Rcvbuf1[11];
          FreqB[10] = Rcvbuf1[12];
          FreqB[11] = 0;
          Vfreqb = strtol(FreqB, NULL, 10);
      }
      else if (Rcvbuf1[1] == 'R'){
          Frcv[0] = Rcvbuf1[2];
          frecvfo = Rcvbuf1[2] - '0';
          vfoaorb = frecvfo;
      }    
      else if (Rcvbuf1[1] == 'T'){
          Frcv[0] = Rcvbuf1[2];
          ftrxvfo = Rcvbuf1[2] - '0';
      }          
      break;

		case 'T':	//Time response or TQ response
			if (Rcvbuf1[1] == 'M'){
					timedat[0] = Rcvbuf1[2];
          timehr[0] = Rcvbuf1[2];
					timedat[1] = Rcvbuf1[3];
          timehr[1] = Rcvbuf1[3];
          timehr[3] = 0;
					timedat[2] = ':';
					timedat[3] = Rcvbuf1[4];
					timedat[4] = Rcvbuf1[5];
					timedat[5] = ':';
					timedat[6] = Rcvbuf1[6];
					timedat[7] = Rcvbuf1[7];
					timedat[8] = 0;
      }
      else if (Rcvbuf1[1] == 'Q'){
          rxtxchk = Rcvbuf1[2] - '0';   //convert character to integer
      }	   
			break;	
		case 'L':	//LCD response-buffer up to 17 is top line plus spaces
			if (Rcvbuf1[1] == 'C'){
					lcddat[0] = Rcvbuf1[17];  // still one space..
					lcddat[1] = Rcvbuf1[18];
          lcddat[2] = Rcvbuf1[19];
					lcddat[3] = Rcvbuf1[20];
					lcddat[4] = Rcvbuf1[21];
					lcddat[5] = Rcvbuf1[22];
					lcddat[6] = Rcvbuf1[23];
					lcddat[7] = Rcvbuf1[24];
					lcddat[8] = Rcvbuf1[25];
					lcddat[9] = Rcvbuf1[26];
					lcddat[10] = Rcvbuf1[27];
					lcddat[11] = Rcvbuf1[28];
					lcddat[12] = Rcvbuf1[29];
          lcddat[13] = Rcvbuf1[30];
					lcddat[14] = Rcvbuf1[31];
					lcddat[15] = Rcvbuf1[32];
					lcddat[16] = Rcvbuf1[33];
          lcddat[17] = 0;
				}
			break;	
		case 'M':	//Mode response
			if (Rcvbuf1[1] == 'D'){
          modex = Rcvbuf1[2] - '0';   //convert character to integer
      }
			break;	
		case 'S': 	//S meter response or SWR response
			if (Rcvbuf1[1] == 'M'){
				smeterdat[0] = Rcvbuf1[2];
				smeterdat[1] = Rcvbuf1[3];
				smeterdat[2] = '.';
				smeterdat[3] = Rcvbuf1[4];
				smeterdat[4] = 0;
				}
      else if (Rcvbuf1[1] == 'W'){
        swrdat[0] = Rcvbuf1[2];
        swrdat[1] = '.';
        swrdat[2] = Rcvbuf1[3];
        swrdat[3] = Rcvbuf1[4];
        swrdat[4] = 0;
      }
			break;	
	}
}

/////////////////////////////////////////////////////////////////////////
// On start up, if there is no response to this command, keep looking for 1 second
// then go back to the main loop. Check each second for connection. 
/////////////////////////////////////////////////////////////////////////
void Chkserconn() {	// Use the get time command to check for a serial connection
	Serial2.write("TM;");		// get time data
	prevmils = millis();
  	while (!Rcvbufavail){
		unsigned long currentMillis = millis();	
		if (currentMillis - prevmils > 1000){ //wait for up to 1 sec for response
			Serconnavail = 0;
			return;
		}
	}
	Serconnavail = 1;
  Rcvbufavail = 0;
}

//////////// Use FA to do band change ////////////////
void BandChgSnd(){
  Serial2.write("FA");
  Serial2.write(bandsets[bandsel]);
  Serial2.write(";");
}

//////////////////////// Setup function-start things up /////////////

void setup() {

  Serial.begin(115200);
  // configure encoder and button pins as inputs
  pinMode(CLK_PIN, INPUT);
  pinMode(DT_PIN, INPUT);
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);


  button.setDebounceTime(20); // set debounce time milliseconds
  button1.setDebounceTime(20); // set debounce time milliseconds
  button2.setDebounceTime(20); // set debounce time milliseconds
  
  Serial2.begin(BAUD, SERIAL_8N1, RXPIN, TXPIN);  // start serial handler
  
  ////////////////////////////////////////////////////////////
  // TFT setup for 7735
  //*************** USER OPTION 1 SPI_SPEED + TYPE ***********
  int8_t DC_TFT  = 8;
  int8_t RST_TFT = 9;
	int8_t CS_TFT  = 10;  
	if (bhardwareSPI == true) { // hw spi
		uint32_t TFT_SCLK_FREQ = 20000000;  // Spi freq in Hertz
		myTFT.setupGPIO_SPI(TFT_SCLK_FREQ, RST_TFT, DC_TFT, CS_TFT); 
	}
//********************************************************
// ****** USER OPTION 2 Screen Setup ****** 
	uint8_t OFFSET_COL = 0;  // 2, These offsets can be adjusted for any issues->
	uint8_t OFFSET_ROW = 0; // 3, with screen manufacture tolerance/defects
	uint16_t TFT_WIDTH = 240;// Screen width in pixels
	uint16_t TFT_HEIGHT = 320; // Screen height in pixels
	myTFT.TFTInitScreenSize(OFFSET_COL, OFFSET_ROW , TFT_WIDTH , TFT_HEIGHT);
// ******************************************
// ******** USER OPTION 3 PCB_TYPE  **************************
  myTFT.TFTST7789Initialize();
//**********************************************************
  myTFT.TFTchangeInvertMode(1);
	myTFT.setRotation(myTFT.Degrees_270);
// use blue as background for SOLID mode
	myTFT.fillScreen(myTFT.C_BLUE);		//fill blue background
	myTFT.setFont(FontGroTesk);
	myTFT.setTextColor(myTFT.C_WHITE, myTFT.C_BLUE);
  myTFT.setCursor(0,45);
  myTFT.print("QMX Serial");
  myTFT.setCursor(15,80);
  myTFT.print("remote");  
  delay (3000);
 	myTFT.fillScreen(myTFT.C_BLUE);		//fill blue background
  neopixelWrite(RGB_BUILTIN,0,0,RGB_BRIGHTNESS); // Blue
  Topline();    // set up top line
  Botline();    // set up bottom line

  Serial2.flush();                                       // wait Serial FIFO to be empty and then spend almost no time processing it
  Serial2.setRxFIFOFull(fifoFull);                      // testing different result based on FIFO Full setup
  Serial2.onReceive(onReceiveFunction, false);  // sets a RX callback function for Serial 2 with timeout or fifofull
  delay (1000);
  neopixelWrite(RGB_BUILTIN,0,RGB_BRIGHTNESS,0); // green

  long lvalue = 1;
    lvalue = strtol(FreqA, NULL, 10);
    Vfreqa = lvalue;
    lvalue = strtol(FreqB, NULL, 10);
    Vfreqb = lvalue;
    Frequpdate(); // write out init freq disp
    prevmils = millis();
}

void loop() {
  unsigned long currentMillis = millis();	
  rotloop();
  if (prev_counter != counter) {
    if (direction == DIRECTION_CW){
      if (bandselproc){
        bandsel++;
        if (bandsel > 10) bandsel = 0;
      }
      else if (vfoaorb == 0) {
        Vfreqa = Vfreqa+stepset;
		    FreqChgsend();
        Frequpdate();
	    }
      else if (vfoaorb == 1) {
        Vfreqb = Vfreqb+stepset;
		    FreqChgsend();
        Frequpdate();
	    }
    }
    if (direction == DIRECTION_CCW){
      if (bandselproc){
        bandsel--;
        if (bandsel < 0) bandsel = 10;
      }
      else if (vfoaorb == 0) {
        Vfreqa = Vfreqa-stepset;
		    FreqChgsend();		
        Frequpdate();
	     }
      else if (vfoaorb == 1) {
        Vfreqb = Vfreqb-stepset;
		    FreqChgsend();
        Frequpdate();
	    }       
	  }
    prev_counter = counter;
  }
  
 // Check for rotary button						   
  button.loop();
  if (button.isPressed()) {
	  if (btnstate == 0){
      btntimer1 = millis();
      btnstate = 1;
    }
    else if ((btnstate == 1) && ((currentMillis - btntimer1) < 350)){
      btnstate = 2;     //double click in less than 100ms
      bandselproc = 1;  //enter band select 
    }
    else if (btnstate == 2 ){
      btnstate = 0;
      bandselproc = 0;
      BandChgSnd();
    }
  }
  if (((currentMillis - btntimer1) > 450) && (btnstate==1)){
    if (stepset == 10) stepset = 100;
    else if (stepset == 100) stepset = 1000;
    else if (stepset == 1000) stepset = 10000;
    else if (stepset == 10000) stepset = 10;
    Botline();
    btnstate = 0;
  }
  
// Check for button 1					 
  button1.loop();     // This button changes the VFO from current to next
  if (button1.isPressed()) {
    if (vfoaorb == 0) vfoaorb = 1;
    else if (vfoaorb == 1) vfoaorb = 0;
    Serial2.write("FR");
    if (!vfoaorb) Serial2.write("0");
    else Serial2.write("1");
    Serial2.write(";");
  }
  
// Check for button2  					  
  button2.loop();     // This button changes the mode of the xceiver: Rotates thru CW,DIGI,USB,LSB
  if (button2.isPressed()) {
    if (modex == 2) modex = 1;
    else if (modex == 1) modex = 3;
    else if (modex == 3) modex = 6;
    else if (modex == 6) modex = 2;
    Serial2.write("MD");
    Serial2.write(modex + '0'); // create ascii number for mode and send
    Serial2.write(";");
  }

// one second updates-general status and time
// Delays are for processing the serial data requested
// The values used let each command to process properly based on received bytes
	if (currentMillis - prevmils1 > 1000)  { //update clock each second
    Chkserconn();   //used the TM command to look for serial connection
		Topline();
    if (Serconnavail){
      Serial2.write(FAcmd);   //get VFOA 
      delay(10);
      Serial2.write(FBcmd);   //get VFOB 
      delay(10);
      Frequpdate(); // Display freq for selected VFO
      Serial2.write(MDcmd);   //mode command request
      delay(4);
      Serial2.write(TQcmd);   //get rx/tx mode 
      delay(4);
      if (rxtxchk == 1){
        Serial2.write(SWcmd);
        delay(4);
      }											   
      Serial2.write(SMcmd);   //Smeter request
      delay(8);
      Serial2.write(FRcmd);   //get selection
      delay(4); 
      Botline();
      Serial2.write(LCcmd);   //get LC data for bottom row
      delay(18);
      LCbotline();
    }
		prevmils1 = currentMillis;
	}} //Main loop end
