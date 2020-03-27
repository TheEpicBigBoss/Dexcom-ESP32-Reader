/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharactCommunication = NULL;
BLECharacteristic* pCharactControl = NULL;
BLECharacteristic* pCharactAuthentication = NULL;
BLECharacteristic* pCharactBackfill = NULL;
BLECharacteristic* pCharactXXX = NULL;

BLECharacteristic* pCharactManufacturer = NULL;
BLECharacteristic* pCharactModel = NULL;
BLECharacteristic* pCharactFirmware = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 88;
uint32_t authStage = 0;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "F8083532-849E-531C-C594-30F1F86A4EA5"
#define SERVICE_UUID2          "febc"
#define DEVICEINFORMATION_UUID "180A"

#define communication_UUID  "F8083533-849E-531C-C594-30F1F86A4EA5" //NOTIFY, READ
#define control_UUID        "F8083534-849E-531C-C594-30F1F86A4EA5" //INDICATE, WRITE,   NOTIFY ????
#define authentication_UUID "F8083535-849E-531C-C594-30F1F86A4EA5" //INDICATE, READ, WRITE
#define backfill_UUID       "F8083536-849E-531C-C594-30F1F86A4EA5" //NOTIFY, READ, WRITE
#define xxx_UUID            "F8083537-849E-531C-C594-30F1F86A4EA5" //READ

#define manufacturer_UUID  "2A29"
#define model_UUID         "2A24"
#define Firmware_UUID      "2A26"


class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      Serial.println("callback onConnect");
      deviceConnected = true;
      BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
      Serial.println("callback onDisconnect");
      deviceConnected = false;
    }
};

class MyControlCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("callback control - New value: ");
        for (int i = 0; i < value.length(); i++)
        {
          Serial.print((uint8_t)value[i],HEX);
          Serial.print(" ");
          //Serial.print(value[i]);
        }

        Serial.println();
      }
    }
};
class MyAuthenticationCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("callback authentication - New value: ");
        for (int i = 0; i < value.length(); i++)
        {
          Serial.print((uint8_t)value[i],HEX);
          Serial.print(" ");
        }
        Serial.println();
        if(authStage == 0) //Dexcom Protokoll 1
        {
          //auth-challenge-rx-message.js
          // !!! in xdrip comment out the line 91 in transmitter.js Line 91 (throw new ....) 
          authStage++;
          uint8_t returnval = 0x3;
          pCharactAuthentication->setValue((uint8_t*)&returnval, 17); //Answer has to start with 3 (0x03) and must be 17 character/byte long
          //response is 0-16=17 byte
          //response[0] = 0x3;
          //response[1..8] = tokenhash
          //response[9..16] = challenge
          // tokenhash = calcHash(challenge, id)
          //pCharactAuthentication->setValue("\x03"); //Answer has to start with 3 (0x03) and must be 17 character/byte long
        }
        else  //Dexcom Protokoll 2
        {
          //auth-status-rx-message.js
          //Answer has to start with 5 (0x05 = opCode) and must be 3 character/byte long, second is 
          pCharactAuthentication->setValue("\x05\x01\x02"); //\x02 = bond yes
        }
      }
    }
};
class MyBackfillCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      if (value.length() > 0) {
        Serial.print("callback backfill - New value: ");
        for (int i = 0; i < value.length(); i++)
        {
          Serial.print((uint8_t)value[i],HEX);
          Serial.print(" ");
        }

        Serial.println();
      }
    }
};


void setup() {
  Serial.begin(115200);

  // Create the BLE Device
  BLEDevice::init("DexcomR5");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService1 = pServer->createService(SERVICE_UUID);
  BLEService *pService2 = pServer->createService(DEVICEINFORMATION_UUID);

  // Create a BLE Characteristic
  pCharactCommunication = pService1->createCharacteristic(
                      communication_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
    pCharactControl = pService1->createCharacteristic(
                      control_UUID,
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY | //????
                      BLECharacteristic::PROPERTY_INDICATE
                    );
  pCharactAuthentication = pService1->createCharacteristic(
                      authentication_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
  pCharactBackfill = pService1->createCharacteristic(
                      backfill_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharactXXX = pService1->createCharacteristic(
                      xxx_UUID,
                      BLECharacteristic::PROPERTY_READ
                    );

  pCharactManufacturer = pService2->createCharacteristic(
                      manufacturer_UUID,
                      BLECharacteristic::PROPERTY_READ
                    );
  pCharactModel = pService2->createCharacteristic(
                      model_UUID,
                      BLECharacteristic::PROPERTY_READ
                    );
  pCharactFirmware = pService2->createCharacteristic(
                      Firmware_UUID,
                      BLECharacteristic::PROPERTY_READ
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharactCommunication->addDescriptor(new BLE2902());
  pCharactControl->addDescriptor(new BLE2902());
  pCharactAuthentication->addDescriptor(new BLE2902());
  pCharactBackfill->addDescriptor(new BLE2902());
  //not pCharactXXX
  
  pCharactControl->setCallbacks(new MyControlCallback());
  pCharactAuthentication->setCallbacks(new MyAuthenticationCallback());
  pCharactBackfill->setCallbacks(new MyBackfillCallback());

  //const uint8_t indicationOn[] = {0x2, 0x0};
  //pCharactControl->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)indicationOn, 2, true);
  
  pCharactCommunication->setValue("AA");
  pCharactAuthentication->setValue("BB");
  pCharactBackfill->setValue("CC");
  pCharactXXX->setValue("DD");
  
  pCharactManufacturer->setValue("Hello World says Neil1");
  pCharactModel->setValue("Hello");
  pCharactFirmware->setValue("World");
  // Start the service
  pService1->start();
  pService2->start();


  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID2); //advertise febc to get find by xdrip
  //pAdvertising->addServiceUUID(DEVICEINFORMATION_UUID); do not advertise for device informations
  pAdvertising->setScanResponse(false); //true??
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a dexcom client connection to notify...");
}

void loop() {
    // notify changed value
    if (deviceConnected) {
        pCharactCommunication->setValue((uint8_t*)&value, 4);
        pCharactCommunication->notify();
        pCharactBackfill->notify();
        pCharactControl->setValue((uint8_t*)&value, 4);
        pCharactControl->notify(); //????
        pCharactControl->indicate(); //????
        value++;
        delay(100); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        Serial.println("loop disconnecting");
        authStage = 0; //Resetting so that next client gets first 3 and then 5
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("loop disconnecting start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        Serial.println("loop connecting");
        oldDeviceConnected = deviceConnected;
    }
}
