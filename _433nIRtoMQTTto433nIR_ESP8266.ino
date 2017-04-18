/*
  433nIRtoMQTTto433nIR  - ESP8266 program for home automation 
  Tested OK on GeekCreek ESP12F
  Not working on NodeMCU V0.9
   Act as a wifi gateway between your 433mhz/infrared IR signal  and a MQTT broker 
   Send and receiving command by MQTT
 
  This program enables to:
 - receive MQTT data from a topic and send RF 433Mhz signal corresponding to the received MQTT data
 - publish MQTT data to a different topic related to received 433Mhz signal
 - receive MQTT data from a topic and send IR signal corresponding to the received MQTT data
 - publish MQTT data to a different topic related to received IR signal
 
  Contributors:
  - 1technophile
  - crankyoldgit
  Based on:
  - MQTT library (https://github.com/knolleary)
  - RCSwitch (https://github.com/sui77/rc-switch)
  - ESP8266Wifi
  - IRremoteESP8266 (https://github.com/markszabo/IRremoteESP8266)
  
  Project home: https://github.com/1technophile/433nIRtoMQTTto433nIR_ESP8266
  Blog, tutorial: http://1technophile.blogspot.com/2016/09/433nIRtomqttto433nIR-bidirectional-esp8266.html
Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
and associated documentation files (the "Software"), to deal in the Software without restriction, 
including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, 
subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
Some usefull commands to test gateway with mosquitto:
Subscribe to the subject for data receiption from RF signal
mosquitto_sub -t home/433toMQTT
Send data by MQTT to convert it on RF signal
mosquitto_pub -t home/MQTTto433/ -m 1315153
*/
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <RCSwitch.h> // library for controling Radio frequency switch
#include <SoftwareSerial.h>

int incomingByte = 0;// for incoming serial data
int outByte = 0;

// software serial : TX = digital pin 7, RX = digital pin 8
SoftwareSerial portOne(14, 12);


RCSwitch mySwitch = RCSwitch();

//Do we want to see trace for debugging purposes
#define TRACE 1  // 0= trace off 1 = trace on

// Update these with values suitable for your network.
#define wifi_ssid "SSID"
#define wifi_password "password"
#define mqtt_server "x.x.x.x"
#define mqtt_user "your_username" // not compulsory if you set it uncomment line 143 and comment line 145
#define mqtt_password "your_password" // not compulsory if you set it uncomment line 143 and comment line 145

//variables to avoid duplicates for RF
#define time_avoid_duplicate 3000 // if you want to avoid duplicate mqtt message received set this to > 0, the value is the time in milliseconds during which we don't publish duplicates
// array to store previous received RFs codes and their timestamps
long ReceivedRF[10][2] ={{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}};

#define subjectMQTTtoX "home/commands/#"
//RF MQTT Subjects
#define subject433toMQTT "home/433toMQTT"
#define subjectMQTTto433 "home/commands/MQTTto433"
//IR MQTT Subjects
#define subjectIRtoMQTT "home/sensors/ir"
#define subjectMQTTtoIR "home/commands/MQTTtoIR"

//adding this to bypass to problem of the arduino builder issue 50
void callback(char*topic, byte* payload,unsigned int length);
WiFiClient espClient;

// client parameters
PubSubClient client(mqtt_server, 1883, callback, espClient);

//MQTT last attemps reconnection number
long lastReconnectAttempt = 0;

//Light Reading**********************************************
unsigned long previousLightMillis = 0;
const int lightInterval = 120000;
//Temp Sensor************************************************
#include <dht.h>
dht DHT;
#define DHT11_PIN 12
int inPin = 12;


// Callback function, when the gateway receive an MQTT value on the topics subscribed this function is called
void callback(char* topic, byte* payload, unsigned int length) {
  // In order to republish this payload, a copy must be made
  // as the orignal payload buffer will be overwritten whilst
  // constructing the PUBLISH packet.
  trc("Hey I got a callback ");
  // Allocate the correct amount of memory for the payload copy
  byte* p = (byte*)malloc(length + 1);
  // Copy the payload to the new buffer
  memcpy(p,payload,length);
  
  // Conversion to a printable string
  p[length] = '\0';
  String callbackstring = String((char *) p);
  String topicNameRec = String((char*) topic);
  
  //launch the function to treat received data
  receivingMQTT(topicNameRec,callbackstring);

  // Free the memory
  free(p);
}

void setup()
{
  //Launch serial for debugging purposes
  Serial.begin(9600);
  
  // Start IR software serial ports
  portOne.begin(9600);
  
  //Begining wifi connection
  setup_wifi();
  delay(1500);
  
  lastReconnectAttempt = 0;

  //RF init parameters
  mySwitch.enableTransmit(4); // RF Transmitter is connected to Pin D2 
  mySwitch.setRepeatTransmit(20); //increase transmit repeat to avoid lost of rf sendings
  mySwitch.enableReceive(5);  // Receiver on pin D1

}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  trc("Connecting to ");
  trc(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    trc(".");
  }
  trc("WiFi connected");
}

boolean reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    trc("Attempting MQTT connection...");
    // Attempt to connect
    // If you  want to use a username and password, uncomment next line and comment the line if (client.connect("433toMQTTto433")) {
    //if (client.connect("433nIRtoMQTTto433nIR", mqtt_user, mqtt_password)) {
    // and set username and password at the program beginning
    if (client.connect("433nIRtoMQTTto433nIR")) {
    // Once connected, publish an announcement...
      client.publish("outTopic","hello world");
      trc("connected");
    //Subscribing to topic(s)
    subscribing(subjectMQTTtoX);
    } else {
      trc("failed, rc=");
      trc(String(client.state()));
      trc(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  return client.connected();
}

void loop()
{
  
  //Start Extra Sensors
  extraSensor();  
  
  //MQTT client connexion management
  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      trc("client mqtt not connected, trying to connect");
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    // MQTT loop
    client.loop();
  }

  // Receive loop, if data received by RF433 send it by MQTT to subject433toMQTT
  if (mySwitch.available()) {
    // Topic on which we will send data
    trc("Receiving 433Mhz signal");
    unsigned long MQTTvalue;
    MQTTvalue=mySwitch.getReceivedValue();  
    mySwitch.resetAvailable();
    if (client.connected()) {
      if (!isAduplicate(MQTTvalue)) {// conditions to avoid duplications of RF -->MQTT
          trc("Sending 433Mhz signal to MQTT");
          trc(String(MQTTvalue));
          sendMQTT(subject433toMQTT,String(MQTTvalue));
          storeValue(MQTTvalue);
      }         
    } else {
      if (reconnect()) {
        trc("Sending 433Mhz signal to MQTT after reconnect");
        trc(String(MQTTvalue));
        sendMQTT(subject433toMQTT,String(MQTTvalue));
        storeValue(MQTTvalue);
        lastReconnectAttempt = 0;
      }
    }
  }


  delay(100);
  
//begining of IR
if (portOne.available() > 0) {
delay(1);
int my_in_bytes[3]={0, 0, 0};

  for (int i=0; i <= 2; i++){
    incomingByte = portOne.read();
    Serial.print("I received: ");
    Serial.println (String (incomingByte, HEX));
    my_in_bytes [i] = incomingByte;
  }
String IRvalue;
  for (int i=0; i <= 2; i++){
    Serial.print(String (my_in_bytes [i], HEX) + "," );
    IRvalue = IRvalue + (String (my_in_bytes [i], HEX) + "," );
  }
    Serial.println("");
    sendMQTT(subjectIRtoMQTT,String(IRvalue));
  }
  

//end of IR
}

void storeValue(long MQTTvalue){
    long now = millis();
    // find oldest value of the buffer
    int o = getMin();
    trc("Minimum index: " + String(o));
    // replace it by the new one
    ReceivedRF[o][0] = MQTTvalue;
    ReceivedRF[o][1] = now;
    trc("send this code :" + String(ReceivedRF[o][0])+"/"+String(ReceivedRF[o][1]));
    trc("Col: value/timestamp");
    for (int i = 0; i < 10; i++)
    {
      trc(String(i) + ":" + String(ReceivedRF[i][0])+"/"+String(ReceivedRF[i][1]));
    }
}

int getMin(){
  int minimum = ReceivedRF[0][1];
  int minindex=0;
  for (int i = 0; i < 10; i++)
  {
    if (ReceivedRF[i][1] < minimum) {
      minimum = ReceivedRF[i][1];
      minindex = i;
    }
  }
  return minindex;
}

boolean isAduplicate(long value){
trc("isAduplicate");
// check if the value has been already sent during the last "time_avoid_duplicate"
for (int i=0; i<10;i++){
 if (ReceivedRF[i][0] == value){
      long now = millis();
      if (now - ReceivedRF[i][1] < time_avoid_duplicate){
      trc("don't send this code :" + String(ReceivedRF[i][0])+"/"+String(ReceivedRF[i][1]));
      return true;
    }
  }
}
return false;
}

void subscribing(String topicNameRec){ // MQTT subscribing to topic
  char topicStrRec[26];
  topicNameRec.toCharArray(topicStrRec,26);
  // subscription to topic for receiving data
  boolean pubresult = client.subscribe(topicStrRec);
  if (pubresult) {
    trc("subscription OK to");
    trc(topicNameRec);
  }
}

void receivingMQTT(String topicNameRec, String callbackstring) {
  trc("Receiving data by MQTT");
  trc(topicNameRec);
  char topicOri[26] = "";
  char topicStrAck[26] = "";
  char datacallback[32] = "";
  // Acknowledgement inside a subtopic to avoid loop
  topicNameRec.toCharArray(topicOri,26);
  char DataAck[26] = "OK";
  client.publish("home/ack", DataAck);
  callbackstring.toCharArray(datacallback,32);
  trc(datacallback);
  unsigned long data = strtoul(datacallback, NULL, 10); // we will not be able to pass value > 4294967295
  trc(String(data)); 
       
    if (topicNameRec == subjectMQTTto433){
      trc("Send received data by RF 433");
      //send received MQTT value by RF signal (example of signal sent data = 5264660)
      mySwitch.send(data, 24);
    }

//IR Recieved
  if (topicNameRec = subjectMQTTtoIR){
  char input1[3];
  char input2[3];
  char input3[3];
  int val1;
  int val2;
  int val3;
   
    uint8_t my_out_bytes[5]={0xA1, 0xF1, 0, 0, 0};

    int commaIndex = callbackstring.indexOf(',');
    //  Search for the next comma just after the first
    int secondCommaIndex = callbackstring.indexOf(',', commaIndex + 1);
    
    String firstValue = callbackstring.substring(0, commaIndex);
    String secondValue = callbackstring.substring(commaIndex + 1, secondCommaIndex);
    String thirdValue = callbackstring.substring(secondCommaIndex + 1); // To the end of the string
    firstValue.toCharArray(input1, 4);
    val1 = StrToHex(input1);
    my_out_bytes [2] = val1;
    secondValue.toCharArray(input2, 4);
    val2 = StrToHex(input2);
    my_out_bytes [3] = val2;
    thirdValue.toCharArray(input3, 4);
    val3 = StrToHex(input3);
    my_out_bytes [4] = val3;

    for (int i=0; i <= 4; i++){
    Serial.print(String (my_out_bytes [i], HEX) + "," );
    }
    portOne.write(my_out_bytes,sizeof(my_out_bytes));
    Serial.println("");

  }

}

//send MQTT data dataStr to topic topicNameSend
void sendMQTT(String topicNameSend, String dataStr){

    char topicStrSend[26];
    topicNameSend.toCharArray(topicStrSend,26);
    char dataStrSend[200];
    dataStr.toCharArray(dataStrSend,200);
    boolean pubresult = client.publish(topicStrSend,dataStrSend);
    trc("sending ");
    trc(dataStr);
    trc("to ");
    trc(topicNameSend);

}

//trace
void trc(String msg){
  if (TRACE) {
  Serial.println(msg);
  }
}

//More Sensors

void extraSensor() {

if (millis() - previousLightMillis >= lightInterval) {

  //Read Analog Light Sensor
  int sensorValue = analogRead(A0);
  Serial.println(sensorValue);
  previousLightMillis = millis();
  String lighttopic = "home/Light";
  sendMQTT(lighttopic,String(sensorValue));
  //Read DHT Sensor
  int chk = DHT.read11(DHT11_PIN);
  float temp = DHT.temperature;
  Serial.print("Temperature = ");
  Serial.println(1.8*temp+32);
  Serial.print("Humidity = ");
  Serial.println(DHT.humidity);
  String temptopic = "home/DHT/temp";
  sendMQTT(temptopic,String(1.8*temp+32));
  String humiditytopic = "home/DHT/humidity";
  sendMQTT(humiditytopic,String(DHT.humidity));
}

}

//IR String to Hex Conversion

int StrToHex(char str[])
{
  return (int) strtol(str, 0, 16);
}

