/*
Hardware connection:
ESP8266        MAX7219
GPIO14        CLK
GPIO15        CS
GPIO13        DIN
VCC           VCC
GND           GND
*/
//-----------------------------------------------------------------------------------------
// Private includes
//-----------------------------------------------------------------------------------------
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Time.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Max72xxPanel.h>
#include <string>
#include <SimpleTimer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <Ticker.h>
     
//-----------------------------------------------------------------------------------------
// Private defines
//-----------------------------------------------------------------------------------------
#define INFO_BUFFER_LEN           256   // Lenght of info buffer
#define TIME_BUFFER_LEN           50    // Lenght of time buffer
#define INFOMATION_BUFFER_LEN     256   // Lenght of information buffer
// Config Wifi
//#define WIFI_SSID   "Thanh Trieu Production"
//#define WIFI_PASSWD "ttp@2015"

//-----------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------
char info_buffer[INFO_BUFFER_LEN];
char time_buffer[TIME_BUFFER_LEN];
char information_buff[INFOMATION_BUFFER_LEN];
// Config WIFI
static const char ntpServerName[] = "time.nist.gov";      // NTP Server name
static const int  timeZone = 7;                           // Time zone: GMT+7
// Declare UDP Object
WiFiUDP Udp;
uint16_t localPort;                                       // local port to listen for UDP packets
// MQTT defines
//const char* mqtt_server = "lethanhtrieu.servehttp.com";
const char* mqtt_server = "iot.eclipse.org";
#define MQTT_PORT 1883
const char*  MQTT_USERNAME = "MQTT_Rynan";
const char* MQTT_PASSWORD = "Rynan@2021";
int mqtt_connect_counter;
WiFiClient espClient;
Ticker ticker; 
PubSubClient client(espClient);
bool have_mqtt_message_flag = false;
// Timer objects
int display_timer_id;
SimpleTimer info_timer,display_timer;
tmElements_t tm,last_min;
int scan_requence = 20;
int INFO_COLOR = 1;                                       // Data is light
int BACKGROUND_COLOR = 0;                                 // Background data is off
char *dayOfWeek;
// Time & info buffer
char date_buffer[10];
char ntp_buffer[20];                                      // 17:44:17 21/10/2016
// MAX7219 font defines
int spacer = 1;
int width = 5 + spacer;                                   // The font width is 5 pixels
int sec, minutes, hours;                          
int pinCS = 15;                                           // Attach CS to this pin, DIN to MOSI and CLK to SCK (cf http://arduino.cc/en/Reference/SPI )
int number_display_column = 8;                            // Number of modules matrix
int number_display_rows   = 1;                            // Number of matrix rows 
Max72xxPanel matrix = Max72xxPanel(pinCS, number_display_column, number_display_rows);
int matrix_len;// = width * INFO_LEN + matrix.width() - 1 - spacer;  // Compute matrix length
int i,j,infomation_len,info_timer_id;
int info_len;
bool display_info_flag = false;
bool display_time_flag = false;
bool got_ntp_time_ok = false;
bool matrix_busy = false;
//---------------------------------------------------------------------
// callback fuction
//---------------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) 
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
   char* setting_topic = "ESP0612_Setting";
  char* info_topic = "ESP0612_Info";
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if(strcmp(topic,info_topic) == 0)
  {
  memset(information_buff,0,INFOMATION_BUFFER_LEN);
    memcpy(information_buff,payload,length);
  Serial.printf("Info: %s",information_buff);
    have_mqtt_message_flag = true;
    display_time_flag = !display_time_flag;
    i = 0;  
  }
  /*
  else if(strcmp(topic,setting_topic) == 0)
  {
  scan_requence = atoi((char*)&payload[0]);
  Serial.printf("scan_requence: %d",scan_requence);
  if(scan_requence > 0)
  display_timer.setInterval(scan_requence, TimerISR);
  display_timer.restartTimer(display_timer_id);
  }*/
}


#define LED_PIN 13
#define NO_NETWORK_LED_PIN 16
#define MODE_BUTTON  4   // choose the input pin (for a pushbutton)
#define OUTPUT_HIGH 5
void tick()
{
  //toggle state
  int state = digitalRead(NO_NETWORK_LED_PIN);  // get the current state of GPIO1 pin
  digitalWrite(NO_NETWORK_LED_PIN, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

//---------------------------------------------------------------------
// MQTT reconnect
//---------------------------------------------------------------------
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
	 if(++mqtt_connect_counter > 2)
	 {
		 mqtt_connect_counter = 0;
		 ESP.reset();
		 delay(1000);
	 }		 
    Serial.print("MQTT connecting to broker...");
    // Create a random client ID
    String clientId = "SmartClockVersion1.0";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str()),MQTT_USERNAME,MQTT_PASSWORD) 
    	{
		Serial.print("Code, rc=");
      Serial.print(client.state());
		Serial.println("connected to Broker");
      // Once connected, publish an announcement...
		client.publish("SmartClock", "Smart Clock is connected");
      // ... and resubscribe
		client.subscribe("ESP0612_Setting");
		client.subscribe("ESP0612_Info");
		;
    } 
  else 
  {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
//---------------------------------------------------------------------
// Clock tick function will be called every 1 sec
//---------------------------------------------------------------------
void showInfo() 
{
  int len_of_infomation = width * infomation_len + matrix.width() - 1 - spacer; 
  j++;
  if(j > len_of_infomation)
  {
    j = 0;
    matrix_busy = false;
  }
  matrix.fillScreen(LOW);
  int letter = j / width;
  int x = (matrix.width() - 1) - j % width;
  int y = (matrix.height() - 8) / 2; // center the text vertically
  while( x + width - spacer >= 0 && letter >= 0 ) 
  {
    if( letter < len_of_infomation) 
    {
      matrix.drawChar(x, y, information_buff[letter], HIGH, LOW, 1);
    }
    letter--;
    x -= width;
  }
  matrix.write(); // Send bitmap to display
}
//---------------------------------------------------------------------
// Update time into buffer
//---------------------------------------------------------------------
void updateTimeBuff(void)
{
  if(tm.Hour >= 22)
  {
    info_len = snprintf(info_buffer,INFO_BUFFER_LEN,"Current time: %s %02d/%02d/%04d %02d:%02d:%02d Good night ! Have a nice dream !",dayOfWeek, tm.Day, tm.Month,tm.Year + 1970,tm.Hour, tm.Minute, tm.Second);
  }
  else
  {
    info_len = snprintf(info_buffer,INFO_BUFFER_LEN,"Current time: %s %02d/%02d/%04d %02d:%02d:%02d Have a nice day !",dayOfWeek, tm.Day, tm.Month,tm.Year + 1970,tm.Hour, tm.Minute, tm.Second);
  }
  if(have_mqtt_message_flag)
  {
  info_len = snprintf(info_buffer,INFO_BUFFER_LEN,"Current time: %s %02d/%02d/%04d %02d:%02d:%02d Message: %s",dayOfWeek, tm.Day, tm.Month,tm.Year + 1970,tm.Hour, tm.Minute, tm.Second,information_buff);  
  }
  matrix_len = width * info_len + matrix.width() - 1 - spacer;  // Compute matrix length
  int time_len = snprintf(time_buffer,INFO_BUFFER_LEN,"%02d:%02d:%02d %s", tm.Hour, tm.Minute, tm.Second,dayOfWeek);
  //Serial.printf("Current min: %02d last min: %02d Display flag %d\r\n",tm.Minute,last_min.Minute,display_time_flag); 
  if(tm.Minute != last_min.Minute)  // Change display mode every 1min
  {
    last_min.Minute = tm.Minute;
    display_time_flag = !display_time_flag;
    i = 0;
  }
  if(display_time_flag == true) 
  {
    matrix.fillScreen(LOW);
    matrix.drawChar(0, 0,  time_buffer[0], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(6, 0,  time_buffer[1], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(11, 0, time_buffer[2], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(16, 0, time_buffer[3], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(22, 0, time_buffer[4], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(27, 0, time_buffer[5], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(32, 0, time_buffer[6], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(38, 0, time_buffer[7], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(43, 0, time_buffer[8], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(48, 0, time_buffer[9], INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(53, 0, time_buffer[10],INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.drawChar(58, 0, time_buffer[11],INFO_COLOR, BACKGROUND_COLOR, 1);
    matrix.write(); // Send bitmap to display         
  }
}
//---------------------------------------------------------------------
// Common Setup
//---------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(MODE_BUTTON, INPUT);   // choose the input pin (for a pushbutton)
  pinMode(OUTPUT_HIGH, OUTPUT);   // choose the input pin (for a pushbutton)
  
  pinMode(NO_NETWORK_LED_PIN, OUTPUT);
  digitalWrite(OUTPUT_HIGH, LOW);   // sets the LED on
  delay(10);
  matrix.setIntensity(3);   // Use a value between 0 and 15 for brightness
  Serial.println(F("Smart Clock and Infomational display panel"));
  int cnt = 0;
  
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset settings - for testing
 //wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("Smart_Clock_Config", "123454321")) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected to AP)");
  ticker.detach();
  /*
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(F("."));
  }
  Serial.print(F("IP number assigned by DHCP is "));
  Serial.println(WiFi.localIP());
  */
  /*
  Serial.println("Starting Smart Config...");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if(cnt++ >= 10){
       WiFi.beginSmartConfig();
       while(1){
           delay(1000);
           if(WiFi.smartConfigDone()){
             Serial.println("SmartConfig Success");
             break;
           }
       }
    }
  }
  
  Serial.println("");
  Serial.println("");
  
  WiFi.printDiag(Serial);
  // Print the IP address
  */
  Serial.println(WiFi.localIP());
  client.setServer(mqtt_server, MQTT_PORT);
  client.setCallback(callback);
  matrix_busy = true;
  infomation_len = snprintf(information_buff,INFOMATION_BUFFER_LEN,"Connected to AP");
  Serial.print(F("infomation_len: "));
  Serial.println(infomation_len);
  info_timer_id = info_timer.setInterval(20, showInfo);
  Serial.print(F("IP number assigned by DHCP is "));
  Serial.println(WiFi.localIP());
  // Seed random with values unique to this device
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  uint32_t seed1 = (macAddr[5] << 24) | (macAddr[4] << 16) |(macAddr[3] << 8)  | macAddr[2];
  randomSeed(WiFi.localIP() + seed1 + micros());
  localPort = random(1024, 65535);
  Serial.println(F("Starting UDP"));
  Udp.begin(localPort);
  Serial.print(F("Local port: "));
  Serial.println(Udp.localPort());
  Serial.println(F("waiting for sync"));
  setSyncProvider(getNtpTime);
  setSyncInterval(60 * 60); // Sync every hour
}
//---------------------------------------------------------------------
// Main program
//---------------------------------------------------------------------
void loop()
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();
  info_timer.run();
  display_timer.run();
  if(got_ntp_time_ok && (matrix_busy == false))
  {
    IPAddress ip_address ;
    ip_address = WiFi.localIP();
    infomation_len = snprintf(information_buff,INFOMATION_BUFFER_LEN,"Current IP Address: %d.%d.%d.%d",ip_address[0],ip_address[1],ip_address[2],ip_address[3]);  
    got_ntp_time_ok = false;
    matrix_busy = true;
    info_timer.deleteTimer(info_timer_id);
    display_timer_id = display_timer.setInterval(scan_requence, TimerISR);
  }
  static time_t prevDisplay = 0;    // when the digital clock was displayed
  timeStatus_t ts = timeStatus();
  switch (ts) {
  case timeNeedsSync:
  case timeSet:
    if(now() != prevDisplay)        // update the display only if time has changed
    { 
      prevDisplay = now();
      //digitalClockDisplay();
      if (ts == timeNeedsSync) 
      {
        Serial.println(F("time needs sync"));
    setSyncProvider(getNtpTime);
      }
    }
    break;
  case timeNotSet:
    Serial.println(F("Time not set"));
    now();
    ESP.reset();
    break;
  default:
    break;
  }
}
//---------------------------------------------------------------------
// Display info to LED Matrix
//---------------------------------------------------------------------
void TimerISR(void)
{
  //Serial.println(info_buffer);
  digitalClockDisplay(); 
  i++;
  if(i > matrix_len)
  {
    i = 0;
    display_time_flag = true;
  }
  if(display_time_flag == false)
  {
    matrix.fillScreen(LOW);
    int letter = i / width;
    int x = (matrix.width() - 1) - i % width;
    int y = (matrix.height() - 8) / 2; // center the text vertically
    while( x + width - spacer >= 0 && letter >= 0 ) 
    {
      if( letter < matrix_len) 
      {
        matrix.drawChar(x, y, info_buffer[letter], HIGH, LOW, 1);
      }
      letter--;
      x -= width;
    }
    matrix.write(); // Send bitmap to display
  }
} 
//---------------------------------------------------------------------
// Display time info
//---------------------------------------------------------------------
void digitalClockDisplay() 
{
  breakTime(now(), tm);
  dayOfWeek = dayShortStr(tm.Wday);
  // digital clock display of the time
  //Serial.printf("%s %02d %02d %04d %02d:%02d:%02d\r\n",dayOfWeek, tm.Day, tm.Month, tm.Year + 1970,tm.Hour, tm.Minute, tm.Second);
  updateTimeBuff();
}
//---------------------------------------------------------------------
// NTP code
//---------------------------------------------------------------------
const int NTP_PACKET_SIZE = 48;       // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];   // buffer to hold incoming & outgoing packets
time_t getNtpTime()
{
  
  IPAddress timeServerIP;             // time.nist.gov NTP server address

  while (Udp.parsePacket() > 0) ;     // discard any previously received packets
  Serial.print(F("Transmit NTP Request "));
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP);
  uint32_t beginWait = millis();
  while((millis() - beginWait) < 2000) 
  {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      got_ntp_time_ok = true;
      return secsSince1900 - 2208988800UL + (timeZone * SECS_PER_HOUR);
    }
  }
  Serial.println(F("No NTP Response :-("));
  return 0; // return 0 if unable to get the time
}
//---------------------------------------------------------------------
// send an NTP request to the time server at the given address
//---------------------------------------------------------------------
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum, or type of clock
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
//---------------------------------------------------------------------
// End of file
//---------------------------------------------------------------------
