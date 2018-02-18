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

const int screenDownDurationMillis = 2000; // How long it takes for the screen to wind all the way down
const int screenUpDurationMillis = 3000; // How long it takes for the screen to wind all the way up

enum webCommandType {STOP, UP, DOWN, NOTHING};  // Possible HTTP commands to control screen
enum projectorCommandType {NO_CHANGE, JUST_TURNED_ON, JUST_TURNED_OFF};  // Possible projector commands to control screen
enum projectorStateType  // State machine for projector control signal 
  {STABLE_OFF, OFF_BUT_UNSTABLE, STABLE_ON, ON_BUT_UNSTABLE};
enum screenStateType  // State machine for screen motor control 
  {STATIONARY_UP, MOTORING_DOWN, STATIONARY_DOWN, MOTORING_UP, STATIONARY_MIDDLE};

// ===== VARIABLES
unsigned long currentMillis = 0;    // stores the value of millis() in each iteration of loop()
unsigned long controlRelaysPreviousMillis = 0;   // will store last time state machine was run
unsigned long OTAPreviousMillis = 0;   // will store last time OTA was handled
unsigned long screenDownStartMillis = 0;  // time when the down motor was turned on
unsigned long screenUpStartMillis = 0;  // time when the up motor was turned on

byte ledState = LOW;  // for toggling the LED
byte projectorSignalNow = LOW;  // currently-read signal from the projector
boolean hardwareSignalChanged = false;
webCommandType webCommand = NOTHING;    // command from http - default to NOTHING
screenStateType screenState = STATIONARY_UP;  // state machine controlling screen motor relays - default to UP
projectorStateType projectorState = STABLE_OFF;  // projector signal state machine defaults to off
projectorCommandType projectorCommand = NO_CHANGE; // default to the projector being stable

ESP8266WebServer myHTTPserver(80);  // Create a webserver object that listens for HTTP request on port 80

// ===== SETUP FUNCTION
void setup()
  {
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
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
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
    if (OTA_AUTH_ERROR == error) Serial.println("Auth Failed");
    else if (OTA_BEGIN_ERROR == error) Serial.println("Begin Failed");
    else if (OTA_CONNECT_ERROR == error) Serial.println("Connect Failed");
    else if (OTA_RECEIVE_ERROR == error) Serial.println("Receive Failed");
    else if (OTA_END_ERROR == error) Serial.println("End Failed");
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
void loop() 
{
  currentMillis = millis();

  controlRelays();
  serviceOTA();
}  // End of MAIN LOOP function

projectorCommandType interpretProjectorSignal() // debounces and detects confirmed edges on hardware signal
{
  projectorSignalNow = digitalRead(PROJECTORSIGNAL);

  switch (projectorState)
  {
    case STABLE_OFF:
      if (HIGH == projectorSignalNow) // change is afoot...
      {
        projectorState = OFF_BUT_UNSTABLE;
      }
      return NO_CHANGE; // regardless, because we can't yet be sure what's happening
      break;

    case OFF_BUT_UNSTABLE:
      if (HIGH == projectorSignalNow) // projector is definitely on
      {
        projectorState = STABLE_ON;
        return JUST_TURNED_ON;
      }
      else // projector signal is low again, so it was just noise
      {
        projectorState = STABLE_OFF;
        return NO_CHANGE;
      }
      break;

    case STABLE_ON:
      if (LOW == projectorSignalNow) // change is afoot...
      {
        projectorState = ON_BUT_UNSTABLE;
      }
      return NO_CHANGE; // regardless, because we can't yet be sure what's happening
      break;

    case ON_BUT_UNSTABLE:
      if (LOW == projectorSignalNow) // projector is definitely off
      {
        projectorState = STABLE_OFF;
        return JUST_TURNED_OFF;
      }
      else // projector signal is high again, so it was just noise
      {
        projectorState = STABLE_ON;
        return NO_CHANGE;
      }
      break;
  }
}

void controlRelays()  // this is the main state machine
{
  if (currentMillis - controlRelaysPreviousMillis >= controlRelaysInterval)
  {
    // gather incoming commands here.  They're events, so must be acted up on immediately or they'll be lost...
    projectorCommand = interpretProjectorSignal(); // gets state of projector
    myHTTPserver.handleClient(); // Listen for HTTP requests from clients

    switch (screenState)
    {
      case STATIONARY_UP:
        digitalWrite(RELAYL1PIN, LOW);  // make sure both relays are off
        digitalWrite(RELAYL2PIN, LOW);

        if ((JUST_TURNED_ON == projectorCommand) || (DOWN == webCommand))
        {
          screenDownStartMillis = currentMillis;  // remember what time the motor was turned on
          screenState = MOTORING_DOWN;
        }
        break;

      case MOTORING_DOWN:
        digitalWrite(RELAYL1PIN, LOW);
        digitalWrite(RELAYL2PIN, HIGH); // going down...

        if (currentMillis - screenDownStartMillis >= screenDownDurationMillis) // screen will be all the way down by now
        {
          screenState = STATIONARY_DOWN;
        }
        else if (STOP == webCommand)
          {
            screenState = STATIONARY_MIDDLE;
          }
          else if ((JUST_TURNED_OFF == projectorCommand) || (UP == webCommand))
            {
              screenUpStartMillis = currentMillis;  // remember what time the motor was turned on
              screenState = MOTORING_UP;
            }
        break;

      case STATIONARY_DOWN:
        digitalWrite(RELAYL1PIN, LOW);  // make sure both relays are off
        digitalWrite(RELAYL2PIN, LOW);

        if ((JUST_TURNED_OFF == projectorCommand) || (UP == webCommand))
        {
          screenUpStartMillis = currentMillis;  // remember what time the motor was turned on
          screenState = MOTORING_UP;
        }
        break;

      case MOTORING_UP:
        digitalWrite(RELAYL1PIN, HIGH); // going up...
        digitalWrite(RELAYL2PIN, LOW);

        if (currentMillis - screenUpStartMillis >= screenUpDurationMillis) // screen will be all the way up by now
        {
          screenState = STATIONARY_UP;
        }
        else if (STOP == webCommand)
          {
            screenState = STATIONARY_MIDDLE;
          }
          else if ((JUST_TURNED_ON == projectorCommand) || (DOWN == webCommand))
            {
              screenDownStartMillis = currentMillis;  // remember what time the motor was turned on
              screenState = MOTORING_DOWN;
            }
        break;

      case STATIONARY_MIDDLE:
        digitalWrite(RELAYL1PIN, LOW);  // make sure both relays are off
        digitalWrite(RELAYL2PIN, LOW);

        if ((JUST_TURNED_ON == projectorCommand) || (DOWN == webCommand))
        {
          screenDownStartMillis = currentMillis;  // remember what time the motor was turned on
          screenState = MOTORING_DOWN;
        }
        else if ((JUST_TURNED_OFF == projectorCommand) || (UP == webCommand))
          {
            screenUpStartMillis = currentMillis;  // remember what time the motor was turned on
            screenState = MOTORING_UP;
          }
        break;
    }

    controlRelaysPreviousMillis += controlRelaysInterval;
  }
}  // END OF CONTROLRELAYS function

void redirectBrowserHome()
{
    myHTTPserver.sendHeader("Location","/"); // Add a header to respond with a new location for the browser to go to the home page again
    myHTTPserver.send(303); // Send it back to the browser with an HTTP status 303 (See Other) to redirect
}

void handleRoot() // When URI / is requested, send a web page with a button to toggle the LED
{
  myHTTPserver.send(200, "text/html", "<h1>Sonoff Dual R2 Projector Screen Controller</h1>"
  "<p>MAC address: " + WiFi.macAddress() + "</p>"
  "<p>Projector signals it's " + ((digitalRead(PROJECTORSIGNAL) == HIGH)?"ON":"OFF") + "<br>"
  "  projectorCommand is " + projectorCommand + "<br>"
  "  screenState is " + screenState + "</p>"
  "<p>Button is now " + ((digitalRead(BUTTONPIN) == LOW)?"Pushed":"Released") + "</p>"
  "<form action=\"/SCREEN=UP\" method=\"POST\"><input type=\"submit\" value=\"Screen Up\"></form>"
  "<form action=\"/SCREEN=STOP\" method=\"POST\"><input type=\"submit\" value=\"Screen STOP\"></form>"
  "<form action=\"/SCREEN=DOWN\" method=\"POST\"><input type=\"submit\" value=\"Screen Down\"></form>"
  "<br>"
  "<form action=\"/LED\" method=\"POST\"><input type=\"submit\" value=\"Toggle LED\"></form>"
  "<p><small>Free Heap RAM: " + ESP.getFreeHeap() + "</small></p>");
}

void toggleLED()
{
  ledState = !ledState;
  digitalWrite(LEDPIN, ledState);  // Invert LED
}

void serviceOTA()
{
  if (currentMillis - OTAPreviousMillis >= OTAInterval)
  {
    toggleLED();
    ArduinoOTA.handle();

    OTAPreviousMillis += OTAInterval;
  }
}  // End of serviceOTA function
