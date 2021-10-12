/*
   COMMUNITY SENSOR LAB - AIR QUALITY SENSOR

   featherM0-Wifi + featherwing adalogger-SD-RTC + SCD30-CO2 + BME280-TPRH + OLED display + SPS30-PM2.5

   The SCD30 has a minimum power consumption of 5mA and cannot be stop-started. It's set to 55s (30s nominal)
   sampling period and the featherM0 sleeps for 2 x 16 =32s, wakes and waits for data available.
   Button A toggles display on/off but must be held down for 16s max and then wait 16s to toggle again.

   Logs: date time, co2, t, rh, t2, press, rh2, battery voltage, status

   https://github.com/Community-Sensor-Lab/Air-Quality-Sensor

   Global status is in uint8_t stat in bit order:
   0- SD card not present
   1- SD could not create file
   2- RTC failed
   3- 

   4- SCD30 CO2 sensor timeout
   5- SPS30 PM2.5 sensor malfunction
   
   RICARDO TOLEDO-CROW NGENS, ESI, ASRC, CUNY,
   AMALIA TORRES, CUNY, July 2021

*/
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SleepyDog.h>
#include "RTClib.h"
#include "SparkFun_SCD30_Arduino_Library.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>  // oled library
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi101.h>
#include "sps30.h" // this is Paul van Haastrecht library, not Sensirion's https://github.com/paulvha/sps30.git

#include "arduino_secrets.h" // wifi name and password in .h file. see tab

#define VBATPIN A7 // this is also D9 button A disable pullup to read analog
#define BUTTON_A  9  // Oled button also A7 enable pullup to read button
#define BUTTON_B  6  // oled button
#define BUTTON_C  5  // oled button

char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
String POSTCommand = String("POST /macros/s/") + String(GSSD_ID) + String("/exec?value=Hello HTTP/1.1");      // Google Sheets Script Deployment ID
int status = WL_IDLE_STATUS;
char server[] = "script.google.com"; // name address for Google scripts as we are communicationg with the scripg (using DNS)
WiFiSSLClient client; // make SSL client
// these are the commands to be sent to the google script: namely add a row to last in Sheet1 with the values TBD
String payload_base =  "{\"command\":\"appendRow\",\"sheet_name\":\"Sheet1\",\"values\":";
String payload = "";
char header[] = "DateTime, CO2, Tco2, RHco2, Tbme, Pbme, RHbme, vbat(mV), status, mP1.0, mP2.5, mP4.0, mP10, ncP0.5, ncP1.0, ncP2.5, ncP4.0, ncP10, avgPartSize";
RTC_PCF8523 rtc; // Real Time Clock for RevB Adafruit logger shield
Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
Adafruit_BME280 bme; // the bme tprh sensor
File logfile;  // the logging file
SCD30 airSensor; // sensirion scd30 ndir
SPS30 sps30; // SPS30 PM sensor
const int SD_CS = 10; // Chip select for SD card default for Adalogger

uint8_t stat = 0; // status byte

float pm25 = 0; //pm2.5 value

void setup(void) {

  pinMode(VBATPIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(9600);
  delay(5000); Serial.println(__FILE__);

  initializeOLED();
  initializeSPS30(); // the PM sensor
  initializeSCD30(25); // this sets CO2 sensor to 30s more stable (1 min max recommended)
  initializeBME();
  logfile = initializeSD(SD_CS);
  initializeWiFi();
}

char outstr[100];
int32_t Tsleep = 0;
bool displayState = true;
bool buttonAstate = true;
int lastTimeToggle = 0;
int timeDebounce = 100;

void loop(void)  {

  uint8_t ctr = 0;
  stat = stat & 0xEF; // clear bit 4 for CO2 sensor

  float Tbme = bme.readTemperature();
  float Pbme = bme.readPressure() / 100; // for hPa
  float RHbme = bme.readHumidity();

  airSensor.setAmbientPressure(Pbme); // update CO2 sensor to current pressure
  // wait for data avail on CO2 sensor
  while (!airSensor.dataAvailable()) {
    delay(3000);
    if (ctr > 61) {  // timeout is 61s
      stat = stat | 0x10; // set bit 4 timeout
      break;
    }
    else
      ctr += 1;
  }

  // read from co2 sensor
  uint16_t CO2 = airSensor.getCO2();
  float Tco2 = airSensor.getTemperature();
  float RHco2 = airSensor.getHumidity();

  DateTime now;
  now = rtc.now(); // fetch the date + time

  pinMode(VBATPIN, INPUT); // read battery voltage
  float measuredvbat = analogRead(VBATPIN) * 0.006445;
  pinMode(BUTTON_A, INPUT_PULLUP);

  // read from the PM sensor
  delay(10000); // wait for the sps30 to stabilize
  String pmString = read_sps30();
  
  //  sprintf(outstr, "%02u/%02u/%02u %02u:%02u:%02u, %.2d, %.2f, %.2f, %.2f, %.2f, %.2f, %s, %.2f, %x, ",
  //          now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
  //          CO2, Tco2, RHco2, Tbme, Pbme, RHbme, pmChar, measuredvbat, stat);
  sprintf(outstr, "%02u/%02u/%02u %02u:%02u:%02u, %.2d, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %x, ",
          now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(),
          CO2, Tco2, RHco2, Tbme, Pbme, RHbme, measuredvbat, stat);

  payloadUpload(String(outstr) + pmString);

  Serial.println(header);
  Serial.print(outstr);
  logfile.print(outstr);
  pmString.toCharArray(outstr, pmString.length() + 1);
  Serial.println(outstr);

  logfile.println(outstr);
  logfile.flush();   // Write to disk. Uses 2048 bytes of I/O to SD card, power and takes time

  // turn off SPS30
  int ret = sps30.sleep();
  // sleep cycle
  for (int i = 1; i <= 8; i++)  {  // 124s = 8x16s sleep, only toggle display
    displayState = toggleButton(BUTTON_A, displayState, buttonAstate, lastTimeToggle, timeDebounce);
    if (displayState)  { // turn display on with data
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("CO2 ppm "); display.print(CO2);
      display.print("  V "); display.println(measuredvbat);
      display.print("T C     "); display.println(Tbme);
      display.print("P mBar  "); display.println(Pbme);
      display.print("RH%     "); display.println(RHbme);
      display.print("PM2.5   "); display.print(pm25);
      display.display();
    }
    else  {  // turn display off
      display.clearDisplay();
      display.display();
    };
    int sleepMS = Watchdog.sleep();// remove comment for 
//    delay(16000); // uncomment to debug because serial communication doesn't come back after sleeping
  }
  // turn on SPS30
  ret = sps30.wakeup();
}
