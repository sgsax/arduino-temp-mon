/*
  Web Server

 A simple web server that shows the value of the analog input pins.
 using an Arduino Wiznet Ethernet shield.

 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 * Analog inputs attached to pins A0 through A5 (optional)

 created 18 Dec 2009
 by David A. Mellis
 modified 9 Apr 2012
 by Tom Igoe
 modified 02 Sept 2015
 by Arturo Guadalupi

 */

/* The API supported by this application is as follows:
    Up to six sensors (numbered 0-5) can be attached and read from this monitoring device.
    Each of the commands below is called as a path in the browser URL following the hostname
      or IP for your device.
        / - return temperature in farenheit and celsius, as well as relative humidity for all
            sensors in comma-delimited format
        /report - return temperature in farenheit and celsius, as well as relative humidity
            for all sensors in human-readable, html-formatted text
        /sensorX/temp - return temperature of sensor X in farenheit (default)
        /sensorX/temp/f - return temperature of sensor X in farenheit
        /sensorX/temp/c - return temperature of sensor X in celsius
        /sensorX/humidity - return percent relative humidity of sensor X
*/

#include <SPI.h>
#include <Ethernet.h>
#include <DHT.h>
#include <string.h>
#include <stdlib.h>

//////////////////////////////
// constant definitions

// application constants
#define REQ_BUF_SZ 30
#define NUM_SENSORS 6
#define MAX_PARMS 5
#define RESP_BUF_SZ 10

// sensors we are going to use
#define DHTTYPE DHT22

// pins to capture data from the sensors
#define DHT1PIN 2
#define DHT2PIN 3
#define DHT3PIN 4
#define DHT4PIN 5
#define DHT5PIN 6
#define DHT6PIN 7

// array of sensor objects
DHT sensors[NUM_SENSORS] = {
  DHT(DHT1PIN, DHTTYPE),
  DHT(DHT2PIN, DHTTYPE),
  DHT(DHT3PIN, DHTTYPE),
  DHT(DHT4PIN, DHTTYPE),
  DHT(DHT5PIN, DHTTYPE),
  DHT(DHT6PIN, DHTTYPE)
};

//////////////////////////////
// Initialize ethernet shield - change these to suit your needs

// The ethernet MAC address is on a label on the ethernet shield.
// Use that MAC or your own here, it just needs to be unique on your network.
byte mac[] = { 0x90, 0xA2, 0xDA, 0x10, 0x3E, 0x40 };

IPAddress ip( 129, 130, 10, 175 ); // device IP
IPAddress ns( 129, 130, 10, 74 );  // DNS nameserver IP
IPAddress gw( 129, 130, 10, 1 );   // default gateway IP
IPAddress nm( 255, 255, 254, 0 );  // netmask

// start a server listening on port 80, the default for HTTP
EthernetServer server(80);

//////////////////////////////
// Initialize device
void setup() {
  // disable SD card
  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  int i;

  // Open serial communications and wait for port to open:
  Serial.begin(9600);

  // start the Ethernet connection and the server:
  Serial.println("Initializing interface");
  Ethernet.begin(mac, ip, ns, gw, nm);
  Serial.println("Initializing server");
  server.begin();
  Serial.print("Interface configured with IP ");
  Serial.println(Ethernet.localIP());

  // start the sensors
  for (i=0;i<6;i++){
    sensors[i].begin();
  }
}

void loop() {
  char HTTP_req[REQ_BUF_SZ] = {0};  // buffered HTTP request stored as null terminated string
  int i = 0;                        // index into HTTP_req buffer
  char label[] = "";
  String data;
  int j;
  char *parms[MAX_PARMS];
  int num_parms;
  float response;
  
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();             // read 1 byte (character) from client
        // buffer first part of HTTP request in HTTP_req array (string)
        // leave last element in array as 0 to null terminate string (REQ_BUF_SZ - 1)
        if (i < (REQ_BUF_SZ - 1)) {
          HTTP_req[i] = c;                  // save HTTP request character to buffer
          i++;
        }
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          //client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("<head>");
          client.println("<title>DUE Temperature Monitor</title>");
          client.println("</head>");
          client.println("<body>");
          if (InStr(HTTP_req, "GET / ")) {
            // no command, default is to return data in comma-delimited format
            for (j=0;j<6;j++) {
              data = format_data_short(sensors[j], j);
              client.println(data);
            }
          } else if (InStr(HTTP_req, "GET /s")) {
            // specific sensor likely requested
            num_parms = GetParms(HTTP_req, parms, MAX_PARMS);   // parse request
            response = HandleParms(parms, num_parms, sensors, NUM_SENSORS);   // format response
            client.println(response);
          } else if (InStr(HTTP_req, "GET /report ")) {
            // html-formatted report requested
            client.println("<h1>Full Sensor Report</h1>");
            for (j=0;j<6;j++) {
              sprintf(label, "DHT%d", j);
              data = format_data_long(sensors[j], label);     // format detailed response for this sensor
              client.print("<p>");
              client.print(data);
              client.println("</p>");
            }
          } 
          client.println("</body>");
          client.println("</html>");
          i = 0;
          StrClear(HTTP_req, REQ_BUF_SZ);
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
    Serial.println("client disconnected");
  }
}

int GetParms(char *req, char *parms[], int num_parms) {
  int i=0;
  int j;
  char *word;
  char *url;
  
  // process GET request and extract only the URL path
  word = strtok(req, " ");
  word = strtok(NULL, " ");
  url = word;

  // process URL and tokenize parameters
  word = strtok(url, "/");
  while (word != NULL) {
    if (i<num_parms) {
      parms[i] = word;
      i++;
      word = strtok(NULL, "/");
    } else {
      Serial.println("ERROR: parsed more than MAX_PARMS parms, bailing out");
      break;
    }
  }
  
  return i;
}

float HandleParms(char *parms[], int num_parms, DHT *sensors, int num_sensors){
  int i;
  char *s = parms[0];
  long sensor;
  char *op = parms[1];
  char *opt;
  int err = 0;
  boolean f = true;
  boolean c = false;
  float t;
  float h;
  float hi;
  float ret;
  char *resp;

  if (num_parms > 2) {
    opt = parms[2];
    if (strcmp(opt, "c") == 0) {
      f = false;
    } else if (strcmp(opt, "f") == 0) {
      f = true;   // default value, not necessary to set here
    } else {
      Serial.println("Invalid option requested, bailing out");
      err = 2;
    }
  }

  if (strcmp(s, "sensor0") == 0) {
    sensor = 0;
  } else if (strcmp(s, "sensor1") == 0) {
    sensor = 1;
  } else if (strcmp(s, "sensor2") == 0) {
    sensor = 2;
  } else if (strcmp(s, "sensor3") == 0) {
    sensor = 3;
  } else if (strcmp(s, "sensor4") == 0) {
    sensor = 4;
  } else if (strcmp(s, "sensor5") == 0) {
    sensor = 5;
  } else {
    Serial.println("Invalid sensor requested, bailing out");
    err = 1;
  }

  if (err == 0) {
    if (strcmp(op, "temp") == 0) {
      //do temp
      ret = sensors[sensor].readTemperature(f);
    } else if (strcmp(op, "humidity") == 0) {
      //do humidity
      ret = sensors[sensor].readHumidity();
    }  else {
      Serial.println("Invalid operation requested, bailing out");
      err = 3;
    }
  }

  if (err != 0) {
    ret = 0;
  }

  return ret;
}

bool InStr(char *str1, const char *str2) {
  int f = 0;
  int i = 0;
  int len;

  len = strlen(str1);
  
  if (strlen(str2) > len) {
      return 0;
  }
  while (i < len) {
      if (str1[i] == str2[f]) {
          f++;
          if (strlen(str2) == f) {
              return true;
          }
      } else {
          f = 0;
      }
      i++;
  }

  return false;
}

void StrClear(char *str, char length)
{
  for (int i = 0; i < length; i++) {
    str[i] = 0;
  }
}

String format_data_long(DHT dht, String label) {
  float f = dht.readTemperature(true);
  if (isnan(f)) { f = 0; };
  float c = dht.readTemperature();
  if (isnan(c)) { c = 0; };
  float h = dht.readHumidity();
  if (isnan(h)) { h = 0; };
  String ret = String(label + ": " + f + "&deg;F, " + c + "&deg;C, " + h + "% RH");
  return ret;
}

String format_data_short(DHT dht, int sensor) {
  float f = dht.readTemperature(true);
  if (isnan(f)) { f = 0; };
  float c = dht.readTemperature();
  if (isnan(c)) { c = 0; };
  float h = dht.readHumidity();
  if (isnan(h)) { h = 0; };
  String ret = String(String(sensor) + "," + f + "," + c + "," + h);
  return ret;
}

