// ===== LIBRARIES
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>                // for OTA updates
#include <WiFiUdp.h>                    // for OTA updates
#include <ArduinoOTA.h>                 // for OTA updates

// ===== #DEFINES
// GPIO:
#define LEDPIN 13   // blue LED on GPIO13 low-side on Sonoff
#define RELAYL1PIN 12   // relay L1 (UP) on GPIO12 high-side on Sonoff
#define RELAYL2PIN 5   // relay L2 (DOWN) on GPIO5 high-side on Sonoff
#define BUTTONPIN 10   // built-in button on GPIO10 (low when pressed?) on Sonoff
#define PROJECTORSIGNAL 9    // projector on GPIO9 (labelled Button 1 on internal header)
#define BUTTON0 0  // spare button on GPIO0 (on internal header - used during reset)

// READABILITY:
#define WEBCOMMANDUP 1
#define WEBCOMMANDDOWN 2
#define WEBCOMMANDSTOP 0
#define WEBCOMMANDNOTHING 3

// ===== CONSTANTS
const char ssid[]  = WIFI_SSID; // from PLATFORMIO_BUILD_FLAGS environment variable
const char password[] = WIFI_PASS; // ---"---

const int controlRelaysInterval = 100; // Acts quickly enough for http commands, but also debounces signal.
const int OTAInterval = 500; // Handle OTA reflashing twice a second
const int webServerInterval = 100;  // Quickly respond to web page request

// ===== VARIABLES
unsigned long currentMillis = 0;    // stores the value of millis() in each iteration of loop()
unsigned long controlRelaysPreviousMillis = 0;   // will store last time sensor was read
unsigned long OTAPreviousMillis = 0;   // will store last time OTA was handled
unsigned long webServerPreviousMillis = 0;   // will store last time web page was handled

byte ledState = LOW;  // for toggling the LED
int webCommand = WEBCOMMANDNOTHING;    // command from http

WiFiServer myHTTPserver(80);
// WiFiClient thingspeakClient;

// ===== SETUP FUNCTION
void setup() {
  Serial.begin(115200);                   // Start serial coms @ 115200
  delay(10);                              // for some reason?
  Serial.println("Booting");
  Serial.println(String("Chip ID: 0x") + (ESP.getChipId(), HEX));

  pinMode(LEDPIN, OUTPUT);     // Initialize the pin as an output for flashing LED
  pinMode(RELAYL1PIN, OUTPUT);     // RelayL1 - HIGH is ON
  pinMode(RELAYL2PIN, OUTPUT);     // RelayL2 - HIGH is ON
  pinMode(BUTTONPIN, INPUT_PULLUP);     // Input - LOW is PRESSED
  pinMode(PROJECTORSIGNAL, INPUT);    // No pullup
  pinMode(BUTTON0, INPUT_PULLUP);    // Pull down during reset for programming

  digitalWrite(RELAYL1PIN, LOW);  // turn off relay for start
  digitalWrite(RELAYL2PIN, LOW);  // turn off relay for start

  WiFi.hostname("Sonoff-Projector");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  
  ArduinoOTA.setHostname("Sonoff-Projector");  // Seems to need to be the same as the WiFi hostname
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    Serial.end();  // makes OTA more reliable, apparently
    digitalWrite(RELAYL1PIN, LOW);  // turn off relays while flashing
    digitalWrite(RELAYL2PIN, LOW);
  });
  ArduinoOTA.onEnd([]() {
    Serial.begin(115200);
    Serial.println("\nOTA End");
  });
  //ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  //  Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  //});
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.begin(115200);
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  Serial.println(String("Ready to connect using IP address: ") + WiFi.localIP());
  
  ArduinoOTA.begin();  
  myHTTPserver.begin();
  
}  // End of SETUP function


// ===== MAIN LOOP
void loop() {

  currentMillis = millis();

  controlRelays();
  updateWebServer();
  serviceOTA();
  
}  // End of MAIN LOOP function

void controlRelays() {
  if (currentMillis - controlRelaysPreviousMillis >= controlRelaysInterval) {

    switch (webCommand) {
      case WEBCOMMANDUP:
        digitalWrite(RELAYL1PIN, HIGH);
        digitalWrite(RELAYL2PIN, LOW);
        break;
      case WEBCOMMANDDOWN:
        digitalWrite(RELAYL1PIN, LOW);
        digitalWrite(RELAYL2PIN, HIGH);
        break;
      case WEBCOMMANDSTOP:
        digitalWrite(RELAYL1PIN, LOW);
        digitalWrite(RELAYL2PIN, LOW);
        break;
    }

    controlRelaysPreviousMillis += controlRelaysInterval;
  }
}  // END OF CONTROLRELAYS function

void updateWebServer() {
  if (currentMillis - webServerPreviousMillis >= webServerInterval) {  // Time to do the task
    WiFiClient client = myHTTPserver.available();  // Get a web browsing client that has data available for reading.
    if (client) {                        // Yes, someone is there
      Serial.println(String("At: ") + currentMillis + " mS    Posting to browser");
                  
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close");  // the connection will be closed after completion of the response
      client.println("Refresh: 5");  // refresh the page automatically every 5 sec
      client.println(""); //  do not forget this one
      client.println("<!DOCTYPE HTML>");
      client.println("<html>");
      client.println("PlatformIO-built on 2018-02-03<br />");
      client.println(String("Free Heap RAM: ") + ESP.getFreeHeap() + "<br />");
      client.println(String("MAC address: ") + WiFi.macAddress() + "<br />");

      client.print("Projector signal is ");
      if (digitalRead(PROJECTORSIGNAL) == HIGH) {
      client.print("HIGH (Projector on)");
      } else {
      client.print("LOW (Projector off)");
      }
      client.println("<br>"); 

      client.print("Button pin is now: ");
      if (digitalRead(BUTTONPIN) == LOW) {
        client.print("Pushed");
      } else {
        client.print("Released");
      }
      client.println("<br><br>");

      client.println("<a href=\"/SCREEN=UP\"\"><button>Screen Up </button></a><br />");
      client.println("<a href=\"/SCREEN=STOP\"\"><button>Screen STOP </button></a><br />");
      client.println("<a href=\"/SCREEN=DOWN\"\"><button>Screen Down </button></a><br />");  
      client.println("</html>");

      if (client.available()) {  // Data is available to be read from the browsing client
        String request = client.readStringUntil('\r');  // Read the first line of the request
        Serial.println(request);
        client.flush();  // Discard any other queued requests?

        // Act on the request (TODO: move actions to state machine)
        webCommand = WEBCOMMANDNOTHING;   // default
        if (request.indexOf("/SCREEN=UP") != -1)  {
          webCommand = WEBCOMMANDUP;
        }
        if (request.indexOf("/SCREEN=STOP") != -1)  {
          webCommand = WEBCOMMANDSTOP;
        }
        if (request.indexOf("/SCREEN=DOWN") != -1)  {
          webCommand = WEBCOMMANDDOWN;
        }
      }
      client.stop();  // client no longer connected
    }

    webServerPreviousMillis += webServerInterval;
  }  // End of time to do the task
}  // End of UpdateWebServer function

void serviceOTA() {
  if (currentMillis - OTAPreviousMillis >= OTAInterval) { 
    ledState = !ledState;
    digitalWrite(LEDPIN, ledState);  // Flash LED

    ArduinoOTA.handle();

    OTAPreviousMillis += OTAInterval;
  }
}  // End of serviceOTA function
