/**
 * Header File with output functions to set the output level in the serial console.
 * Must be a header file so it can be included in the Arduino files.
 * 
 * Author: Max Kaiser
 * 24.03.2020
 */

#ifndef OUTPUT_H
#define OUTPUT_H

typedef enum 
{ 
    DEBUG   = 0,                                                                                                        // Tag for normal (debug) output (bytes send / recv, notify, callbacks).
    DATA    = 1,                                                                                                        // Tag for messages with calculated / parsed data from the transmitter.
    ERROR   = 2,                                                                                                        // Tag for error messages.
    GLUCOSE = 3                                                                                                         // Only for the one print message with the glucose value.
} OutputType;

typedef enum 
{ 
    FULL           = 0,                                                                                                 // Prints all output.
    NO_DEBUG       = 1,                                                                                                 // Prints only errors or data from the transmitter.
    ONLY_ERROR     = 2,                                                                                                 // Prints only errors ond one line with glucose.
    ONLY_GLUCOSE   = 3,                                                                                                 // Print only one line with the glucose value (NO ERRORS!).
    NONE           = 4                                                                                                  // Do not print anything - used when no serial monitor is connected.
} OutputLevel;


static OutputLevel outputLevel = NO_DEBUG;                    /* Change to your output level  */                            // Set this to NONE if no serial connection is used.

/** 
 * Wrapper functions for Serial.print(..) to allow filtering and setting an output log level.
 */
void SerialPrint(OutputType type, const char * text);
void SerialPrint(OutputType type, uint8_t value, int mode);

void SerialPrintln(OutputType type);
void SerialPrintln(OutputType type, const char * text);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wenum-compare"
void SerialPrint(OutputType type, const char * text)
{
    if(type >= outputLevel)                                                                                             // Only print if OutputType is more specific than OutputLevel
        Serial.print(text);
}
void SerialPrint(OutputType type, uint8_t value, int mode)
{
    if(type >= outputLevel)
        Serial.print(value, mode);
}
template<typename... Args> void SerialPrintf(int type, const char * f, Args... args)                                    // Use C++11 variadic templates
{
    if(type >= outputLevel)
        Serial.printf(f, args...);
    else
        delay(10);                                                                                                      // Use a delay as compensation for serial.print()
}

void SerialPrintln(OutputType type)
{
    if(type >= outputLevel)
        Serial.println();
    else
        delay(10);
}
void SerialPrintln(OutputType type, const char * text)
{
    if(type >= outputLevel)
        Serial.println(text);
    else
        delay(10);
}
#pragma GCC diagnostic pop

#endif /* OUTPUT_H */