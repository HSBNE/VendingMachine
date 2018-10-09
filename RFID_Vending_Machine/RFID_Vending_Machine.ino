#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>

// Wifi Details
const char* ssid = "HSBNEWiFi";
const char* password = "<secret>";

// Connection details.
const char* apiHost = "http://10.0.0.101";
const char* apiSecret = "cookiemonster";

// Machine vending details.
const int costAmount = 200; // in cents
const String costString = "2.00"; // displayed on the screen
const int dispensePin = 4; // pin that triggers the vending machine relay
const char* machineName = "VEND-DrinksLeft"; // used as WiFi and OTA host name
const char* purchaseDescription = "Vending Machine Drink Purchase"; //"Drink purchase from a vending machine."; // description sent to debit API
const char* machinePassword = "<secret>"; // secret password for OTA updates

//LCD Stuff
LiquidCrystal_I2C lcd(0x27, 16, 2);
#define printByte(args)  write(args);
uint8_t check[8] = {0x0, 0x1 , 0x3, 0x16, 0x1c, 0x8, 0x0};
uint8_t cross[8] = {0x0, 0x1b, 0xe, 0x4, 0xe, 0x1b, 0x0};
uint8_t clock[8] = {0x0, 0xe, 0x15, 0x17, 0x11, 0xe, 0x0};

WiFiClient client;
HTTPClient http;

void setup() {
  Serial.begin(9600); // this talks to our rfid reader
  Serial.println("Booting");

  // set our dispense pin as an output
  pinMode(dispensePin, OUTPUT);

  // init the LCD
  initLCD();
  writeLCD("Waiting for     WiFi", 1);
  

  // connect to WiFi and setup our details
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.hostname(machineName);

  // wait until we're connected to WiFi before proceeding
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Connection Failed! Rebooting...");
    delay(1000);
    ESP.restart();
  }
  
  configOTA();
  setStandbyScreen();
}

void loop() {
  ArduinoOTA.handle();
  Serial.println("Mischief managed.");
  readTag();
}

void configOTA() {
  ArduinoOTA.setHostname(machineName);
  ArduinoOTA.setPassword(machinePassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    writeLCD("Update Progress: " + String(progress / (total / 100)) + "%", 1);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      writeLCD("Update Failed.  Auth Failed.", 1);
    }
    else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      writeLCD("Update Failed.  Begin Failed.", 1);
    }
    else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      writeLCD("Update Failed.  Connect Failed.", 1);
    }
    else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      writeLCD("Update Failed.  Receive Failed.", 1);
    }
    else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      writeLCD("Update Failed.  End Failed.", 1);
    }

    delay(2000);
    setStandbyScreen();
  });
  ArduinoOTA.begin();
}

void setStandbyScreen() {
  writeLCD("Drinks $" + costString + "    Swipe/InsertCoin", 1);
}

void initLCD() {
  lcd.init();
  lcd.createChar(0, check);
  lcd.createChar(1, cross);
  lcd.createChar(2, clock);
  lcd.backlight();
  //  writeLCD("RFID Disabled for Testing", 1);
  lcd.backlight();
}

void writeLCD(String lcdText, bool clear) {
  lcd.home();
  if (clear) {
    lcd.clear();
  }
  lcd.print(lcdText.substring(0, 15));
  lcd.setCursor(0, 1);
  lcd.print(lcdText.substring(16, 32));
  lcd.noCursor();
}

void dispense() {
  digitalWrite(dispensePin, HIGH);
  delay(500);
  digitalWrite(dispensePin, LOW);
}

void readTag() {
  char tagBytes[6];

  //  while (!Serial.available()) { delay(10); }

  if (Serial.readBytes(tagBytes, 5) == 5)
  {
    uint8_t checksum = 0;
    uint32_t cardId = 0;

    tagBytes[6] = 0;

    //    Serial.println("Raw Tag:");
    for (int i = 0; i < 4; i++)
    {
      checksum ^= tagBytes[i];
      cardId = cardId << 8 | tagBytes[i];
      //     Serial.println(tagBytes[i], HEX);
    }

    if (checksum == tagBytes[4])
    {
      Serial.print("Tag Number:");
      Serial.println(cardId);
      Serial.flush();
      checkCard(cardId);
    }
  }
}

void delayThenStandby(int times) {
  for (int x = times; x > 0; x--) {
    writeLCD("Please wait to  swipe again: " + String(x) + "s", 1);
    delay(1000);
  }
  Serial.flush();
  setStandbyScreen();
}

void checkCard(long tagid) {
  writeLCDIcon("Card Detected!  Checking Server...", 2);

  // We now create a URI for the request
  String url = String(apiHost) + "/api/spacebucks/debit/" + String(tagid) + "/" + String(costAmount) + "/" + urlencode(purchaseDescription) + "?secret=" + apiSecret;
  Serial.print("Requesting URL: ");
  Serial.println(url);

  if (http.begin(url)) {
    int httpCode = http.GET();
    Serial.println("HTTP Response Code: " + String(httpCode));

    // httpCode will be negative on error
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println(response);
        DynamicJsonBuffer jsonBuffer;
        JsonObject&root = jsonBuffer.parseObject(response.substring(response.indexOf('{'), response.length()));

        if ( root[String("success")] == "true" ) {
          Serial.println("Purchase approved.");
          String balance = root[String("balance")];
          writeLCDIcon("Accepted!       $" + balance + " left.", 0);
          dispense();
        } else {
          String balance = root[String("balance")];
          Serial.println("Purchase failed for some reason.");
          Serial.println(response);
          writeLCDIcon("Denied :(         $" + balance + " left.", 1);
        }

        Serial.flush(); // make sure the buffer is clear
        delay(4000); // give users time to see the "approved $x left" message.
        delayThenStandby(3);
      } else {
        Serial.println("Unknown server error :(.");
        writeLCDIcon("Unknown server  error :(", 1);
        delay(4000);
        setStandbyScreen();
      }
    } else {
      Serial.println("Unknown server error :(.");
      writeLCDIcon("Unknown server  error :(", 1);
      delay(4000);
      setStandbyScreen();
    }
  }
  flushSerial();
}

void writeLCDIcon(String lcdText, int icon) {
  lcd.clear();
  lcd.printByte(icon);
  lcd.print(" " + lcdText.substring(0, 15));
  lcd.setCursor(0, 1);
  lcd.print(lcdText.substring(16, 32));
  lcd.noCursor();
}

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  char code2;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      code2 = '\0';
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
      //encodedString+=code2;
    }
    yield();
  }
  return encodedString;

}

void flushSerial() {
  int flushCount = 0;
  while (  Serial.available() ) {
    char t = Serial.read();  // flush any remaining bytes.
    flushCount++;
    // Serial.println("flushed a byte");
  }
  if (flushCount > 0) {
    flushCount = 0;
  }

}
