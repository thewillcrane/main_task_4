//=====[Libraries]=============================================================

#include "mbed.h"
#include "arm_book_lib.h"

//=====[Defines]===============================================================

#define NUMBER_OF_KEYS                           4
#define BLINKING_TIME_GAS_ALARM               1000
#define BLINKING_TIME_OVER_TEMP_ALARM          500
#define BLINKING_TIME_GAS_AND_OVER_TEMP_ALARM  100
#define NUMBER_OF_AVG_SAMPLES                   100
#define OVER_TEMP_LEVEL                         50
#define TIME_INCREMENT_MS                       10

//=====[Declaration and initialization of public global objects]===============

DigitalIn enterButton(BUTTON1);
DigitalIn alarmTestButton(D2);
DigitalIn aButton(D4);
DigitalIn bButton(D5);
DigitalIn cButton(D6);
DigitalIn dButton(D7);
DigitalIn mq2(PE_12);

DigitalOut alarmLed(LED1);
DigitalOut incorrectCodeLed(LED3);
DigitalOut systemBlockedLed(LED2);

DigitalInOut sirenPin(PE_10);

UnbufferedSerial uartUsb(USBTX, USBRX, 115200);

AnalogIn potentiometer(A0);
AnalogIn lm35(A1);
AnalogIn Gas0127(A2);

//=====[Declaration and initialization of public global variables]=============

bool alarmState    = OFF;
bool incorrectCode = false;
bool overTempDetector = OFF;

int numberOfIncorrectCodes = 0;
int buttonBeingCompared    = 0;
int codeSequence[NUMBER_OF_KEYS]   = { 1, 1, 0, 0 };
int buttonsPressed[NUMBER_OF_KEYS] = { 0, 0, 0, 0 };
int accumulatedTimeAlarm = 0;

bool gasDetectorState          = OFF;
bool overTempDetectorState     = OFF;

float potentiometerReading = 0.0;
float lm35ReadingsAverage  = 0.0;
float lm35ReadingsSum      = 0.0;
float lm35ReadingsArray[NUMBER_OF_AVG_SAMPLES];
float lm35TempC            = 0.0;

//=====[Declarations (prototypes) of public functions]=========================

void inputsInit();
void outputsInit();

void alarmActivationUpdate();
void alarmDeactivationUpdate();

void uartTask();
void availableCommands();
bool areEqual();
float celsiusToFahrenheit( float tempInCelsiusDegrees );
float analogReadingScaledWithTheLM35Formula( float analogReading );
float gasReading( float analogReading);

//=====[Main function, the program entry point after power on or reset]========

//=====[Main function, the program entry point after power on or reset]========

int main()
{
    inputsInit();
    outputsInit();
    
    Timer timer;  // Add a timer to ensure periodic updates without delay
    timer.start();  // Start the timer
    
    while (true) {
        // Continuously update without delay
        alarmActivationUpdate();
        alarmDeactivationUpdate();
        uartTask();

        // Check the gas sensor and display a message about which sensor has been activated.
        // Send temperature and potentiometer readings every second (1000 ms)
        if (timer.read_ms() >= 1000) {
            float lm35TempC = analogReadingScaledWithTheLM35Formula(lm35.read());
            float potentiometerReading = potentiometer.read();
            float Gas0127Reading = Gas0127.read();
            uartUsb.write("Temperature: ", 15);
            char temperatureStr[50];
            sprintf(temperatureStr, "%.2f \xB0 C\r\n", lm35TempC);
            uartUsb.write(temperatureStr, strlen(temperatureStr));

            uartUsb.write("Potentiometer: ", 15);
            char potentiometerStr[50];
            sprintf(potentiometerStr, "%.2f\r\n", potentiometerReading);
            uartUsb.write(potentiometerStr, strlen(potentiometerStr));

            if(Gas0127Reading > 0.55){
                uartUsb.write("Gas detected!\r\n", 15);
            }else{
                uartUsb.write("No gas detected!\r\n", 18);
            }

            // Check for alarm conditions
            if (gasDetectorState) {
                uartUsb.write("Alarm Source: Gas Alarm\r\n", 25);
            } else if (overTempDetector) {
                uartUsb.write("Alarm Source: Temperature Alarm\r\n", 35);
            }
            
            // Reset the timer
            timer.reset();
        }
    }
}


//=====[Implementations of public functions]===================================

void inputsInit()
{
    alarmTestButton.mode(PullDown);
    aButton.mode(PullDown);
    bButton.mode(PullDown);
    cButton.mode(PullDown);
    dButton.mode(PullDown);
    sirenPin.mode(OpenDrain);
    sirenPin.input();
}

void outputsInit()
{
    alarmLed = OFF;
    incorrectCodeLed = OFF;
    systemBlockedLed = OFF;
}

void alarmActivationUpdate()
{
    static int lm35SampleIndex = 0;
    int i = 0;

    lm35ReadingsArray[lm35SampleIndex] = lm35.read();
    lm35SampleIndex++;
    if ( lm35SampleIndex >= NUMBER_OF_AVG_SAMPLES) {
        lm35SampleIndex = 0;
    }
    
       lm35ReadingsSum = 0.0;
    for (i = 0; i < NUMBER_OF_AVG_SAMPLES; i++) {
        lm35ReadingsSum = lm35ReadingsSum + lm35ReadingsArray[i];
    }
    lm35ReadingsAverage = lm35ReadingsSum / NUMBER_OF_AVG_SAMPLES;
       lm35TempC = analogReadingScaledWithTheLM35Formula ( lm35ReadingsAverage );    
    
    if ( lm35TempC > OVER_TEMP_LEVEL ) {
        overTempDetector = ON;
    } else {
        overTempDetector = OFF;
    }

    float Gas0127Reading = Gas0127.read();

    if (Gas0127Reading > 0.55) {  //Normal value is 0.43
    gasDetectorState = ON;   // So above 0.55 indicates high level of gas
    alarmState = ON;
    alarmLed = ON;
    }

    if (Gas0127Reading <= 0.55) { //Need to turn alarm back off
       gasDetectorState = OFF;
       if (!overTempDetector) { //Checks if temp alarm is off too
           alarmState = OFF;
           alarmLed = OFF;
       }
    }

    if( overTempDetector ) {
        overTempDetectorState = ON;
        alarmState = ON;
    }
    if( alarmTestButton ) {             
        overTempDetectorState = ON;
        gasDetectorState = ON;
        alarmState = ON;
    }    
    if( alarmState ) { 
        accumulatedTimeAlarm = accumulatedTimeAlarm + TIME_INCREMENT_MS;
        sirenPin.output();                                     
        sirenPin = LOW;                                        
    
        if( gasDetectorState && overTempDetectorState ) {
            if( accumulatedTimeAlarm >= BLINKING_TIME_GAS_AND_OVER_TEMP_ALARM ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        } else if( gasDetectorState ) {
            if( accumulatedTimeAlarm >= BLINKING_TIME_GAS_ALARM ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        } else if ( overTempDetectorState ) {
            if( accumulatedTimeAlarm >= BLINKING_TIME_OVER_TEMP_ALARM  ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        }
    } else{
        alarmLed = OFF;
        gasDetectorState = OFF;
        overTempDetectorState = OFF;
        sirenPin.input();                                  
    }
}

void alarmDeactivationUpdate()
{
    if ( numberOfIncorrectCodes < 5 ) {
        if ( aButton && bButton && cButton && dButton && !enterButton ) {
            incorrectCodeLed = OFF;
        }
        if ( enterButton && !incorrectCodeLed && alarmState ) {
            buttonsPressed[0] = aButton;
            buttonsPressed[1] = bButton;
            buttonsPressed[2] = cButton;
            buttonsPressed[3] = dButton;
            if ( areEqual() ) {
                alarmState = OFF;
                numberOfIncorrectCodes = 0;
            } else {
                incorrectCodeLed = ON;
                numberOfIncorrectCodes++;
            }
        }
    } else {
        systemBlockedLed = ON;
    }
}

void uartTask()
{
    char receivedChar = '\0';
    char str[100];
    int stringLength;
    if( uartUsb.readable() ) {
        uartUsb.read( &receivedChar, 1 );
        switch (receivedChar) {
        case '1':
            if ( alarmState ) {
                uartUsb.write( "The alarm is activated\r\n", 24);
            } else {
                uartUsb.write( "The alarm is not activated\r\n", 28);
            }
            break;

        case '2':
            if ( !mq2 ) {
                uartUsb.write( "Gas is being detected\r\n", 22);
            } else {
                uartUsb.write( "Gas is not being detected\r\n", 27);
            }
            break;

        case '3':
            if ( overTempDetector ) {
                uartUsb.write( "Temperature is above the maximum level\r\n", 40);
            } else {
                uartUsb.write( "Temperature is below the maximum level\r\n", 40);
            }
            break;
            
        case '4':
            uartUsb.write( "Please enter the code sequence.\r\n", 33 );
            uartUsb.write( "First enter 'A', then 'B', then 'C', and ", 41 ); 
            uartUsb.write( "finally 'D' button\r\n", 20 );
            uartUsb.write( "In each case type 1 for pressed or 0 for ", 41 );
            uartUsb.write( "not pressed\r\n", 13 );
            uartUsb.write( "For example, for 'A' = pressed, ", 32 );
            uartUsb.write( "'B' = pressed, 'C' = not pressed, ", 34);
            uartUsb.write( "'D' = not pressed, enter '1', then '1', ", 40 );
            uartUsb.write( "then '0', and finally '0'\r\n\r\n", 29 );

            incorrectCode = false;

            for ( buttonBeingCompared = 0;
                  buttonBeingCompared < NUMBER_OF_KEYS;
                  buttonBeingCompared++) {

                uartUsb.read( &receivedChar, 1 );
                uartUsb.write( "*", 1 );

                if ( receivedChar == '1' ) {
                    if ( codeSequence[buttonBeingCompared] != 1 ) {
                        incorrectCode = true;
                    }
                } else if ( receivedChar == '0' ) {
                    if ( codeSequence[buttonBeingCompared] != 0 ) {
                        incorrectCode = true;
                    }
                } else {
                    incorrectCode = true;
                }
            }

            if ( incorrectCode == false ) {
                uartUsb.write( "\r\nThe code is correct\r\n\r\n", 25 );
                alarmState = OFF;
                incorrectCodeLed = OFF;
                numberOfIncorrectCodes = 0;
            } else {
                uartUsb.write( "\r\nThe code is incorrect\r\n\r\n", 27 );
                incorrectCodeLed = ON;
                numberOfIncorrectCodes++;
            }                
            break;

        case '5':
            uartUsb.write( "Please enter new code sequence\r\n", 32 );
            uartUsb.write( "First enter 'A', then 'B', then 'C', and ", 41 );
            uartUsb.write( "finally 'D' button\r\n", 20 );
            uartUsb.write( "In each case type 1 for pressed or 0 for not ", 45 );
            uartUsb.write( "pressed\r\n", 9 );
            uartUsb.write( "For example, for 'A' = pressed, 'B' = pressed,", 46 );
            uartUsb.write( " 'C' = not pressed,", 19 );
            uartUsb.write( "'D' = not pressed, enter '1', then '1', ", 40 );
            uartUsb.write( "then '0', and finally '0'\r\n\r\n", 29 );

            for ( buttonBeingCompared = 0; 
                  buttonBeingCompared < NUMBER_OF_KEYS; 
                  buttonBeingCompared++) {

                uartUsb.read( &receivedChar, 1 );
                uartUsb.write( "*", 1 );

                if ( receivedChar == '1' ) {
                    codeSequence[buttonBeingCompared] = 1;
                } else if ( receivedChar == '0' ) {
                    codeSequence[buttonBeingCompared] = 0;
                }
            }

            uartUsb.write( "\r\nNew code generated\r\n\r\n", 24 );
            break;
 
        case 'p':
        case 'P':
            potentiometerReading = potentiometer.read();
            sprintf ( str, "Potentiometer: %.2f\r\n", potentiometerReading );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        case 'c':
        case 'C':
            sprintf ( str, "Temperature: %.2f \xB0 C\r\n", lm35TempC );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        case 'f':
        case 'F':
            sprintf ( str, "Temperature: %.2f \xB0 F\r\n", 
                celsiusToFahrenheit( lm35TempC ) );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        default:
            availableCommands();
            break;

        }
    }
}

void availableCommands()
{
    uartUsb.write( "Available commands:\r\n", 21 );
    uartUsb.write( "Press '1' to get the alarm state\r\n", 34 );
    uartUsb.write( "Press '2' to get the gas detector state\r\n", 41 );
    uartUsb.write( "Press '3' to get the over temperature detector state\r\n", 54 );
    uartUsb.write( "Press '4' to enter the code sequence\r\n", 38 );
    uartUsb.write( "Press '5' to enter a new code\r\n", 31 );
    uartUsb.write( "Press 'P' or 'p' to get potentiometer reading\r\n", 47 );
    uartUsb.write( "Press 'f' or 'F' to get lm35 reading in Fahrenheit\r\n", 52 );
    uartUsb.write( "Press 'c' or 'C' to get lm35 reading in Celsius\r\n\r\n", 51 );
}

bool areEqual()
{
    int i;

    for (i = 0; i < NUMBER_OF_KEYS; i++) {
        if (codeSequence[i] != buttonsPressed[i]) {
            return false;
        }
    }

    return true;
}

float analogReadingScaledWithTheLM35Formula( float analogReading )
{
    return ( analogReading * 3.3 / 0.01 );
}

float celsiusToFahrenheit( float tempInCelsiusDegrees )
{
    return ( tempInCelsiusDegrees * 9.0 / 5.0 + 32.0 );
}
