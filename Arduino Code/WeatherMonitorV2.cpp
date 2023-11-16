/**
 * Window Weather Monitor
 * 
 * This program is designed to monitor the weather outside a window and control the window based on the weather.
 * It utilizes a ESP32 Huzzah Feather board, a DHTxx temperature and humidity sensor, a LM393 rain sensor, a stepper motor,
 * and a 4x4 keypad. It also has a 128x64 OLED display for debugging and displaying the current temperature and humidity.
 * 
 * Some components are utilized as proof of concept and would be replaced or altered for a final product.
 * (For example, the stepper motor is not strong enough to open the window, and the components would need to be housed outside the window)
 * 
 * It polls the DHT device for temperature and humidity readings and sends them to the Sinric Pro server.
 * It also sends the readings to the OLED display over I2C on address 0x3D.
 * The DHT polling is done every 60 seconds, and data is sent serially to the ESP32 on pin 4.
 * 
 * The rain sensor is a secondary system which can detect if it is raining. It utilizes a resistive panel and a
 * differential amplifier to detect the presence of water. Since it amplifies the difference between the two inputs, a 1 indicates
 * that the sensor is dry and a 0 indicates that the sensor is wet. The sensor is connected to the ESP32 on pin 17. 
 *
 * The stepper motor is a proof of concept for controlling the window, the final product would need a stronger motor and 
 * a dedicated power supply. There is breakout board for the stepper motor which is connected to the ESP32 on pins 2, 4, 15, and 16. 
 * The breakout board used the values sent to the pins to determine the direction and speed of the motor. Power is supplied to the
 * motor separately from the ESP32.
 * 
 * The keypad is connected to a breakout board which is connected to the ESP32 over I2C on address 0x34. The keypad is used to
 * control the monitor and adjust settings. It can also be used to manually open and close the window.
 * 
 * There are a couple of status LEDs on the PCB. The red LED indicates that the ESP32 is powered on. The green LED indicates that
 * window is open. The RGB LED is used to indicate the temperature status. Blue indicates that the temperature is below the lower limit,
 * Green is within the limits, and Red is above the upper limit. 
 * 
 * Other comments:
 * If the DHT detects unfavorable conditions (temperature too high or too low, humidity too high or too low) it will send a push notification
 * and close the window. Similarly, if the rain sensor detects rain it will send a push notification and close the window.
 * Otherwise, the window will open if the temperature is within the limits and the humidity is within range of the target humidity.
 * 
 * Downloads/Installs:
 * This was developed using the Arduino IDE and Virtual Studio Code with the PlatformIO extension.
 * Arduino IDE uses sketches with .ino extensions, PlatformIO uses .cpp files. You should be able to copy over the code in the cpp files into 
 * the .ino file in the Arduino IDE and it should work.
 * - https://www.arduino.cc/en/software
 * - https://code.visualstudio.com/
 * - https://platformio.org/install/ide?install=vscode
 * 
 *************
 * Used guides:
 * - https://learn.adafruit.com/adafruit-tca8418-keypad-matrix-and-gpio-expander-breakout/arduino
 * - https://learn.adafruit.com/adafruit-128x64-oled-featherwing/arduino-code
 * - https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
 * 
 * 
 * Github Libraries:
 * - https://github.com/sinricpro/esp8266-esp32-sdk (Sinric Pro)
 * - https://github.com/adafruit/Adafruit_TCA8418 (Keypad)
 * - https://github.com/adafruit/Adafruit_SH110x (OLED Display)
 * - https://github.com/markruys/arduino-DHT (DHT Sensor)
 * 
 * 
 * Other:
 * - https://forums.adafruit.com/viewtopic.php?t=110757 (Graphing on OLED Display initial graph idea since I don't have the part)
 * 
 **/

/*************
 * Libraries *
 *************/

#include <Arduino.h>
#ifdef ESP32
  #include <WiFi.h>
#else
  #error "ESP32 device required for this project"
#endif

/* Sinric Pro headers */
#include <SinricPro.h>
#include <SinricProTemperaturesensor.h>
#include "WeatherMonitor.h"

/* DHT sensor library */
#include <DHT.h>

/* Keypad Matrix Library */
#include <Adafruit_TCA8418.h>

/* OLED Display Library */
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>


/* OLED definitions */
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

/* Keypad Definitions */
#define ROWS 4
#define COLS 4

//define the symbols on the buttons of the keypad
char keymap[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};


/* Sinric Pro definitions */
#define APP_KEY    "7a7caefc-db9f-4372-b86d-41393f1f74cd"
#define APP_SECRET "8fede556-9e2f-4613-b78b-aeebc5cd2dbb-84d4844f-28c7-4350-9a96-d69387f56bbb"
#define DEVICE_ID  "653846228332c2648adaa2a7"

/* Wifi Credentials */
#define SSID       "Pixel_7137" //"PSU-IoT" //
#define PASS       "tc9h7msz9rpug8x" //"9SFkew1Hi2HyRANA" //

/* Pin definitions */
#define rainAnalog 36
#define rainDigital 17

/* DHT definitions */
#define EVENT_WAIT_TIME   60000               // send event every 60 seconds
#define DHT_PIN           4                   // located on pin 4

/* Serial Communication rate */
#define BAUD_RATE  115200

/* Objects */
WeatherMonitor &weatherMonitor = SinricPro[DEVICE_ID];  // make instance of SinricPro device
DHT dht;                                                // make instance of DHT sensor
Adafruit_SH1107 display = Adafruit_SH1107(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_TCA8418 keypad;

/*************
 * Prototypes *
 *************/

void handleTemperaturesensor(void);
void handleRainSensor(void);
void handleKeypad(void);
void handleDisplay(void);

bool check_interval(unsigned long* previousMillis, long interval);
char get_key(void);
void drawGraph(void);
void drawTempGraph(void);

void setupSinricPro(void);
void setupWiFi(void);
void setupKeypad(void);
void setupDisplay(void);

void testKeypad(void);


/*************
 * Variables *
 ***********************************************
 * Global variables to store the device states *
 ***********************************************/

// ToggleController
std::map<String, bool> globalToggleStates;

/* DHT device */
bool deviceIsOn;                              // Temeprature sensor on/off state
float temperature;                            // actual temperature
float humidity;                               // actual humidity
float lastTemperature;                        // last known temperature (for compare)
float lastHumidity;                           // last known humidity (for compare)
unsigned long lastEvent = (-EVENT_WAIT_TIME); // last time event has been sent

/* LM393 Rain Sensor */
unsigned long LM393_previous_millis = 0;        // will store last time rain sensor was checked
const long LM393_sample_interval = 10 * 1000;   // interval at which to sample (in milliseconds)

/*************
 * Functions *
 *************/

/* checks if enough time has passed since last occurance, returns true/false*/
bool check_interval(unsigned long* previousMillis, long interval) {
  unsigned long currentMillis = millis();
  if (currentMillis - *previousMillis >= interval) {
    *previousMillis = currentMillis;
    return true;
  }
  return false;
}

/* get key press */
char get_key() {
  if (keypad.available()) {
    //  datasheet page 15 - Table 1
    int k = keypad.getEvent();
    bool pressed = k & 0x80;
    k &= 0x7F;
    k--;
    uint8_t row = k / 10;
    uint8_t col = k % 10;

    ///*
    if (pressed)
      Serial.print("PRESS\tR: ");
    else
      Serial.print("RELEASE\tR: ");
    Serial.print(row);
    Serial.print("\tC: ");
    Serial.print(col);
    Serial.print(" - ");
    Serial.print(keymap[col][row]);
    Serial.println();
    //*/
    return keymap[col][row];
  }
  return 0;
}

/* Draw Graph (copied function, verify it works) 
https://forums.adafruit.com/viewtopic.php?t=110757 */
void drawGraph() {
  for (uint8_t i = 1; i < 4; i++) {
    display.drawPixel(6, SCREEN_HEIGHT - SCREEN_HEIGHT / 4 * i, SH110X_WHITE);
  }
  display.drawPixel(27, 62, SH110X_WHITE); 
  display.drawPixel(47, 62, SH110X_WHITE); 
  display.drawPixel(67, 62, SH110X_WHITE); 
  display.drawPixel(87, 62, SH110X_WHITE); 
  display.drawPixel(107, 62, SH110X_WHITE);
  for (uint8_t i = 7; i < SCREEN_WIDTH; i = i + 5) {
    display.drawPixel(i, SCREEN_HEIGHT - SCREEN_HEIGHT / 4, SH110X_WHITE); 
  }
  display.drawFastVLine(7, 0, 63, SH110X_WHITE);
  display.drawFastHLine(7, 63, 120, SH110X_WHITE);
}

/* Draw Temp Graph (copied function, verify it works) 
https://forums.adafruit.com/viewtopic.php?t=110757 */
void drawTempGraph() {

  display.clearDisplay();
  drawGraph();
  display.setCursor(9, 0);
  display.print(F("Temp:"));
  display.print((float) temperature, 1);
  display.println('C');
  display.setCursor(73, 0);
  display.print(F("Cur:"));
  display.print((float) temperature, 1);
  display.println('C');
  display.setCursor(0, 0);
  display.write(24); 
  display.setCursor(0, 8);
  display.print('T'); 
  

}

/*************
 * Callbacks *
 *************/

// ToggleController
bool onToggleState(const String& deviceId, const String& instance, bool &state) {
  Serial.printf("[Device: %s]: State for \"%s\" set to %s\r\n", deviceId.c_str(), instance.c_str(), state ? "on" : "off");
  globalToggleStates[instance] = state;
  return true;
}

/* handleTemperatatureSensor()
 * - Checks if Temperaturesensor is turned on
 * - Checks if time since last event > EVENT_WAIT_TIME to prevent sending too many events
 * - Get actual temperature and humidity and check if these values are valid
 * - Compares actual temperature and humidity to last known temperature and humidity
 * - Send event to SinricPro Server if temperature or humidity changed
 */
void handleTemperaturesensor() {
  if (deviceIsOn == false) return; // device is off...do nothing

  unsigned long actualMillis = millis();
  if (actualMillis - lastEvent < EVENT_WAIT_TIME) return; //only check every EVENT_WAIT_TIME milliseconds

  temperature = dht.getTemperature();          // get actual temperature in °C
//  temperature = dht.getTemperature() * 1.8f + 32;  // get actual temperature in °F
  humidity = dht.getHumidity();                // get actual humidity

  if (isnan(temperature) || isnan(humidity)) { // reading failed... 
    Serial.printf("DHT reading failed!\r\n");  // print error message
    return;                                    // try again next time
  } 

  if (temperature == lastTemperature && humidity == lastHumidity) 
  {
    //Serial.println("Same temp and humidity as last checkin");
  }

  bool success = weatherMonitor.sendTemperatureEvent(temperature, humidity); // send event
  if (success) {  // if event was sent successfuly, print temperature and humidity to serial
    Serial.printf("Temperature: %2.1f Celsius\tHumidity: %2.1f%%\r\n", temperature, humidity);
  } else {  // if sending event failed, print error message
    Serial.printf("Something went wrong...could not send Event to server!\r\n");
    return;
  }

  lastTemperature = temperature;  // save actual temperature for next compare
  lastHumidity = humidity;        // save actual humidity for next compare
  lastEvent = actualMillis;       // save actual time for next compare
}

/* handleRainSensor()
 * - Checks if time since last call > LM393_sample_interval to prevent sending too many events
 * - Get actual rain sensor values (doesn't check if they are valid)
 * - Prints values to Serial
 * - Other functionality not yet implemented
 * - TODO: - Add input validation
 *         - Add rain sensitivity adjustment
 *         - Add rain sensor timeout during rain event (will likely be wet for a while even after rain stops)
 */
void handleRainSensor(){
  /* Determine if enough time has passed since last check-in */
  if (check_interval(&LM393_previous_millis, LM393_sample_interval)) {
    int rainAnalogVal = analogRead(rainAnalog);
    int rainDigitalVal = digitalRead(rainDigital);
    
    Serial.print("Analog: ");
    Serial.print(rainAnalogVal);
    //Trust the Digital value over the Analog value, for whatever reason the Analog value doesn't match the sensitivity of the digitial level
    //ADC input is weighted heavily towards 4095
    Serial.print("\tDigitial: ");
    Serial.println(rainDigitalVal);
  }
}

/* handleKeypad()
 * - Follows guide: https://learn.adafruit.com/adafruit-tca8418-keypad-matrix-and-gpio-expander-breakout/arduino
 * - Checks if a key has been pressed
 * - Prints key to Serial
 * - Other functionality not yet implemented
 * - TODO: Add menu system with options to adjust settings:
 *          - Rain sensitivity
 *          - Temperature sensor upper and lower limits
 *          - Manually open/close window
 *          - Timeout/Sleep mode?
 */
void handleKeypad(){
    // analyze user inputs
  /*
  / switch (get_key()) {
    case 'A':
      Serial.println("Scroll up");
      menu.scrollUp();
      break;
    case 'B':
      Serial.println("Scroll down");
      menu.scrollDown();
      break;
    case 'C':
      Serial.println("Select");
      menu.select();
      break;
    case 'D':
      Serial.println("Back");
      menu.back();
      break;
    default:
      break;
}
  */

  //TODO: Add menu system
}

/* handleDisplay()
 * - TODO: - Display current temperature and humidity on the display
 *         - Other functionality not yet implemented
 *  
 */
void handleDisplay(){
  //TODO: Add display functionality
  Serial.println("Found measurements:");
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.print(" Humidity: ");
  Serial.println(humidity);
  Serial.println("Displaying measurements:");

  //Send measurements to display
  //drawTempGraph();

  
}

/**********
 * Events *
 *************************************************
 * Examples how to update the server status when *
 * you physically interact with your device or a *
 * sensor reading changes.                       *
 *************************************************/

// TemperatureSensor
void updateTemperature(float temperature, float humidity) {
  weatherMonitor.sendTemperatureEvent(temperature, humidity);
}

// PushNotificationController
void sendPushNotification(String notification) {
  weatherMonitor.sendPushNotification(notification);
}

// ToggleController
void updateToggleState(String instance, bool state) {
  weatherMonitor.sendToggleStateEvent(instance, state);
}

/********* 
 * Setup *
 *********/

void setupSinricPro() {
  // ToggleController
  weatherMonitor.onToggleState("toggleInstance1", onToggleState);

  SinricPro.onConnected([]{ Serial.printf("[SinricPro]: Connected\r\n"); });
  SinricPro.onDisconnected([]{ Serial.printf("[SinricPro]: Disconnected\r\n"); });
  SinricPro.begin(APP_KEY, APP_SECRET);
};

void setupWiFi() {
  #if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP); 
    WiFi.setAutoReconnect(true);
  #elif defined(ESP32)
    WiFi.setSleep(false); 
    WiFi.setAutoReconnect(true);
  #endif

  WiFi.begin(SSID, PASS);
  Serial.printf("[WiFi]: Connecting to %s", SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }
  Serial.printf("connected\r\n");
}

void setupKeypad() {
  // Initialize the TCA8418 with I2C addr 0x34
  if (!keypad.begin(TCA8418_DEFAULT_ADDR, &Wire)){
    Serial.println("Keypad not found!");
    while (1); //trap program to show error TODO: better user feedback (Visual and/or audible?)
  }
  Serial.println("Keypad initialized!");

  // configure the size of the keypad matrix.
  // all other pins will be inputs
  keypad.matrix(ROWS, COLS);

  // flush the internal buffer
  keypad.flush();
}

void setupDisplay() {
  //https://learn.adafruit.com/adafruit-128x64-oled-featherwing/arduino-code
  // Initialize display with I2C addr 0x3D
  if(!display.begin(SCREEN_ADDRESS, true)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); //trap program to show error TODO: better user feedback (Visual and/or audible?)
  }
  Serial.println("Display initialized!");
  // Show initial display buffer contents on the screen --
  // the library initializes this with an Adafruit splash screen.
  display.display();
}

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(rainDigital,INPUT);
  pinMode(rainAnalog,INPUT);

  setupKeypad();
  setupWiFi();
  setupSinricPro();
  sendPushNotification("ESP Device is online");

  // Initial readings
  handleRainSensor();   // check rain sensor
  handleTemperaturesensor(); // check temperature sensor
}

/********* 
 * Test Functions *
 *********/

void testKeypad(){
  if (keypad.available() > 0) {
    //  datasheet page 15 - Table 1
    int k = keypad.getEvent();
    bool pressed = k & 0x80;
    k &= 0x7F;
    k--;
    uint8_t row = k / 10;
    uint8_t col = k % 10;

    if (pressed)
      Serial.print("PRESS\tR: ");
    else
      Serial.print("RELEASE\tR: ");
    Serial.print(row);
    Serial.print("\tC: ");
    Serial.print(col);
    Serial.print(" - ");
    Serial.print(keymap[col][row]);
    Serial.println();
  }
}

/********
 * Loop *
 ********/
void loop() {
  /* Perform Sinric Pro actions*/
  SinricPro.handle();

  /* Check for input from the keypad */
  handleKeypad();

  /* Check for input from the rain sensor */
  handleRainSensor();

  /* Measure temperature and humidity. */
  handleTemperaturesensor();

  /* Display temperature and humidity on the display */
  handleDisplay();

  }