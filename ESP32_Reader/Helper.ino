/**
 * Some helper functions for debugging and printing values.
 * 
 * Author: Max Kaiser
 * 24.03.2020
 */

#include "BLEDevice.h"
#include "BLEScan.h"
#include "Output.h"

/**
 * Prints a sting as hex values.
 */
void printHexString(std::string value)
{
    for (int i = 0; i < value.length(); i++)
    {
        SerialPrint(DEBUG, (uint8_t)value[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrintln(DEBUG);
}

/**
 * Prints an uint8_t array as hex values.
 */
void printHexArray(uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        SerialPrint(DEBUG, data[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrintln(DEBUG);
}

/**
 * Converts an uint8_t array to string.
 */
std::string uint8ToString(uint8_t *data, size_t length)
{
    std::string value = "";
    for (size_t i = 0; i < length; i++)
    {
        value += (char)data[i];
    }
    return value;
}




