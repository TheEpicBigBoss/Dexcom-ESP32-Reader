/**
 * A ESP32 BLE client that can read (glucose, raw, ..) data from the dexcom G6 (G5) transmitter.
 * 
 * Developed in the context of my bachelor thesis at the Technical University (TU) of Darmstadt 
 * under the supervision of the Telecooperation Lab.
 * 
 * Specifications Hardware / Software:
 * - ESP32-WROOM-32D (ESP32_DevKitc_V4)
 * - espressif v1.0.4  (https://dl.espressif.com/dl/package_esp32_index.json)
 * - Arduino Studio 1.8.10
 * - Dexcom G6 Transmitter 81xxxx (Model: SW11163, Firmware: 1.6.5.25 / 1.6.13.139)
 * 
 * Author: Max Kaiser
 * Copyright (c) 2020
 * 24.03.2020
 */

#include "BLEDevice.h"
#include "BLEScan.h"
#include "Output.h"

#define STATE_START_SCAN 0                                                                                              // Set this state to start the scan.
#define STATE_SCANNING   1                                                                                              // Indicates the esp is currently scanning for devices.
#define STATE_SLEEP      2                                                                                              // Finished with reading data from the transmitter.
static int Status      = 0;                                                                                             // Set to 0 to automatically start scanning when esp has started.

// The remote service we wish to connect to.
static BLEUUID    serviceUUID("f8083532-849e-531c-c594-30f1f86a4ea5");                                                  // This service holds all the important characteristics.
static BLEUUID advServiceUUID("0000febc-0000-1000-8000-00805f9b34fb");                                                  // This service gets advertised by the transmitter.
static BLEUUID deviceInformationServiceUUID("180A");                                                                    // The default service for the general device informations.
// The characteristic of the remote serviceUUID service we are interested in.
static BLEUUID  communicationUUID("F8083533-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ
static BLEUUID        controlUUID("F8083534-849E-531C-C594-30F1F86A4EA5"); // INDICATE, WRITE
static BLEUUID authenticationUUID("F8083535-849E-531C-C594-30F1F86A4EA5"); // INDICATE, READ, WRITE (G6 Plus INDICATE / WRITE)
static BLEUUID       backfillUUID("F8083536-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ, WRITE (G6 Plus NOTIFY)
//static BLEUUID          xxxUUID("F8083537-849E-531C-C594-30F1F86A4EA5"); // READ
//static BLEUUID          yyyUUID("F8083538-849E-531C-C594-30F1F86A4EA5"); // NOTIFY, READ (G6 Plus only)
// The general characteristic of the device information service.
static BLEUUID manufacturerUUID("2A29"); // READ
static BLEUUID        modelUUID("2A24"); // READ
static BLEUUID     firmwareUUID("2A26"); // READ


static std::string transmitterID = "***REMOVED***";              /* Set here your transmitter ID */                            // This transmitter ID is used to identify our transmitter if multiple dexcom transmitters are found.
static boolean useAlternativeChannel = true;      /* Enable when used concurrently with xDrip / Dexcom CGM */           // Tells the transmitter to use the alternative bt channel.
static volatile boolean connected = false;                                                                              // Indicates if the ble client is connected to the transmitter.
static boolean bonding = false;                                                                                         // Gets set by Auth handshake "StatusRXMessage" and shows if the transmitter would like to bond with the client.

// Shared variables (used in the callbacks)
static std::string AuthCallbackResponse = "";
static std::string ControlCallbackResponse = "";
// Use "volatile" so that the compiler does not optimise code related 
// with this variable and delete the empty while loop which is used as a barrier.
static volatile boolean bondingFinished = false;                                                                        // Get set when the bonding has finished, does not indicates if it was successfull.

static BLERemoteCharacteristic* pRemoteCommunication;
static BLERemoteCharacteristic* pRemoteControl;
static BLERemoteCharacteristic* pRemoteAuthentication;
static BLERemoteCharacteristic* pRemoteBackfill;
static BLERemoteCharacteristic* pRemoteManufacturer;                                                                    // Uses deviceInformationServiceUUID
static BLERemoteCharacteristic* pRemoteModel;                                                                           // Uses deviceInformationServiceUUID
static BLERemoteCharacteristic* pRemoteFirmware;                                                                        // Uses deviceInformationServiceUUID
static BLEAdvertisedDevice* myDevice = NULL;                                                                            // The remote device (transmitter) found by the scan and set by scan callback function.

/**
 * Callback for the connection.
 */
class MyClientCallback : public BLEClientCallbacks
{
    void onConnect(BLEClient* pclient) 
    {
        SerialPrintln(DATA, "onConnect");
        connected = true;
    }
    void onDisconnect(BLEClient* pclient) 
    {
        SerialPrintln(DATA, "onDisconnect");
        connected = false;
    }
};

/**
 * Callback class for the secure bonding process.
 * The transmitter will request / initiate the bonding.
 */
class MySecurity : public BLESecurityCallbacks 
{
    uint32_t onPassKeyRequest()
    {
        return 123456;
    }
    void onPassKeyNotify(uint32_t pass_key) {}
    bool onConfirmPIN(uint32_t pass_key)
    {
        return true;
    }
    bool onSecurityRequest()
    {
        return true;
    }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl)                                                        // This function is the only one that gets currently triggered.
    {
        SerialPrint(DEBUG, "pair status = ");
        SerialPrintln(DEBUG, auth_cmpl.success ? "success" : "fail");
        bondingFinished = true;                                                                                         // Finished with bonding.
        SerialPrintln(DEBUG, "onAuthenticationComplete : finished with bonding.");
    }
};

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 * Also check that the transmitter has the ID in the bluetooth name so that we connect only to this 
 * dexcom transmitter (if multiple are around).
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks 
{
    void onResult(BLEAdvertisedDevice advertisedDevice)                                                                 // Called for each advertising BLE server.
    {
        SerialPrint(DEBUG, "BLE Advertised Device found: ");
        SerialPrintln(DEBUG, advertisedDevice.toString().c_str());

        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(advServiceUUID) &&              // If the advertised service is the dexcom advertise service (not the main service that contains the characteristics).
            advertisedDevice.haveName() && advertisedDevice.getName() == ("Dexcom" + transmitterID.substr(4,2)))
        {
            BLEDevice::getScan()->stop();                                                                               // We found our transmitter so stop scanning for now.
            myDevice = new BLEAdvertisedDevice(advertisedDevice);                                                       // Save device as new copy, myDevice also triggers a state change in main loop.
        }
    } 
};

/**
 * The different callbacks for notify and indicate if new data from the transmitter is available.
 */ 
static void notifyCommunicationCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrintf(DEBUG, "notifyCommunicationCallback - read %d byte data: ", length);
    printHexArray(pData, length);
}
static void indicateControlCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrintf(DEBUG, "indicateControlCallback - read %d byte data: ", length);
    printHexArray(pData, length);
    ControlCallbackResponse = uint8ToString(pData, length);
}
static void indicateAuthCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrintf(DEBUG, "indicateAuthCallback - read %d byte data: ", length);
    printHexArray(pData, length);
    AuthCallbackResponse = uint8ToString(pData, length);
}
static void notifyBackfillCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) 
{
    SerialPrintf(DEBUG, "notifyBackfillCallback - read %d byte data: ", length);
    printHexArray(pData, length);
}

/**
 * Connects to the transmitter found by the scan and get services / characteristics.
 * Return false if an error occurred.
 */
bool connectToTransmitter() 
{
    SerialPrint(DEBUG, "Forming a connection to ");
    SerialPrintln(DEBUG, myDevice->getAddress().toString().c_str());
    
    BLEClient* pClient = BLEDevice::createClient();                                                                     // We specify the security settings later after we have successful authorised with the transmitter.
    SerialPrintln(DEBUG, " - Created client");

    pClient->setClientCallbacks(new MyClientCallback());                                                                // Callbacks for onConnect() onDisconnect()

    // Connect to the remove BLE Server.
    if(!pClient->connect(myDevice))                                                                                     // Notice from the example: if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
        return false;
    
    SerialPrintln(DEBUG, " - Connected to server");

    // Obtain a reference to the service.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) 
    {
        SerialPrint(ERROR, "Failed to find our service UUID: ");
        SerialPrintln(ERROR, serviceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    BLERemoteService* pRemoteServiceInfos = pClient->getService(deviceInformationServiceUUID);
    if (pRemoteServiceInfos == nullptr)
    {
        SerialPrint(ERROR, "Failed to find our service UUID: ");
        SerialPrintln(ERROR, deviceInformationServiceUUID.toString().c_str());
        pClient->disconnect();
        return false;
    }
    SerialPrintln(DEBUG, " - Found our services");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    getCharacteristic(&pRemoteCommunication, pRemoteService, communicationUUID);
    getCharacteristic(&pRemoteControl, pRemoteService, controlUUID);
    getCharacteristic(&pRemoteAuthentication, pRemoteService, authenticationUUID);
    getCharacteristic(&pRemoteBackfill, pRemoteService, backfillUUID);

    getCharacteristic(&pRemoteManufacturer, pRemoteServiceInfos, manufacturerUUID);
    getCharacteristic(&pRemoteModel, pRemoteServiceInfos, modelUUID);
    getCharacteristic(&pRemoteFirmware, pRemoteServiceInfos, firmwareUUID);
    SerialPrintln(DEBUG, " - Found our characteristics");
    

    if (pRemoteAuthentication->canNotify())
        SerialPrintln(DEBUG, "Can Notify on Auth.");
    else
        SerialPrintln(ERROR, "Can NOT Notify on Auth.");
    
    if (pRemoteAuthentication->canIndicate())
        SerialPrintln(DEBUG, "Can Indicate on Auth.");
    else
        SerialPrintln(ERROR, "Can NOT Indicate on Auth.");


    const uint8_t bothOn2[]         = {0x3, 0x0};
    pRemoteAuthentication->registerForNotify(indicateAuthCallback, false);
    pRemoteAuthentication->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)bothOn2, 2, true);
    //registerForIndication(indicateAuthCallback, pRemoteAuthentication);                                                 // We only register for the Auth characteristic. When we are authorised we can register for the other characteristics.

    //Vielleicht mus bei neurern transmitter schon am anfang alle Chracteristics abboniert werden
    return true;
}

/**
 * Reads the device informations which are not dexcom specific.
 */
bool readDeviceInformations()
{
    if(!pRemoteManufacturer->canRead())                                                                                 // Check if the characteristic is readable.
        return false;
    SerialPrint(DEBUG, "The Manufacturer value was: ");
    SerialPrintln(DEBUG, pRemoteManufacturer->readValue().c_str());                                                     // Read the value of the device information characteristics.
    
    if(!pRemoteModel->canRead())
        return false;
    SerialPrint(DEBUG, "The Model value was: ");
    SerialPrintln(DEBUG, pRemoteModel->readValue().c_str());
    
    if(!pRemoteFirmware->canRead())
        return false;
    SerialPrint(DEBUG, "The Firmware value was: ");
    SerialPrintln(DEBUG, pRemoteFirmware->readValue().c_str());
    return true;
}

/**
 * Method to check the reason the ESP woke up or was started.
 */
void wakeUpRoutine() 
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch(wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_TIMER : 
            SerialPrintln(DEBUG, "Wakeup caused by timer from hibernation.");                                           // No need to restart because all memory is lost after hibernation (bot not after deep sleep).
            //ESP.restart();                                                                                            // Restart the ESP, this will forget every ble state and bonding informations.
            break;
        default : 
            //Print all saved values
            SerialPrintln(DEBUG, "Wakeup was not caused by deep sleep (normal start)."); 
            break;
    }
}

/**
 * Set up the ESP32 ble.
 */
void setup() 
{
    Serial.begin(115200);
    wakeUpRoutine();
    SerialPrintln(DEBUG, "Starting ESP32 dexcom client application...");
    BLEDevice::init("");

    BLEScan* pBLEScan = BLEDevice::getScan();                                                                           // Retrieve a Scanner.
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());                                          // Set the callback to informed when a new device was detected.
    pBLEScan->setInterval(100); //100 works                                                                             // The time in ms how long each search intervall last. Important for fast scanning so we dont miss the transmitter waking up.
    pBLEScan->setWindow(99); //60 works                                                                                 // The actual time that will be searched. Interval - Window = time the esp is doing nothing (used for energy efficiency).
    pBLEScan->setActiveScan(false);                                                                                     // Possible source of error if we cant connect to the transmitter.
}

/**
 * This function can be called in an error case to exit and go to sleep.
 */
void ExitState(std::string message)
{
    SerialPrintln(ERROR, message.c_str());
    sleepHibernation();                                                                                                 // Exit by going into deep sleep.
}
/**
 * This method will perform a full transmitter connect and read data.
 */
bool run()
{
    if (!connectToTransmitter())                                                                                        // Connect to the found transmitter.
        ExitState("We have failed to connect to the transmitter!");
    else
        SerialPrintln(DEBUG, "We are now connected to the transmitter.");

    if(!readDeviceInformations())                                                                                       // Read the general device informations like model no. and manufacturer.
        SerialPrintln(DEBUG, "Error while reading device informations!");

    if(!authenticate())                                                                                                 // Authenticate with the transmitter.
        ExitState("Error while trying to authenticate!");

    if(!requestBond())                                                                                                  // Enable encryption and requesting bonding.
        ExitState("Error while trying to bond!");

    registerForIndication(indicateControlCallback, pRemoteControl);                                                     // Now register to receive new data on the control characteristic.

    //Reading Time
    if(!readTimeMessage())
        SerialPrintln(DEBUG, "Error reading Time Message!");
    
    if(!readBatteryStatus())
        SerialPrintln(DEBUG, "Can't read Battery Status!");

    //Read glucose values
    if(!readGlucose())
        SerialPrintln(DEBUG, "Can't read Glucose!");

    if(!readSensor())
        SerialPrintln(DEBUG, "Can't read raw Sensor values!");

    if(!readLastCalibration())
        SerialPrintln(DEBUG, "Can't read last calibration data!");

    //Let the Transmitter close the connection.
    sendDisconnect();
}

/**
 * This function sets the ESP to hibernation mode with the lowest power consumption of ~5uA.
 */
void sleepHibernation()
{
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    esp_sleep_enable_timer_wakeup(270 * 1000000);                                                                       // Sleep for 4.5 minutes.
    SerialPrintln(DEBUG, "Going to hibernation sleep mode now.");
    Serial.flush();
    esp_deep_sleep_start();                                                                                             // Go to sleep.
}

/**
 * This is the main loop function.
 */
void loop() 
{
    switch (Status)
    {
        case STATE_START_SCAN:
          BLEDevice::getScan()->start(0, true);                                                                         // true = continuous scanning?
          Status = STATE_SCANNING;
          break;

        case STATE_SCANNING:
          if(myDevice != NULL)                                                                                          // A device (transmitter) was found by the scan (callback).
            run();                                                                                                      // This function is blocking until all tansmitter communication has finished.
          Status = STATE_SLEEP;
          break;

        case STATE_SLEEP:
          //Goto Sleep
          sleepHibernation();                                                                                           // Goto Hiberation (more power efficient that deep sleep)
        break;
    }
}
