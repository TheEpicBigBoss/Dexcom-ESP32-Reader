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
 * 12.04.2020
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


static std::string transmitterID = "xxxxxx";              /* Set here your transmitter ID */                            // This transmitter ID is used to identify our transmitter if multiple dexcom transmitters are found.
static boolean useAlternativeChannel = true;      /* Enable when used concurrently with xDrip / Dexcom CGM */           // Tells the transmitter to use the alternative bt channel.
static boolean bonding = false;                                                                                         // Gets set by Auth handshake "StatusRXMessage" and shows if the transmitter would like to bond with the client.
static boolean force_rebonding = false;               /* Enable when problems with connecting */                        // When true: disables bonding before auth handshake. Enables bonding after successful authenticated (and before bonding command) so transmitter then can initiate bonding.
/* Optimization or connecting problems: 
 * - pBLEScan->setInterval(100);             10-500 and > setWindow(..)
 * - pBLEScan->setWindow(99);                10-500 and < setInterval(..)
 * - pBLEScan->setActiveScan(false);         true, false
 * - BLEDevice::getScan()->start(0, true)    true, false */


// Variables which survives the deep sleep. Uses RTC_SLOW memory.
#define saveLastXValues 12                                                                                              // This saves the last x glucose levels by requesting them through the backfill request.
RTC_SLOW_ATTR static uint16_t glucoseValues[saveLastXValues] = {0};                                                     // Reserve space for 1 hour a 5 min resolution of glucose values.
RTC_SLOW_ATTR static boolean error_last_connection = false;
static boolean error_current_connection = false;                                                                        // To detect an error in the current session.

// Shared variables (used in the callbacks)
static volatile boolean connected = false;                                                                              // Indicates if the ble client is connected to the transmitter. Used to detect a transmitter timeout.
static std::string AuthCallbackResponse = "";
static std::string ControlCallbackResponse = "";
// Use "volatile" so that the compiler does not optimise code related with
// this variable and delete the empty while loop which is used as a barrier.
static volatile boolean bondingFinished = false;                                                                        // Get set when the bonding has finished, does not indicates if it was successful.

static BLERemoteCharacteristic* pRemoteCommunication;
static BLERemoteCharacteristic* pRemoteControl;
static BLERemoteCharacteristic* pRemoteAuthentication;
static BLERemoteCharacteristic* pRemoteBackfill;
static BLERemoteCharacteristic* pRemoteManufacturer;                                                                    // Uses deviceInformationServiceUUID
static BLERemoteCharacteristic* pRemoteModel;                                                                           // Uses deviceInformationServiceUUID
static BLERemoteCharacteristic* pRemoteFirmware;                                                                        // Uses deviceInformationServiceUUID
static BLEAdvertisedDevice* myDevice = NULL;                                                                            // The remote device (transmitter) found by the scan and set by scan callback function.
static BLEClient* pClient = NULL;                                                                                       // Is global so we can disconnect everywhere when an error occured.

/**
 * Callback for the connection.
 */
class MyClientCallback : public BLEClientCallbacks
{
    /**
     * This function sets the ESP to hibernation mode with the lowest power consumption of ~10uA.
     */
    void sleepHibernation()
    {
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);                                              // Keep the RTC slow memory on to save the last glucose values and last error variable.
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);                                              // Could be turned off in later esp chips but is on to not trigger this bug https://forum.mongoose-os.com/discussion/1628/tg0wdt-sys-reset-immediately-after-waking-from-deep-sleep
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
        esp_sleep_enable_timer_wakeup(270 * 1000000);                                                                   // Sleep for 4.5 minutes.
        SerialPrintln(DEBUG, "Going back to hibernation sleep mode now.");
        Serial.flush();
        esp_deep_sleep_start();                                                                                         // Go to sleep.
    }
    void onConnect(BLEClient* bleClient) 
    {
        SerialPrintln(DATA, "onConnect");
        connected = true;
    }
    void onDisconnect(BLEClient* bleClient)
    {
        SerialPrintln(DATA, "onDisconnect");                                                                            // Go on the onDisconnect event directly into deep sleep -> So we can go to the deep sleep even when
        error_last_connection = error_current_connection;                                                               // Save if there was an error in this session.
        sleepHibernation();                                                                                             // the chracacteristic->write() freezes (happens with low battery transmitters A:<218 B:<172 R:>4454 ).
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
        SerialPrintln(DEBUG, "onAuthenticationComplete : finished with bonding.");
        bondingFinished = true;                                                                                         // Finished with bonding.
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
    if(!saveBackfill(uint8ToString(pData, length)))
    {
        SerialPrint(ERROR, "Can't parse this backfill data: ");
        printHexArray(pData, length);
    }
}

/**
 * Connects to the transmitter found by the scan and get services / characteristics.
 * Return false if an error occurred.
 */
bool connectToTransmitter() 
{
    SerialPrint(DEBUG, "Forming a connection to ");
    SerialPrintln(DEBUG, myDevice->getAddress().toString().c_str());

    pClient = BLEDevice::createClient();                                                                                // We specify the security settings later after we have successful authorised with the transmitter.
    SerialPrintln(DEBUG, " - Created client");

    pClient->setClientCallbacks(new MyClientCallback());                                                                // Callbacks for onConnect() onDisconnect()

    // Connect to the remote BLE Server.
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

    forceRegisterNotificationAndIndication(indicateAuthCallback, pRemoteAuthentication, false);                         // Needed to work with G6 Plus (and G6) sensor. The command below only works for G6 (81...) transmitter.
    //registerForIndication(indicateAuthCallback, pRemoteAuthentication);                                               // We only register for the Auth characteristic. When we are authorised we can register for the other characteristics.
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
            if(error_last_connection)                                                                                   // An error occured last session.
            {
                force_rebonding = true;
                SerialPrintln(DEBUG, "Error happened in last connection so set force rebonding to true.");
            }                                                                                                           // Otherwise keep the default force_rebonding setting. (could be false or true when changed manually).
            SerialPrintln(DEBUG, "Wakeup caused by timer from hibernation.");                                           // No need to restart / reset variables because all memory is lost after hibernation.
            printSavedGlucose();                                                                                        // Only potential values available when woke up from deep sleep.
            break;
        default :
            force_rebonding = true;                                                                                     // Force bonding when esp first started after power off (or flash).
            SerialPrintln(DEBUG, "Wakeup was not caused by deep sleep (normal start).");                                // Problem with allways this case? See https://forum.mongoose-os.com/discussion/1628/tg0wdt-sys-reset-immediately-after-waking-from-deep-sleep
            break;
    }
}

/**
 * Returns true if invalid data was found / missing values or not x values are available.
 */
bool needBackfill()
{
    bool doBackfill = error_last_connection;                                                                            // Also request backfill if last time was an error (maybe error while backfilling so missed some data).
    for(int i = 0; i < saveLastXValues && !doBackfill; i++)
    {
        if(glucoseValues[i] < 10 || glucoseValues[i] > 600)                                                             // This includes 0 values from initialisation.
            doBackfill = true;
    }
    return doBackfill;
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
    pBLEScan->setWindow(99); //60-99 works                                                                              // The actual time that will be searched. Interval - Window = time the esp is doing nothing (used for energy efficiency).
    pBLEScan->setActiveScan(false);                                                                                     // Possible source of error if we cant connect to the transmitter.
}

/**
 * This function can be called in an error case.
 */
void ExitState(std::string message)
{
    error_current_connection = true;                                                                                    // Set to true to indicate that an error has occured.
    SerialPrintln(ERROR, message.c_str());
    pClient->disconnect();                                                                                              // Disconnect to trigger onDisconnect event and go to sleep.
}
/**
 * This method will perform a full transmitter connect and read data.
 */
bool run()
{
    error_current_connection = true;                                                                                    // Set to true to mark a potential error if it does not get set to false after successfully transmitter communication.

    if(!force_rebonding)
        setup_bonding();                                                                                                // Enable bonding from the start on, so transmitter does not want to (re)bond.

    if (!connectToTransmitter())                                                                                        // Connect to the found transmitter.
        ExitState("We have failed to connect to the transmitter!");
    else
        SerialPrintln(DEBUG, "We are now connected to the transmitter.");
    
    if(!readDeviceInformations())                                                                                       // Read the general device informations like model no. and manufacturer.
        SerialPrintln(DEBUG, "Error while reading device informations!");                                               // If empty strings are read from the device information Characteristic, try reading device information after successfully authenticated. 

    if(!authenticate())                                                                                                 // Authenticate with the transmitter.
        ExitState("Error while trying to authenticate!");

    if(!requestBond())                                                                                                  // Enable encryption and requesting bonding.
        ExitState("Error while trying to bond!");
    
    forceRegisterNotificationAndIndication(indicateControlCallback, pRemoteControl, false);                             // Now register (after auth) to receive new data on the control characteristic.

    // Reading current time from the transmitter (important for backfill).
    if(!readTimeMessage())
        SerialPrintln(ERROR, "Error reading Time Message!");
    
    // Optional: reading battery status.
    //if(!readBatteryStatus())
        //SerialPrintln(ERROR, "Can't read Battery Status!");

    //Read current glucose level to save it.
    if(!readGlucose())
        SerialPrintln(ERROR, "Can't read Glucose!");

    // Optional: read sensor raw (unfiltered / filtered) data.
    //if(!readSensor())
        //SerialPrintln(ERROR, "Can't read raw Sensor values!");

    // Optional: read time and glucose of last calibration.
    //if(!readLastCalibration())
        //SerialPrintln(ERROR, "Can't read last calibration data!");

    if(needBackfill())
    {
        forceRegisterNotificationAndIndication(notifyBackfillCallback, pRemoteBackfill, true);                          // Now register on the backfill characteristic.       
        // Read backfill of the last x values to also saves them.
        if(!readBackfill())
            SerialPrintln(ERROR, "Can't read backfill data!");
    }

    error_current_connection = false;                                                                                   // When we reached this point no error occured.
    //Let the Transmitter close the connection.
    sendDisconnect();
}

/**
 * This is the main loop function.
 */
void loop() 
{
    switch (Status)
    {
        case STATE_START_SCAN:
          BLEDevice::getScan()->start(0, true);                                                                         // false = maybe helps with connection problems.
          Status = STATE_SCANNING;
          break;

        case STATE_SCANNING:
          if(myDevice != NULL)                                                                                          // A device (transmitter) was found by the scan (callback).
            run();                                                                                                      // This function is blocking until all tansmitter communication has finished.
          break;
    }
}
