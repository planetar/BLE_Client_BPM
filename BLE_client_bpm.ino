/**
 * A BLE client taylored to fetch BPM measurement
 * tested with medisana bu546
 * 
 * This sketch is based on the BLE client example
 * author unknown
 * updated by chegewara
 * adapted/extended by planetar
 
 - wakes every 30s
 - tries to find a BLE server that provides the service we are looking for
 - registers a callback to receive the data via indication
 - formats the received data into json sent to the configured broker 
 */


#include "mqtt_wifi_secrets.h"   // update with your local settings
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "Streaming.h"
#include <TimeLib.h>
 
#include "BLEDevice.h"


const char* version = "0.6.6";
#define SENSORNAME "bleClient BPM"

const char* state_topic = "sensor/medisa/bu546/state";
const char* set_topic   = "sensor/medisa/bu546/set";
const char* dbg_topic   = "sensor/medisa/bu546/dbg";



// timed_loop
#define INTERVAL_0     50
#define INTERVAL_1   1000
#define INTERVAL_2  30000
#define INTERVAL_3  60000
#define INTERVAL_4 180000

unsigned long time_0 = 0;
unsigned long time_1 = 0;
unsigned long time_2 = 0;
unsigned long time_3 = 0;
unsigned long time_4 = 0;

// debug messages
const int numMsg=80;
int msgCount=0;
String msg=".";
String arrMessages[numMsg];

int clientID = 0;
bool sleepy = false;

WiFiClient espClient;
PubSubClient mqClient(espClient);

/********************************* sleeper *************************************************/

#define uS_TO_S_FACTOR 1000000   /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  30        /* Time ESP32 will go to sleep (in seconds) */
#define TIME_BEFORE_SLEEP  3     /* Time of grace after apparent inaction is stated before sleep is started */
#define Threshold 60             /* sensitivity goes up with value */



/********************************* bleClient **********************************************/
// The remote service we wish to connect to.
static BLEUUID serviceUUID("00001810-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("00002A35-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

/*******************************************************************************/
    
int flag1 ;
int syst  ;
int diast ;
int arter ;
int dyear  ;
int dmonth ;
int dday   ;
int dhour  ;
int dminu  ;
int dsec   ;
int puls  ;
int user  ;
int flag2 ;

/*******************************************************************************/

void setup() {
  Serial.begin(115200);
  
  
  //debug(String(SENSORNAME)+" "+version,true);

  setupWifi(); 
  setupMq();

  setupSleeper() ;
  setupBleClient();
}

void setupSleeper() {
  //Setup interrupt on Touch Pad 3 (GPIO15)
  touchAttachInterrupt(T3, sleeperCallback, Threshold);
  /*
  First we configure the wake up source
  We set our ESP32 to wake up every 5 seconds
  */
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_sleep_enable_touchpad_wakeup();
  
}

void sleeperCallback() {
  // called when touch while awake
}

void getSleepy(){
    // sleepiness
    // deepSleep in 5 sec - transfer is done after ~2 sec
    //debug("sleepiness starts now",true);
    sleepy = true;
    time_2 = millis()-INTERVAL_2+TIME_BEFORE_SLEEP*1000;
    
    Serial << F("\ngetSleepy\n");
}

void loop() {

  mqClient.loop();
  timed_loop();
} 

void timed_loop() {
   if(millis() > time_0 + INTERVAL_0){
    time_0 = millis();

    checkDebug();  
  } 
  
  if(millis() > time_1 + INTERVAL_1){
    time_1 = millis();
    
    bleClient_loop();
    if (!mqClient.connected()) {
      mqConnect();
    }  
  }
   
  if(millis() > time_2 + INTERVAL_2){
    time_2 = millis();

    if (sleepy){
      
      mqClient.disconnect();
      Serial << F("\ndeep_sleep_start\n");
      delay(100);
      Serial.flush(); 
      esp_deep_sleep_start();
      
    }
  }
 
  if(millis() > time_3 + INTERVAL_3){
    time_3 = millis();
     
  }

  if(millis() > time_4 + INTERVAL_4){
    time_4 = millis();
  }
  
}


// reset by melodramatic
void seppuku(String msg){
  debug("about to restart "+msg,true);
  checkDebug();
  delay(50);
  ESP.restart();
}

/******************************* bleClient *******************************************/


/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setupBleClient() {  
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.



void bleClient_loop() {
  Serial.print(F("."));
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    ////String newValue = "Time since boot: " + String(millis()/1000);
    //Serial.println("Setting new characteristic value to \"" + newValue + "\"");
    
    // Set the characteristic's value to be the array of bytes that is actually a string.
    ////pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());

    
  }else if(doScan){
    Serial.println("getScan()->start(0)");
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }

  // if we received data sleepy gets set end notifyCallback
  // if not we want to do it here. sleepy has a grace of ~5sec while connect/notification/transfer takes ~2 sec so it should fit
  if (not sleepy){
    getSleepy();
  }

} 


    
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
    Serial.print("data: ");
    
    // jetzt byte f byte
    for (int i = 0; i < length; i++) {
      Serial.print(pData[i], HEX);
      Serial.print(" "); //separate values with a space
    }

    // aufloesung
    
    flag1 = pData[0];
    syst  = pData[2]*256 + pData[1];
    diast = pData[4]*256 + pData[3];
    arter = pData[6]*256 + pData[5];
    dyear  = pData[8]*256 + pData[7];
    dmonth = pData[9];
    dday   = pData[10];
    dhour  = pData[11];
    dminu  = pData[12];
    dsec   = pData[13];
    puls  = pData[15]*256 + pData[14];
    user  = pData[16];
    flag2 = pData[18]*256 + pData[17];
    
    Serial.println();

    Serial << F("syst:  ") << syst  << F("\n");
    Serial << F("diast: ") << diast << F("\n");  
    Serial << F("arter: ") << arter << F("\n");
    Serial << F("puls:  ") << puls  << F("\n");
    Serial << F("date:  ") << dyear << F("-") << dmonth  << F("-") << dday << F("T") << dhour  << F(":") << dminu << F(":") << dsec << F("\n");
    Serial << F("flag1: ") << flag1 << F("\n");
    Serial << F("flag2: ") << flag2 << F("\n");

    Serial.println();

    sendState();
    doScan = false;
    getSleepy();
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
       
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    // hier ist meins
    
    if(pRemoteCharacteristic->canIndicate()){
      pRemoteCharacteristic->registerForNotify(notifyCallback);
      Serial.println(" - Registered Callback"); 
      const uint8_t indicationOn[] = {0x2, 0x0};
      pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)indicationOn, 2, true);
      Serial.println(" - Enabled Indication");    
    }

    connected = true;
    return true;
}

/********************************** START MQTT CALLBACK*****************************************/
void mqttCallback(char* topic, byte* payload, unsigned int length) {

    
  StaticJsonDocument<256> root;
  deserializeJson(root, payload, length);

  // reset

  if (root.containsKey("seppuku")) {
      String memento = root["seppuku"];
      seppuku(memento);
  }
  
}

String getIsoDate(int Year,int Month,int Day,int Hour,int Minute,int Second){
  String dmonth,dday,dhour,dminute,dsecond;
  
  if (Month<10){
     dmonth = "0"+String(Month);
  }
  else {
     dmonth = String(Month);
  }
  
if (Day<10){
     dday = "0"+String(Day);
  }
  else{
     dday = String(Day);
  }
  
if (Hour<10){
     dhour = "0"+String(Hour);
  }
  else{
     dhour = String(Hour);
  }
  
if (Minute<10){
     dminute = "0"+String(Minute);
  }
  else{
     dminute = String(Minute);
  }
  
if (Second<10){
     dsecond = "0"+String(Second);
  }
  else{
     dsecond = String(Second);
  }

String isoDate = String(Year)+"-"+dmonth+"-"+dday+"T"+dhour+":"+dminute+":"+dsecond;
return isoDate;
}
/********************************** START SEND STATE*****************************************/
void sendState() {
  StaticJsonDocument<256> root;

  String isoDate = getIsoDate(dyear,dmonth,dday,dhour,dminu,dsec) ;
  
  // hour, min, sec, day, month, year
  setTime(dhour, dminu, dsec, dday, dmonth, dyear);
  unsigned long epoch = now();
  Serial << F("") << epoch << F("\n");
   
  root["syst"]    = syst;
  root["diast"]   = diast;
  root["arter"]   = arter;
  root["puls"]    = puls;
  root["date"]    = isoDate;
  root["ts"]      = epoch;
  root["person"]    = user; 
  root["flag1"]   = flag1;
  root["flag2"]   = flag2;

  
  root["id"]   = clientID;
  root["vers"] = version;
   
 
  char buffer[256];
  serializeJson(root, buffer);

  mqClient.publish(state_topic, buffer, true);
  root.clear();
}




/*******************************************************************************/

// send a message to mq
void sendDbg(String msg){
  StaticJsonDocument<256> root;
 
  root["dbg"]=msg;
  

  char buffer[256];
  size_t n = serializeJson(root, buffer);

  mqClient.publish(dbg_topic, buffer, n);
  root.clear();
}

// called out of timed_loop async
void checkDebug(){
  if (msgCount>0){
    
    String message = arrMessages[0];

     for (int i = 0; i < numMsg-1; i++) {
      arrMessages[i]=arrMessages[i+1];
    }
    arrMessages[numMsg-1]="";
    msgCount--;
    sendDbg(message);
  }
  
  
}

// stuff the line into an array. Another function will send it to mq later
void debug(String dbgMsg, boolean withSerial){
  //Serial << "dbgMsg: " << dbgMsg <<  "\n";
  
  if (withSerial) {
    Serial.println( dbgMsg );
  }
  if (msgCount<numMsg){
    arrMessages[msgCount]=dbgMsg;
    msgCount++;
  }
  
}

/*******************************************************************************/

void setupWifi(){
  // Connect to WiFi

  // make sure there is no default AP set up unintended
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, password);
  Serial << ssid << " "<< password<< " ";
 
  while (WiFi.status() != WL_CONNECTED) {
    Serial <<WiFi.status() << " | ";
    delay(500);
  }

  IPAddress ip = WiFi.localIP();
  clientID = ip[3];

  msg = String(SENSORNAME)+" "+version + " - WiFi connected, local IP: "+ip.toString();
  
  debug(msg,true);
  
}


/********************************** START mosq *****************************************/

void setupMq(){
  // pubsub setup
  mqClient.setServer(mqtt_server, mqtt_port);
  mqClient.setCallback(mqttCallback);
  mqConnect();  
}


void mqConnect() {
  // Loop until we're reconnected
  while (!mqClient.connected()) {

    // Attempt to connect
    if (mqClient.connect(SENSORNAME, mqtt_username, mqtt_password)) {
      
      mqClient.subscribe(set_topic);     
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

}
