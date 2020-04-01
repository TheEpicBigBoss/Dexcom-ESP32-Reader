/**
 * Some helper functions for debugging and printing values.
 * 
 * Author: Max Kaiser
 * 24.03.2020
 */

#include "BLEDevice.h"
#include "BLEScan.h"
#include "rom/crc.h"
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

/**
 * 
 */
std::string CRC_16_XMODEM(std::string message)
{
    uint16_t crc = ~crc16_be((uint16_t)~0x0000, reinterpret_cast<const uint8_t*>(&message[0]), message.length());       // calculate crc 16 xmodem 
    std::string crcString = { (uint8_t)crc, (uint8_t)(crc >> 8) };

    SerialPrint(DEBUG, "CRC_16_XMODEM of ");
    for (int i = 0; i < message.length(); i++)
    {
        SerialPrint(DEBUG, (uint8_t)message[i], HEX);
        SerialPrint(DEBUG, " ");
    }
    SerialPrint(DEBUG, "is ");
    printHexString(crcString);
    return crcString;
}