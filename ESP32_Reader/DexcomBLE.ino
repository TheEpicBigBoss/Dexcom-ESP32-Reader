/**
 * Dexcom BLE communication functions.
 * 
 * Author: Max Kaiser
 * 12.04.2020
 */

#include "BLEDevice.h"
#include "Output.h"

/**
 * Wrapper function to send data to the authentication characteristic.
 */
bool AuthSendValue(std::string value)
{
    AuthCallbackResponse = "";                                                                                          // Reset to invalid because we will write to the characteristic and must wait until new data arrived from the notify callback.
    return writeValue("AuthSendValue", pRemoteAuthentication, value);
}

/**
 * Wrapper function to send data to the control characteristic.
 */
bool ControlSendValue(std::string value)
{
    ControlCallbackResponse = "";                                                                                          
    return writeValue("ControlSendValue", pRemoteControl, value);
}

/**
 * Barrier to wait until new data arrived through the notify callback.
 */
std::string AuthWaitToReceiveValue()
{
    while(connected)                                                                                                    // Only loop until we lost connection.
    {
        if(AuthCallbackResponse != "")
        {
            std::string returnValue = AuthCallbackResponse;                                                             // Save the new value.
            AuthCallbackResponse = "";                                                                                  // Reset because we handled the new data.
            //SerialPrint(DEBUG, "AuthWaitToReceiveValue = ");
            //printHexString(returnValue);
            return returnValue;
        }
    }
    ExitState("Error timeout in AuthWaitToReceiveValue");                                                               // The transmitter disconnected so exit.
    return "";
}

/**
 * Barrier to wait until new data arrived through the notify callback.
 */
std::string ControlWaitToReceiveValue()
{
    while(connected)                                                                                                    // Only loop until we lost connection.
    {
        if(ControlCallbackResponse != "")
        {
            std::string returnValue = ControlCallbackResponse;                                                          // Save the new value.
            ControlCallbackResponse = "";                                                                               // Reset because we handled the new data.
            //SerialPrint(DEBUG, "ControlWaitToReceiveValue = ");
            //printHexString(returnValue);
            return returnValue;
        }
    }
    ExitState("Error timeout in ControlWaitToReceiveValue");                                                            // The transmitter disconnected so exit.
    return "";
}
