// ===== LIBRARIES
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>           // easier than using WebServer, apparently
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

// ===== CONSTANTS
const char ssid[]  = WIFI_SSID;  // assigned by the PLATFORMIO_BUILD_FLAGS environment variable
const char password[] = WIFI_PASS;  // ----"-----

const int controlRelaysInterval = 100; // Acts quickly enough for http commands, but also debounces signal.
const int OTAInterval = 500; // Handle OTA reflashing twice a second
const int webServerInterval = 100;  // Quickly respond to web page request

enum webCommandType {STOP, UP, DOWN, NOTHING};  // Possible HTTP commands to control screen
enum motorStateType  // State machine for screen motor control 
  {IDLE, DEBOUNCE_DOWN_COMMAND, MOTORING_DOWN, DEBOUNCE_UP_COMMAND, MOTORING_UP};

// ===== VARIABLES
unsigned long currentMillis = 0;    // stores the value of millis() in each iteration of loop()
unsigned long controlRelaysPreviousMillis = 0;   // will store last time sensor was read
unsigned long OTAPreviousMillis = 0;   // will store last time OTA was handled
unsigned long webServerPreviousMillis = 0;   // will store last time web page was handled

byte ledState = LOW;  // for toggling the LED
webCommandType webCommand = NOTHING;    // command from http

ESP8266WebServer myHTTPserver(80);  // Create a webserver object that listens for HTTP request on port 80

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

  myHTTPserver.on("/", HTTP_GET, [](){ // Call the 'handleRoot' function when a client requests URI "/"
    webCommand = NOTHING; // The default
    handleRoot();
    });
  myHTTPserver.on("/LED", HTTP_ANY, [](){ // when any request is made to URI "/LED"
    toggleLED();
    redirectBrowserHome();
    });
  myHTTPserver.on("/SCREEN=UP", HTTP_ANY, [](){ // Call the 'handleScreenUp' function when any request is made to URI "/SCREEN=UP"
    webCommand = UP;
    redirectBrowserHome();
    });
  myHTTPserver.on("/SCREEN=STOP", HTTP_ANY, [](){ // Call the 'handleScreenUp' function when any request is made to URI "/SCREEN=STOP"
    webCommand = STOP;
    redirectBrowserHome();
    });
  myHTTPserver.on("/SCREEN=DOWN", HTTP_ANY, [](){ // Call the 'handleScreenUp' function when any request is made to URI "/SCREEN=DOWN"
    webCommand = DOWN;
    redirectBrowserHome();
    });
  myHTTPserver.onNotFound( [](){
    myHTTPserver.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when a client requests an unknown URI
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
      case UP:
        digitalWrite(RELAYL1PIN, HIGH);
        digitalWrite(RELAYL2PIN, LOW);
        break;
      case DOWN:
        digitalWrite(RELAYL1PIN, LOW);
        digitalWrite(RELAYL2PIN, HIGH);
        break;
      case STOP:
        digitalWrite(RELAYL1PIN, LOW);
        digitalWrite(RELAYL2PIN, LOW);
        break;
    }

    controlRelaysPreviousMillis += controlRelaysInterval;
  }
}  // END OF CONTROLRELAYS function

void redirectBrowserHome() {
    myHTTPserver.sendHeader("Location","/"); // Add a header to respond with a new location for the browser to go to the home page again
    myHTTPserver.send(303); // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

void handleRoot() { // When URI / is requested, send a web page with a button to toggle the LED
  myHTTPserver.send(200, "text/html", "<h1>Sonoff Dual R2 Projector Screen Controller</h1>"
  "<p>MAC address: " + WiFi.macAddress() + "</p>"
  "<p>Projector signals it's " + ((digitalRead(PROJECTORSIGNAL) == HIGH)?"ON":"OFF") + "</p>"
  "<p>Button is now " + ((digitalRead(BUTTONPIN) == LOW)?"Pushed":"Released") + "</p>"
  "<form action=\"/SCREEN=UP\" method=\"POST\"><input type=\"submit\" value=\"Screen Up\"></form>"
  "<form action=\"/SCREEN=STOP\" method=\"POST\"><input type=\"submit\" value=\"Screen STOP\"></form>"
  "<form action=\"/SCREEN=DOWN\" method=\"POST\"><input type=\"submit\" value=\"Screen Down\"></form>"
  "<br>"
  "<form action=\"/LED\" method=\"POST\"><input type=\"submit\" value=\"Toggle LED\"></form>"
  "<p><small>Free Heap RAM: " + ESP.getFreeHeap() + "</small></p>");
}

void toggleLED() {
  ledState = !ledState;
  digitalWrite(LEDPIN, ledState);  // Invert LED
}

void updateWebServer() {
  if (currentMillis - webServerPreviousMillis >= webServerInterval) {  // Time to do the task
    myHTTPserver.handleClient(); // Listen for HTTP requests from clients

    webServerPreviousMillis += webServerInterval;
  }  // End of time to do the task
}  // End of UpdateWebServer function

void serviceOTA() {
  if (currentMillis - OTAPreviousMillis >= OTAInterval) {

    toggleLED();
    ArduinoOTA.handle();

    OTAPreviousMillis += OTAInterval;
  }
}  // End of serviceOTA function
