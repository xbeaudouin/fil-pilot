#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <TimeLib.h>
#include <WiFiManager.h>

#define WIFI_HOSTNAME "FilPilotESP"

#define MQTT_SERVER        "192.0.2.17"
#define MQTT_PORT          1883
#define MQTT_USER          ""
#define MQTT_PASSWORD      ""
#define MQTT_TOPIC_ROOT    "filpilot/"  //this is where mqtt data will be pushed
#define MQTT_PUSH_FREQ_SEC 2  //maximum mqtt update frequency in seconds

// Debug
#define DEBUG 1

#ifdef DEBUG
 #define debug(x)   Serial.print(x)
 #define debugln(x) Serial.println(x)
#else /* DEBUG */
 #define debug(x)
 #define debugln(x)
#endif /* DEBUG */

char msgToPublish[MQTT_MAX_PACKET_SIZE + 1];

// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Set Webserver port number to 80
WiFiServer server(80);
// Varifble to store HTTP request
String header;

// FP status info LittleFS
#define FP_STATUS "/fp_status.txt"

File Status_File;

// Configuration des IO
const uint16_t ALT_POS = 13; // Broche pilotant l'alternance positive
const uint16_t ALT_NEG = 14; // Broche pilotant l'alternance négative

const uint8_t DEFAULT_PILOTE_STATUS = 1; // Mode par défaut Eco

// Names of states
const char fp_states[][9]={ "Confort", "Eco", "Hors-Gel", "Off"};

unsigned int fp = 0;
unsigned int fp_old = 0;

// Cette fonction pilote le changement d'état des sorties, de la LED bicolore d'état et le mémorise dans le fichier d'état
void Pilote (int Status) {
    switch (Status) {
      case 0 : // aucune alternance en sortie, LED allumée en rouge : Confort
        debugln("Confort");
        digitalWrite(ALT_NEG,LOW);
        digitalWrite(ALT_POS,LOW);
        break;
      case 1 : // pleine alternance en sortie, LED allumée en orange (rouge+vert) : Eco
        debugln("Eco");
        digitalWrite(ALT_NEG,HIGH);
        digitalWrite(ALT_POS,HIGH);
        break;
      case 2 : // demie alternance négative en sortie, LED allumée en vert : Hors Gel
        debugln("Hors Gel");
        digitalWrite(ALT_NEG,HIGH);
        digitalWrite(ALT_POS,LOW);
        break;
      case 3 : // demie alternance positive en sortie, LED éteinte : Arrêt
        debugln("Arrêt");
        digitalWrite(ALT_NEG,LOW);
        digitalWrite(ALT_POS,HIGH);
        break;
   }
    // Mémorisation dans le fichier
    debug(F("Status écrit : "));
    debugln(Status);
    Status_File = LittleFS.open(FP_STATUS, "w+");
    Status_File.write(Status);
    Status_File.seek(0, SeekSet);
    debug(F("Status relu : "));
    debugln(Status_File.read());
    Status_File.close();
}

// Setup OTA Stuff
void setupOTA() {
    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(WIFI_HOSTNAME);

  // No authentication by default
  ArduinoOTA.setPassword("12345");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

// Setup LittleFS
void setupLFS() {
  Serial.println(F("Initializing FS..."));
  if (LittleFS.begin()) {
    debugln(F("LittleFS system mounted with success"));
  } else {
    debugln(F("An Error has occurred while mounting LittleFS"));
  }

  // Get all information about LittleFS
  FSInfo fsInfo;
  LittleFS.info(fsInfo);

  debugln("------------------------------");
  debugln("File system info");
  debugln("------------------------------");
  
  // Taille de la zone de fichier
  debug("Total space:      ");
  debug(fsInfo.totalBytes);
  debugln(" byte");

  // Espace total utilise
  debug("Total space used: ");
  debug(fsInfo.usedBytes);
  debugln(" byte");

  // Taille d un bloc et page
  debug("Block size:       ");
  debug(fsInfo.blockSize);
  debugln(" byte");

  debug("Page size:        ");
  debug(fsInfo.totalBytes);
  debugln(" byte");

  debug("Max open files:   ");
  debugln(fsInfo.maxOpenFiles);

  // Taille max. d un chemin
  debug("Max path lenght:  ");
  debugln(fsInfo.maxPathLength);

  debugln();

  debugln("------------------------------");
  debugln("List files");
  debugln("------------------------------");
  // Ouvre le dossier racine | Open folder
  Dir dir = LittleFS.openDir("/");
  // Affiche le contenu du dossier racine | Print dir the content
  while (dir.next()) {
    // recupere le nom du fichier | get filename
    debug(dir.fileName());
    debug(" - ");
    // et sa taille | and the size
    if (dir.fileSize()) {
      File file = dir.openFile("r");
      debug(file.size());
      debugln(" byte");
      file.close();
    } else {
      File file = dir.openFile("r");
      if ( file.isDirectory() ) {
        debugln("this is a folder");
      } else {
        debugln("file is empty");
      }
      file.close();
    }
  }
}

void fp_from_fs() {
    unsigned int Num_Cde;
    
    if (!LittleFS.exists(FP_STATUS)) {
      debugln("Fil Pilot status file does not exists. Create it");
      Status_File = LittleFS.open(FP_STATUS, "w");
      Status_File.write(DEFAULT_PILOTE_STATUS);
      Status_File.close();
      Pilote(DEFAULT_PILOTE_STATUS);
      return;
    }
    // File should exist so read it
    debugln(F("Status FP from FS  : "));
    Status_File = LittleFS.open(FP_STATUS, "r");
    Num_Cde = Status_File.read();
    Status_File.close();
    debug(F("Status is : "));
    debugln(Num_Cde);
    fp = Num_Cde;
    Pilote(Num_Cde);
}

void setup() {
  Serial.begin(115200);
  debugln("Booting");

  // WifiManager
  WiFiManager wifiManager;
  // If needed to reset :
  //wifiManager.resetSetting();
  
  // Fetch SSID and pass from eeprom and tries to connect
  // OtherWise : captive portal... etc..
  wifiManager.autoConnect("Setup" WIFI_HOSTNAME);

  // Setup OTA
  setupOTA();

  // Setup LittleFS
  setupLFS();
  
  // Alternance -
  pinMode(ALT_NEG, OUTPUT);
  // Alternance +
  pinMode(ALT_POS, OUTPUT);

  // Now retreive FP status from FS
  fp_from_fs();

  // Start Webserver
  server.begin();

  // We are ready
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("Booted !");  
}

void loop() {
  WiFiClient client = server.available();

  // Do MQTT stuff
  mqttLoop();
  // Wifi
  if (client) {
    debugln("New HTTP CLient.");
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        debug(c);
        header += c;
        if (c== '\n') {
          if (currentLine.length() == 0) {
            client.println("HTTP/1.0 200 OK");
            client.println("Content-Type: text/html");
            client.println("Conection: close");
            client.println();
            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            
            // Web Page Heading
            client.println("<body><h1>Fil-Pilote Web Server</h1>");
            client.println("<p>Current State : <b>");
            client.println(fp_states[fp]);
            client.println("</b></p>");
            client.println("</body></html>");
            
            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    debugln("Client disconnected.");
    debugln("");
  }
  // In case of OTA upgrade
  ArduinoOTA.handle();
}

// MQTT Stuff
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  debug("Message arrived [");
  debug(topic);
  debug("] ");

  char message[5]={0x00};
  for (int i = 0; i < length; i++) {
    message[i]=(char)payload[i];
  }
  message[length]=0x00;
  debug("Message = ");
  debug(message);
  debug(" ");
  String str_command = String(message);
  str_command.toUpperCase();
  debug("Search for :");
  debugln(str_command);
  
  if (str_command.equals("CONFORT")) {
    fp=0;
  } else if (str_command.equals("ECO")) {
    fp=1;
  } else if (str_command.equals("HORS-GEL")) {
    fp=2;
  } else if (str_command.equals("OFF")) {
    fp=3;
  }
  if (fp_old!=fp) {
    Pilote(fp);
  }
}

// Sends MQTT payload to the Mosquitto server running on a Raspberry Pi.
// Mosquitto server deliveres data to Domoticz server running on a same Raspberry Pi
void sendMQTTPayload(String msgpayload)
{
  // Convert payload to char array
  msgpayload.toCharArray(msgToPublish, msgpayload.length()+1);

  //Publish payload to MQTT broker
  if (mqttClient.publish(MQTT_TOPIC_ROOT "Status", msgToPublish))
  {
    debug("Following data published to MQTT broker: ");
    debug(MQTT_TOPIC_ROOT "status");
    debug(" ");
    debugln(msgpayload);
  }
  else 
  {
    debug("Publishing to MQTT broker failed... ");
    debugln(mqttClient.state());
  }
}

void mqttLoop()
{
  //if we have problems with connecting to mqtt server, we will attempt to re-estabish connection each 1minute (not more than that)
  //first: let's make sure we are connected to mqtt
  const char* topicLastWill = MQTT_TOPIC_ROOT "availability";
  const char* topicIP =       MQTT_TOPIC_ROOT "IPAddr";
 
  if (!mqttClient.connected()) {
    if(mqttClient.connect(WIFI_HOSTNAME, MQTT_USER, MQTT_PASSWORD, topicLastWill, 1, true, "offline"))
    {
      mqttClient.publish(topicLastWill, "online", true);
    } else {
      // Wait for 5 seconds
      for(int i=0; i<5000; i++) {
        delay(1);
      }
    }
    mqttClient.subscribe(MQTT_TOPIC_ROOT "set");
    String MQIP = WiFi.localIP().toString();
    MQIP.toCharArray(msgToPublish, MQIP.length()+1);
    mqttClient.publish(topicIP, msgToPublish);
  }
  if (fp != fp_old) {
    sendMQTTPayload(fp_states[fp]);
    fp_old=fp;
  }
  mqttClient.loop();
}
