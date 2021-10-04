// NTP clock with ESP8266 on commom chatode 7 segment led display

// used base schematic from http://www.valvewizard.co.uk/iv18clock.html 
// NTP clock, used info from // https://lastminuteengineers.com/esp8266-ntp-server-date-time-tutorial/
// NTP info: https://randomnerdtutorials.com/esp8266-nodemcu-date-time-ntp-client-server-arduino/
// changed for common cathode multiplexed led display by Nicu FLORICA (niq_ro)
// added real temperature and humidity measurements with DHT22 
// added 12-hour format - https://en.wikipedia.org/wiki/12-hour_clock
// added web control for Daylight Saving Time (or summer time) - https://github.com/tehniq3/NTP_DST_ESP8266
// added web control for TimeZome: https://nicuflorica.blogspot.com/2021/02/ceas-gps-cu-reglaj-ora-locala-4.html
// added WiFi Manager, see http://nicuflorica.blogspot.com/2019/10/configurare-usoara-conectare-la-retea.html
// display IP on led display

#include <EEPROM.h>
#include <DS3231.h>       // For the DateTime object, originally was planning to use this RTC module: https://www.arduino.cc/reference/en/libraries/ds3231/
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>  //https://github.com/tzapu/WiFiManager                                                                                                              
#include "DHTesp.h"       // https://github.com/beegee-tokyo/DHTesp

int TIMEZONE = 0; // Define your timezone to have an accurate clock (hours with respect to GMT +2)
// "PST": -7 
// "MST": -6 
// "CST": -5 
// "EST": -4 
// "GMT": 0 

int timezone0 = 16; 
int timezone1 = 0;
float diferenta[38] = {-12., -11.,-10.,-9.5,-9.,-8.,-7.,-6.,-5.,-4.,-3.5,-3.,-2.,-1.,0,
                      1.,2.,3.,3.5,4.,4.5,5.,5.5,5.75,6.,6.5,7.,8.,8.75,9.,9.5,10.,10.5,
                      11.,12.,12.75,13.,14};   // added manualy by niq_ro
#define adresa  100  // adress for store the
byte zero = 0;  // variable for control the initial read/write the eeprom memory

const long utcOffsetInSeconds =  3*3600;  // +3 hour

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// define DHT
DHTesp dht;

// Definitions using https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/ 
#define SER_DATA_PIN 13 // D7     // serial data pin for VFD-grids shift register
#define SER_LAT_PIN  15 // D8      // latch pin for VFD-grids shift register
#define SER_CLK_PIN  14 // D5      // clock pin for VFD-grids shift register

#define DHT_PIN  12 // D6 

//unsigned long DATE_DELAY = 7000;     //Delay before date is displayed (loop exectutions)
//unsigned long TEMP_DELAY = 4000;    //Delay before temperature is displayed (loop exectutions)

// DST State
byte DST = 0;
byte DST0 = 0;
String oravara = ""; //

int ora, minut, secunda, rest;
int ora0, minut0;
byte ziua, luna, an;
unsigned long ceas = 0;
unsigned long ceas0 = 0;
unsigned long tpcitire = 0;

int temperature = 0;
int humidity = 0;
byte citire = 0;
byte citire2 = 0;
byte refres = 0;

unsigned long epochTime;
byte tensHour, unitsHour, tensMin, unitsMin, tensSec, unitsSec;

String ip = "";
byte lungip = 0;
int aipi[20];

/******************************************************************************* 
* Digits are lit by shiting out one byte where each bit corresponds to a grid. 
* 1 = on; 0 = off;
* msb = leftmost digit grid;
* lsb = rightmost digit grid.
*******************************************************************************/

/******************************************************************************* 
* Segments are lit by shiting out one byte where each bit corresponds to a segment:  G-B-F-A-E-C-D-dp 
* --A--
* F   B
* --G--
* E   C
* --D----DP (decimal point)
* 
* 1 = on; 0 = off;
*******************************************************************************/
 byte sevenSeg[19] = {
  B01111110, //'0' 
  B01000100, //'1'
  B11011010, //'2'
  B11010110, //'3'
  B11100100, //'4'
  B10110110, //'5'
  B10111110, //'6'
  B01010100, //'7'
  B11111110, //'8'
  B11110110, //'9'
  B11110000, //degrees symbol  - no.10
  B00111010, //'C' -> 11
  B10001000, //'r' -> 12
  B10101100, //'h' -> 13
  B10001110, //'o' -> 14
  B00000000, //' ' -> 15 (blank)
  B11111100, //'A' -> 16
  B11111000, //'P' -> 17
  B00000001, //'.' -> 18
 };

/******************************************************************************* 
Funtion prototypes
*******************************************************************************/
void updateVFD(int pos, byte num, boolean decPoint);
void displayTemperature();
void clearVFD(void);
void displayTime();
void displayDate();

// Web Host
String header; // Variable to store the HTTP request
unsigned long currentTime = millis(); // Current time
unsigned long previousTime = 0; // Previous time
const long timeoutTime = 2000; // Define timeout time in milliseconds (example: 2000ms = 2s)

// Hour State
// * DateTime objects cannot be modified after creation, so I get the current DateTime and use it to create the default 7AM alarm *
DateTime today = DateTime(timeClient.getEpochTime());

WiFiServer server(80); // Set web server port number to 80

void setup(){
  EEPROM.begin(512);  //Initialize EEPROM  - http://www.esp8266learning.com/read-and-write-to-the-eeprom-on-the-esp8266.php
  Serial.begin(115200);   // Setup Arduino serial connection
  Serial.println(" ");
  Serial.println("-------------------");
  pinMode(SER_DATA_PIN,OUTPUT);
  pinMode(SER_CLK_PIN,OUTPUT);
  pinMode(SER_LAT_PIN,OUTPUT);
//  digitalWrite(SER_LAT_PIN, HIGH);
  Serial.println("-----clock------");

  // Autodetect is not working reliable, don't use the following line
  // dht.setup(17);
  // use this instead: 
  dht.setup(DHT_PIN, DHTesp::DHT22); // Connect DHT sensor to GPIO 17

zero = EEPROM.read(adresa - 1); // variable for write initial values in EEPROM
if (zero != 16)
{
EEPROM.write(adresa - 1, 16);  // zero
EEPROM.write(adresa, timezone0); // time zone (0...37 -> -12...+12) // https://en.wikipedia.org/wiki/Coordinated_Universal_Time
EEPROM.write(adresa + 1, 1);  // 0 => winter time, 1 => summer time
EEPROM.commit();    //Store data to EEPROM
} 

// read EEPROM memory;
timezone0 = EEPROM.read(adresa);  // timezone +12
timezone1 = timezone0;
TIMEZONE = (float)diferenta[timezone0];  // convert in hours
DST = EEPROM.read(adresa+1);
DST0 = DST;

if (DST == 0) oravara = "off"; 
else oravara = "on"; 

Serial.print("TimeZone: ");
Serial.println(TIMEZONE);
Serial.print("DST: ");
Serial.println(DST);

//WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

Serial.println("Wi-Fi: AutoConnectAP");
Serial.println("IP: 192.168.4.1     ");

/*
for (byte i=0; i<100; i++)
{  
 updateVFD(3, sevenSeg[16], false); //A
 updateVFD(2, sevenSeg[17], false); //P
 updateVFD(1, sevenSeg[15], false); // blank
 updateVFD(0, sevenSeg[15], false); // blank
 delay(1);
}
*/
Serial.println("------- ");
/*
 WiFi.mode(WIFI_STA);
  WiFi.begin (ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("WiFi connected      "); 

  // Print local IP address
  Serial.println("\nWiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  delay (500);
*/

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(180);
  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if(!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } 

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

    // Print local IP address
  Serial.println("\nWiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

ip = WiFi.localIP().toString();
lungip = ip.length();

 Serial.print(lungip);
 Serial.println("chars");

aipi[lungip];

for (byte i=0; i<lungip; i++)  
{ 
  if (ip[i] == '.')
  {
    aipi[i] = 18;
    Serial.print(".");
  }
  if (ip[i] == '0') 
  {
    aipi[i] = 0;
    Serial.print("0");
  }
  if (ip[i] == '1') 
  {
    aipi[i] = 1;
    Serial.print("1");
  }
  if (ip[i] == '2') 
  {
    aipi[i] = 2;
    Serial.print("2");
  }
  if (ip[i] == '3') 
  {
    aipi[i] = 3;
    Serial.print("3");
  }
  if (ip[i] == '4')
  {
    aipi[i] = 4;
    Serial.print("4");
  }
  if (ip[i] == '5') 
  {
    aipi[i] = 5;
    Serial.print("5");
  }
  if (ip[i] == '6') 
  {
    aipi[i] = 6;
    Serial.print("6");
  }
  if (ip[i] == '7') 
  {
    aipi[i] = 7;
    Serial.print("7");
  }
  if (ip[i] == '8') 
  {
    aipi[i] = 8;
    Serial.print("8");
  }
  if (ip[i] == '9') 
  {
    aipi[i] = 9;
    Serial.print("9");
  }
  /*
     else 
     {
      updateVFD(j+i, sevenSeg[10], false); //point   
      Serial.print("*"); 
     }
     */
 delay(1);
}
Serial.println("-------//--------"); 


for (byte i=0; i<lungip-3; i++)  
{ 
 for (byte k=0; k<100; k++)
{  
 for (byte j=0; j<4; j++)
{
  updateVFD(3-j, sevenSeg[aipi[i+j]], false); //point
}
delay(4);
}
}

byte i = lungip-4;
 for (byte k=0; k<150; k++)
{  
 for (byte j=0; j<4; j++)
{
  updateVFD(3-j, sevenSeg[aipi[i+j]], false); //point
}
delay(4);
}

  timeClient.begin();
  timeClient.setTimeOffset(TIMEZONE*3600); // Offset time from the GMT standard
  server.begin(); // Start web server!

//timeClient.begin();

iaOra();
iaData();

humidity = dht.getHumidity();
temperature = dht.getTemperature();
}

void loop(){

  timeClient.update(); // Update the latest time from the NTP server
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    currentTime = millis();
    previousTime = currentTime;
    while (client.connected() && currentTime - previousTime <= timeoutTime) { // loop while the client is connected
      currentTime = millis();         
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          if (currentLine.length() == 0) { // If the current line is blank, you got two newline characters in a row. That's the end of the client HTTP request, so send a response:
            client.println("HTTP/1.1 200 OK"); // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            client.println("Content-type:text/html"); // and a content-type so the client knows what's coming, then a blank line:
            client.println("Connection: close");
            client.println();
            
            if (header.indexOf("GET /vara/on") >= 0) { // If the user clicked the alarm's on button
              Serial.println("Daylight saving time (DST) was activated !");
              oravara = "on";
              timeClient.setTimeOffset((TIMEZONE+1)*3600); // Offset time from the GMT standard
              client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
              iaOra();
            } 
            else if (header.indexOf("GET /vara/off") >= 0) { // If the user clicked the alarm's off button
              Serial.println("Daylight saving time (DST) was deactivated !");
              oravara = "off";
              timeClient.setTimeOffset((TIMEZONE+0)*3600); // Offset time from the GMT standard
              client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
              iaOra();
            }
              if (header.indexOf("GET /TZplus") >= 0) { // If the user clicked the TimeZone change button
              Serial.println("TimeZone was changed !");
              timezone0 = timezone0 + 1;
              if (timezone0 > 37) timezone0 = 0;
              TIMEZONE = (float)diferenta[timezone0];  // convert in hours
              timeClient.setTimeOffset((TIMEZONE+DST)*3600); // Offset time from the GMT standard
            client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
            } 

            if (header.indexOf("GET /TZminus") >= 0) { // If the user clicked the TimeZone change button
              Serial.println("TimeZone was changed !");
              timezone0 = timezone0 - 1;
              if (timezone0 < 0) timezone0 = 37;
              TIMEZONE = (float)diferenta[timezone0];  // convert in hours
              timeClient.setTimeOffset((TIMEZONE+DST)*3600); // Offset time from the GMT standard
              client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
            }

              if (header.indexOf("GET /Refreshinfo") >= 0) { // If the user clicked the "Refresh Info" button
              Serial.println("Clock was updated !");
            //  client.println("<meta http-equiv='Refresh' content=0; url=//" + WiFi.localIP().toString()+ ":80/>");
            }
            
            else if (header.indexOf("GET /time") >= 0) { // If the user submitted the time input form
              // Strip the data from the GET request
              int index = header.indexOf("GET /time");
              String timeData = header.substring(index + 15, index + 22);
             
             
              Serial.println(timeData);
              // Update our alarm DateTime with the user selected time, using the current date.
              // Since we just compare the hours and minutes on each loop, I do not think the day or month matters.
              DateTime temp = DateTime(timeClient.getEpochTime()); 
         //     client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
            }
            
            
            // Display the HTML web page
            // Head
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=0\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            client.println("<link rel=\"stylesheet\" href=\"//stackpath.bootstrapcdn.com/bootstrap/4.4.1/css/bootstrap.min.css\">"); // Bootstrap
          //  client.println("<meta http-equiv='Refresh' content=0;url=//" + WiFi.localIP().toString()+ ":80/>");
            client.println("</head>");
            
            // Body
            client.println("<body>");
            client.println("<h1 class=\"text-center mt-3\"NTP / DSP Clock</h1>"); // Title

            // Current Time
            client.print("<h1 class=\"text-center\">"); 
            client.print(timeClient.getFormattedTime());
            client.println("</h1>");
            client.print("<h4 class=\"text-center\">"); 
            client.print("(last update for clock)"); 
            client.println("<a href=\"/Refreshinfo\"><button class=\"btn btn-sm btn-warning\">Refresh info (clock)</button></a></p>");
            client.println("</h4>");
            client.println("</br>");
            client.println("</br>");
            
            // Display current state, and ON/OFF buttons for DST 
            client.println("<h2 class=\"text-center\">Daylight Saving Time - " + oravara + "</h2>");
            if (oravara=="off") {
              client.println("<p class=\"text-center\"><a href=\"/vara/on\"><button class=\"btn btn-sm btn-danger\">ON</button></a></p>");
              DST = 0;
            }
            else {
              client.println("<p class=\"text-center\"><a href=\"/vara/off\"><button class=\"btn btn-success btn-sm\">OFF</button></a></p>");
              DST = 1;
            }

            // Display TimeZone state, and button for increase TimeZone 
            client.println("<h2 class=\"text-center\">TimeZone = " + String(TIMEZONE) + "</h2>");
            client.println("<p class=\"text-center\"><a href=\"/TZminus\"><button class=\"btn btn-sm btn-info\">decrease TimeZone (-)</button></a>");
            client.println("<a href=\"/TZplus\"><button class=\"btn btn-sm btn-warning\">increase TimeZone (+)</button></a></p>");

            // DHT sensor info
            client.println("<h2 class=\"text-center\">DHT22 sensor: ");
            client.println(temperature);
            client.println("<sup>0</sup>C / ");
            client.println(humidity);
            client.println("%RH</h2>");      
                 
            client.println("</body></html>");
            client.println(); // The HTTP response ends with another blank line
            break; // Break out of the while loop
            
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    
    header = ""; // Clear the header variable
    client.stop(); // Close the connection
    Serial.println("Client disconnected.");
    Serial.println("");
  }

if(DST == 1)
timeClient.setTimeOffset((TIMEZONE+1)*3600); // Offset time from the GMT standard
else
timeClient.setTimeOffset(TIMEZONE*3600); // Offset time from the GMT standard

if (millis() - tpcitire > 1000)
{
iaOra();
tpcitire = millis();
}

if ((ora == 0) and (minut == 0))
{
if (secunda == 0)
{
iaData();
}
}

 if (secunda <= 45)
 {
  if (minut % 2 == 0) displayTime();
     else displayTime12();
  citire = 0;
  citire2 = 0;
 }
 else
 if ((secunda > 45) and (secunda <= 50))
 {
  if (minut % 2 == 0)
 {
  if (citire == 0)
  {
//    temperature = random(22,30);           //Scale to produce a three digit number equal to temperature
  temperature = dht.getTemperature();
    citire = 1;
  }
  displayTemperature();
 }
 else
 {
 displayDate();
 }
 }
 else
 if ((secunda > 50) and (secunda <= 53))
 {
  if (minut % 2 != 0)
 {
 displayYear();
 }
 else
 {
   if (citire2 == 0)
  {
  //  humidity = random(35,41);           //Scale to produce a three digit number equal to temperature
  humidity = dht.getHumidity();
    citire2 = 1;
  }
    displayHumidity();
 }
 }
 else
 {
   if (minut % 2 == 0) displayTime();
     else displayTime12();
 }

if (DST0 != DST)
{
timeClient.setTimeOffset((TIMEZONE+DST)*3600);
EEPROM.write(adresa + 1, DST);  // 1 => summer format, 0 => winter format 
EEPROM.commit();    //Store data to EEPROM 
Serial.print("Write in EEPROM: DST = ");
Serial.println(DST);
}
DST0 = DST;
}   //End of main program loop


void updateVFD(int pos, byte num, boolean decPoint){ //This shifts 16 bits out LSB first on the rising edge of the clock, clock idles low. Leftmost byte is position 7-0, rightmost byte is 7-seg digit
    if(pos >= 0 && pos < 9){               //Only accept valid positons on the display
      digitalWrite(SER_CLK_PIN, LOW);
      digitalWrite(SER_DATA_PIN, LOW);
      digitalWrite(SER_LAT_PIN, LOW);
      num = num + decPoint;                // sets the LSB to turn on the decimal point
      word wordOut = (1 << (pos+8)) + num; // concatenate bytes into a 16-bit word to shift out
      boolean pinState;

        for (byte i=0; i<16; i++){        // once for each bit of data
          digitalWrite(SER_CLK_PIN, LOW);
          if (wordOut & (1<<i)){          // mask the data to see if the bit we want is 1 or 0
            pinState = 1;                 // set to 1 if true
          }
          else{
            pinState = 0; 
          }
        //  if (i >=  8) pinState = (pinState + 1)%2;    
          if (i >=  8) pinState = !pinState;  
          digitalWrite(SER_DATA_PIN, pinState); //Send bit to register
          digitalWrite(SER_CLK_PIN, HIGH);      //register shifts bits on rising edge of clock
          digitalWrite(SER_DATA_PIN, LOW);      //reset the data pin to prevent bleed through
        }
      
        digitalWrite(SER_CLK_PIN, LOW);
        digitalWrite(SER_LAT_PIN, HIGH); //Latch the word to light the VFD
        //delay(1); //This delay slows down the multiplexing to get get the brightest display (but too long causes flickering)
        delayMicroseconds(500);
      
      clearVFD();
    }
} 

void clearVFD(void){
    digitalWrite(SER_DATA_PIN, LOW);          //clear data and latch pins
    digitalWrite(SER_LAT_PIN, LOW);
        for (byte i=0; i<16; i++)
        {            //once for each bit of data
            digitalWrite(SER_CLK_PIN, LOW);
            digitalWrite(SER_CLK_PIN, HIGH);  //register shifts bits on rising edge of clock
        }
    digitalWrite(SER_CLK_PIN, LOW);
    digitalWrite(SER_LAT_PIN, HIGH);          //Latch the word to update the VFD
}

void displayTemperature(){
  //  Serial.println(temperature);
    byte tens = temperature / 10;
    byte units = temperature % 10;
       if(tens > 0)
       {          //don't display first digit if 0, to avoid 00.0 
        updateVFD(3, sevenSeg[tens], false); //With decimal point
       }
        updateVFD(2, sevenSeg[units], false);   
        updateVFD(1, sevenSeg[10], false);  //degrees symbol
        updateVFD(0, sevenSeg[11], false);  //'C'
}

void displayTime(){
    byte tensHour = ora / 10; //Extract the individual digits
    byte unitsHour = ora % 10;
    byte tensMin = minut / 10;
    byte unitsMin = minut % 10;
    byte tensSec = secunda / 10;
    byte unitsSec = secunda % 10;

    if (tensHour == 0) updateVFD(3, sevenSeg[15], false); 
    else
    updateVFD(3, sevenSeg[tensHour], false);  
    if (millis()/500%2 == 0)
    updateVFD(2, sevenSeg[unitsHour], true);
    else
    updateVFD(2, sevenSeg[unitsHour], false);    
    updateVFD(1, sevenSeg[tensMin], false);  
    updateVFD(0, sevenSeg[unitsMin], false);
}

void displayTime12(){
    byte pm = ora/12;
 //    Serial.print("pm = ");
 //   Serial.println(pm);
    byte ora12 = 0;
    ora12 = ora%12;
    if (ora12 == 0) ora12 = 12;
    byte tensHour = ora12 / 10; //Extract the individual digits
    byte unitsHour = ora12 % 10;
    byte tensMin = minut / 10;
    byte unitsMin = minut % 10;
    byte tensSec = secunda / 10;
    byte unitsSec = secunda % 10;

    if (tensHour == 0) updateVFD(3, sevenSeg[15], false); 
    else
    updateVFD(3, sevenSeg[tensHour], false);  
    if (millis()/500%2 == 0)
    updateVFD(2, sevenSeg[unitsHour], true);
    else
    updateVFD(2, sevenSeg[unitsHour], false);    
    updateVFD(1, sevenSeg[tensMin], false);  
    if (pm == 0)
    updateVFD(0, sevenSeg[unitsMin], false);
    else
    updateVFD(0, sevenSeg[unitsMin], true);
}

void displayDate(){
    byte tensDate = ziua / 10; //Extract the individual digits
    byte unitsDate = ziua % 10;
    byte tensMon = luna / 10;
    byte unitsMon = luna % 10;
    byte tensYear = an / 10;
    byte unitsYear = an % 10;    
        updateVFD(3, sevenSeg[tensDate], false); 
        updateVFD(2, sevenSeg[unitsDate], true);   //with decimal point 
        updateVFD(1, sevenSeg[tensMon], false); 
        updateVFD(0, sevenSeg[unitsMon], true);    //with decimal point
}


void displayYear(){
    byte tensDate = ziua / 10; //Extract the individual digits
    byte unitsDate = ziua % 10;
    byte tensMon = luna / 10;
    byte unitsMon = luna % 10;
    byte tensYear = an / 10;
    byte unitsYear = an % 10;
  
        updateVFD(3, sevenSeg[2], false);  
        updateVFD(2, sevenSeg[0], false);
        updateVFD(1, sevenSeg[tensYear], false);  
        updateVFD(0, sevenSeg[unitsYear], false);
}


void displayHumidity(){
    //Serial.println(humidity);
    byte tens2 = humidity / 10;
    byte units2 = humidity % 10;

       if(tens2 > 0)
       {          //don't display first digit if 0, to avoid 00.0 
        updateVFD(3, sevenSeg[tens2], false); //With decimal point
       }
        updateVFD(2, sevenSeg[units2], false);   
   //   updateVFD(1, sevenSeg[12], false);  //'r'
   //   updateVFD(0, sevenSeg[13], false);  //'h'
        updateVFD(1, sevenSeg[10], false);  //degree symbol
        updateVFD(0, sevenSeg[14], false);  //'o'       
}

void iaData()
{
  epochTime = timeClient.getEpochTime();
  //Get a time structure
  struct tm *ptm = gmtime ((time_t *)&epochTime); 

  ziua = ptm->tm_mday;
  Serial.print("Month day: ");
  Serial.println(ziua);

  luna = ptm->tm_mon+1;
  Serial.print("Month: ");
  Serial.println(luna);


  an = ptm->tm_year+1900-2000;
  Serial.print("Year: ");
  Serial.println(an);
}

void iaOra()
{
  timeClient.update();

epochTime = timeClient.getEpochTime();
  Serial.print("Epoch Time: ");
  Serial.println(epochTime);
  
  String formattedTime = timeClient.getFormattedTime();
  Serial.print("Formatted Time: ");
  Serial.println(formattedTime);  
  
    ora = timeClient.getHours();
  //  Serial.print("ora = ");
  //  Serial.println(ora);
    minut = timeClient.getMinutes();
    secunda = timeClient.getSeconds();
}
