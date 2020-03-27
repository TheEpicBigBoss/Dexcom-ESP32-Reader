/**
 * General bluetooth low energy (BLE) functionality for the bluedroid stack.
 * 
 * Author: Max Kaiser
 * 24.03.2020
 */

#include "BLEDevice.h"
#include "BLEScan.h"
#include "Output.h"

// Byte values for the notification / indication.
const uint8_t bothOff[]        = {0x0, 0x0};
const uint8_t notificationOn[] = {0x1, 0x0};
const uint8_t indicationOn[]   = {0x2, 0x0};
const uint8_t bothOn[]         = {0x3, 0x0};

/**
 * Gets and checks the characteristic from the remote service specified by the characteristics UUID.
 */
bool getCharacteristic(BLERemoteCharacteristic** pRemoteCharacteristic, BLERemoteService* pRemoteService, BLEUUID uuid) // Use *pRemoteCharacteristic as an out parameter so get address/pointer of this pointer.
{
    *pRemoteCharacteristic = pRemoteService->getCharacteristic(uuid);                                                   // Write to where the pointer points (the pRemoteCharacteristic pointer address).
    if (*pRemoteCharacteristic == nullptr) 
    {
        SerialPrint(DEBUG, "Failed to find our characteristic for UUID: ");
        SerialPrintln(DEBUG, uuid.toString().c_str());
        return false;
    }
    return true;
}

/**
 * Write an uint8_t array to the given characteristic.
 */ 
/*bool writeValue(std::string caller, BLERemoteCharacteristic* pRemoteCharacteristic, uint8_t data[])
{
    SerialPrint(DEBUG, caller.c_str());
    SerialPrint(DEBUG, " - Writing Data Array = ");
    printHexArray(data, sizeof(data) / sizeof(data[0]));
    pRemoteCharacteristic->writeValue(data, sizeof(data) / sizeof(data[0]), true);                                      // Is the size calculation correct, does it need length of the array or length in byte?
    return true;
}//*/

/**
 * Write a string to the given characteristic.
 */ 
bool writeValue(std::string caller, BLERemoteCharacteristic* pRemoteCharacteristic, std::string data)
{
    SerialPrint(DEBUG, caller.c_str());
    SerialPrint(DEBUG, " - Writing Data = ");
    printHexString(data);
    pRemoteCharacteristic->writeValue(data, true);    /* important must be true so we don't flood the transmitter */    // true = wait for response (acknowledgment) from the transmitter.
    return true;
}

/**
 * Register for notification, also check if notification is available.
 */
bool registerForNotification(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic)
{
    if (pBLERemoteCharacteristic->canNotify())                                                                          // Check if the characteristic has the potential to notify.
    {
        pBLERemoteCharacteristic->registerForNotify(_callback);
        SerialPrint(DEBUG, " - Registered for notify on UUID: ");
        SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
        return true;
    }
    else
    {
        SerialPrint(ERROR, " - Notify NOT available for UUID: ");
        SerialPrintln(ERROR, pBLERemoteCharacteristic->getUUID().toString().c_str());
    }
    return false;
}

/**
 * Register for notification AND indication, also check if both are available.
 */
bool registerForNotificationAndIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic)
{
    if (pBLERemoteCharacteristic->canNotify() && pBLERemoteCharacteristic->canIndicate()) 
    {
        pBLERemoteCharacteristic->registerForNotify(_callback);
        pBLERemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)bothOn, 2, true);     // Manually set the bytes because there is no such funktion to set both.
        SerialPrint(DEBUG, " - Registered for indicate and Notify on UUID: ");
        SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
        return true;
    }
    else
    {
        SerialPrint(ERROR, " - Indicate with Notify NOT available for UUID: ");
        SerialPrintln(ERROR, pBLERemoteCharacteristic->getUUID().toString().c_str());
    }
    return false;
}

/**
 * Register for indication, also check if indications are available.
 */
bool registerForIndication(notify_callback _callback, BLERemoteCharacteristic *pBLERemoteCharacteristic)
{
    if (pBLERemoteCharacteristic->canIndicate())
    {
        pBLERemoteCharacteristic->registerForNotify(_callback, false);                                                  // false = indication, true = notification
        SerialPrint(DEBUG, " - Registered for indicate on UUID: ");
        SerialPrintln(DEBUG, pBLERemoteCharacteristic->getUUID().toString().c_str());
        return true;
    }
    else
    {
        SerialPrint(ERROR, " - Indicate NOT available for UUID: ");
        SerialPrintln(ERROR, pBLERemoteCharacteristic->getUUID().toString().c_str());
    }
    return false;
}
