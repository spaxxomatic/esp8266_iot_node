/*********
  Rui Santos
  Complete instructions at https://RandomNerdTutorials.com/esp32-ble-server-client/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*********/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <Arduino.h>
#include "btle_conn.h"
//Default Temperature is in Celsius
//Comment the next line for Temperature in Fahrenheit
#define temperatureCelsius

//BLE server name
#define bleServerName "BME280_ESP32"

float temp;
float hum;

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 2000;

bool deviceConnected = false;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"

// Temperature Characteristic and Descriptor

BLECharacteristic bmeTemperatureCelsiusCharacteristics("cba1d466-344c-4be3-ab3f-189f80dd7518", BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor bmeTemperatureCelsiusDescriptor(BLEUUID((uint16_t)0x2902));

BLEServer* pServer;
BLEService* bmeService ;
//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("Connect ");
    deviceConnected = true;
  };
  void onDisconnect(BLEServer* pServer) {
    Serial.println("Disconnect ");
    deviceConnected = false;
  }
};

bool bBtEnabled = false;

void enable_ble() {
  if (bBtEnabled){
    Serial.println("Already on");
    return;
  }
  
  // Create the BLE Device
  Serial.println("Dev init");
  yield();
  BLEDevice::init(bleServerName);

  // Create the BLE Server
  Serial.println("Create serv");
  yield();
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  bmeService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristics and Create a BLE Descriptor
  bmeService->addCharacteristic(&bmeTemperatureCelsiusCharacteristics);
  bmeTemperatureCelsiusDescriptor.setValue("BME temperature Celsius");
  bmeTemperatureCelsiusCharacteristics.addDescriptor(&bmeTemperatureCelsiusDescriptor);
    // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  Serial.println("Service start");
  yield();
  bmeService->start();
  // Start the service
  Serial.println("Start adv");
  yield();
  pServer->getAdvertising()->start();
  Serial.println("Waiting for client conn...");
  yield();
  bBtEnabled = true;
}

void disable_ble(){  
  if (! bBtEnabled) {
    Serial.println("Already disabled");
    return ;
  }
  std::map<uint16_t, conn_status_t> p_connectedServersMap = pServer->getPeerDevices(true);
  Serial.printf("%i devices will lose conn\n", p_connectedServersMap.size());   
  
  pServer->getAdvertising()->stop();
  
  bmeService->stop();    
  pServer->removeService(bmeService);
  BLEDevice::deinit(false);
  bBtEnabled = false;

}

void loop_ble() {
  if (deviceConnected) {
    if ((millis() - lastTime) > timerDelay) {
      // Read temperature as Celsius (the default)      
      
      //Notify temperature reading from BME sensor
        static char temperatureCTemp[6];
        temp = -20;
        //Set temperature Characteristic value and notify connected client
        bmeTemperatureCelsiusCharacteristics.setValue(temperatureCTemp);
        bmeTemperatureCelsiusCharacteristics.notify();
        Serial.print("BT notify ");
        Serial.println(temp);        
        lastTime = millis();
    }
  }
}