/*
MIT License
Copyright (c) 2019 Ron Diamond

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include <Adafruit_NeoPixel.h>
#define PIN            6
#define NUMPIXELS      18
Adafruit_NeoPixel neoPixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

#include "LedControl.h"
LedControl ledControl = LedControl(12,10,11,4);

#include "RTClib.h"
RTC_DS1307 realTimeClock;

#include<Wire.h>
const int MPU_addr=0x69;  // I2C address of the MPU-6050

enum Action {
    none                        = 0,
    displayIMUAttitude          = 1,
    displayRealTimeClock        = 2,
    displayGPS                  = 3,
    displayRangeWith1202Error   = 4,
    setTime                     = 5,
    setDate                     = 6,
};

enum Mode {
    modeIdle                = 0,
    modeInputVerb           = 1,
    modeInputNoun           = 2,
    modeInputProgram        = 3,
    modeLampTest            = 4
};

enum programNumber {
    programNone             = 0,
    programJFKAudio         = 62,
    programApollo11Audio    = 69,
    programApollo13Audio    = 70
};

enum lampNumber {
    lampNoun                = 0,
    lampProg                = 1,
    lampVerb                = 2,
    lampCompActy            = 3,
    lampOprErr              = 13,   // ?
    lampKeyRelease          = 14,
    lampUplinkActy          = 17    // ?
    // ...
};

enum keyValues {
    // symbolic references to individual keys
    keyNone                 = 20,
    keyVerb                 = 10,
    keyNoun                 = 11,
    keyPlus                 = 12,
    keyMinus                = 13,
    keyNumber0              = 0,
    keyNumber1              = 1,
    keyNumber2              = 2,
    keyNumber3              = 3,
    keyNumber4              = 4,
    keyNumber5              = 5,
    keyNumber6              = 6,
    keyNumber7              = 7,
    keyNumber8              = 8,
    keyNumber9              = 9,
    keyClear                = 18,
    keyProceed              = 14,
    keyRelease              = 16,
    keyEnter                = 15,
    keyReset                = 17
};

enum verbValues {
    verbNone                = 0,
    verbLampTest            = 35,
    verbDisplayDecimal      = 16,
    verbSetComponent        = 21
};

enum nounValues {
    nounNone                = 0,
    nounIMUAttitude         = 17,
    nounClockTime           = 36,
    nounDate                = 37,
    nounLatLongAltitude     = 43,
    nounRangeTgoVelocity    = 68
};

enum registerDisplayPositions {
    register1Position       = 4,
    register2Position       = 5,
    register3Position       = 6
};

long valueForDisplay[7];
byte digitValue[7][7];
byte keyValue = keyNone;
byte oldKey = none;
bool fresh = true;
byte action = none;
byte currentAction = none;
byte verb = verbNone;
byte verbNew[2];
byte verbOld[2];
byte noun = 0;
byte nounNew[2];
byte nounOld[2];
byte currentProgram = programNone;
byte progNew[2];
byte progOld[2];
byte count = 0;
byte mode = modeIdle;
byte oldMode = modeIdle;
bool toggle = false;
byte toggleCount = 0;
bool error = 0;
bool newAction = false;
byte audioTrack = 1;


void setup() {
    pinMode(A0, INPUT);
    pinMode(A1, INPUT);
    pinMode(A2, INPUT);
    pinMode(A7, INPUT);
    pinMode(7, OUTPUT);
    digitalWrite(7, LOW);
    randomSeed(analogRead(A7));
    neoPixels.begin();

    for (int index = 0; index < 4; index++) {
        ledControl.shutdown(index,false);
        ledControl.setIntensity(index, 8);
        ledControl.clearDisplay(index);
    }

    Wire.begin();
    Wire.beginTransmission(MPU_addr);
    Wire.write(0x6B);  // PWR_MGMT_1 register
    Wire.write(0);     // set to zero (wakes up the MPU-6050)
    Wire.endTransmission(true);

    realTimeClock.begin();
    Serial.begin(9600);
}

void loop() {
    if (currentProgram == programJFKAudio) {
        jfk(1);
    }
    else if (currentProgram == programApollo11Audio) {
        jfk(2);
    }
    else if (currentProgram == programApollo13Audio) {
        jfk(3);
    }

    if (mode == modeIdle) {
        executeIdleMode();
    }
    else if (mode == modeInputVerb) {
        executeVerbInputMode();
    }
    else if (mode == modeInputNoun) {
        executeNounInputMode();
    }
    else if (mode == modeInputProgram) {
        executeProgramInputMode();
    }
    else if (mode == modeLampTest) {
        executeLampTestModeWithDuration(5000);
    }

    if (toggleCount == 4) {
        toggleCount = 0;
        if (toggle == false) {
            toggle == true;
        }
        else {
            toggle = false;
        }
    }
        toggleCount++;

        if (action == displayGPS) {
            toggleCount = 4;
            delay(200);
        }
        else {
            delay(100);
        }

        delay(100);

        if (action == displayIMUAttitude) {
            actionReadIMU();  // V16N17 ReadIMU
        }
        else if (action == displayRealTimeClock) {
            actionReadTime();   // V16N36 ReadTime
        }
        else if (action == displayGPS) {
            actionReadGPS();    // V16N43 Read GPS
        }
        else if (action == setTime) {
            actionSetTime();    // V21N36 Set The Time
        }
        else if (action == setDate) {
            actionSetDate();    // V21N37 Set The Date
        }

        Serial.print(verb);
        Serial.print("  ");
        Serial.print(noun);
        Serial.print("  ");
       //Serial.println(action);
};

void executeIdleMode() {
    // no action set just reading the kb
    if (newAction == true) {
        validateAction();
    }
    else {
        if (error == 1) {
            flasher();
        }
        keyValue = readKeyboard();
        processIdleMode();
    }
}

void processIdleMode() {
    if (keyValue != oldKey) {
        fresh = true;
        oldKey = keyValue;
    }

    if (fresh == true) {
        if (keyValue == keyVerb) {
            // verb
            mode = modeInputVerb;
            fresh = false;
            byte keeper = verb;
            for (int index = 0; keeper >= 10 ; keeper = (keeper - 10)) {
                index++;
                verbOld[0] = index;
            }
            for (int index = 0; keeper >= 1; keeper = (keeper - 1)) {
                index++;
                verbOld[1] = index;
            }
        }
        else if (keyValue == keyNoun) {
            // noun
            mode = modeInputNoun;
            fresh = false;
            byte keeper = noun;
            for (int index = 0; keeper >= 10; keeper = (keeper - 10)) {
                index++; nounOld[0] = index;
            }
            for (int index = 0;keeper >= 1; keeper = (keeper - 1)) {
                index++; nounOld[1] = index;
            }
        }
        else if (keyValue == keyProceed) {
            // program
            mode = modeInputProgram;
            fresh = false;
        }
        else if (keyValue == keyReset) {
            // resrt reeor
            error = 0;
            turnOffLampNumber(13);
            fresh = false;
        }
    }
}

void executeVerbInputMode() {
    // inputting the verb
    illuminateWithRGBAndLampNumber(0, 150, 0, lampVerb);
    toggleKeyReleaseLamp();
    if (error == 1) {
        flasher();
    }
    keyValue = readKeyboard();
    processVerbInputMode();
}

void processVerbInputMode() {
    if (keyValue == oldKey) {
        fresh = false;
    }
    else {
        fresh = true;
        oldKey = keyValue;
        if ((error == 1) && (keyValue == keyReset) && (fresh == true)) {
            error = 0; turnOffLampNumber(lampOprErr); fresh = false;
        } //resrt reeor
        if ((keyValue == keyEnter) && (fresh == true)) {
            fresh = false;
            verb = ((verbNew[0] * 10) + (verbNew[1]));
            if ((verb != verbDisplayDecimal)
                && (verb != verbSetComponent)
                && (verb != verbLampTest)
                && (verb != verbNone)) {
                error = 1;
                verb = ((verbOld[0] * 10) + verbOld[1]);    // restore prior verb
            }
            else {
                turnOffLampNumber(lampOprErr);
                turnOffLampNumber(lampKeyRelease);
                turnOffLampNumber(lampVerb);
                mode = modeIdle;
                count = 0;
                fresh = false;
                error = 0;
                newAction = true;
            }
        }

        if (fresh == true) {
            if (keyValue == keyRelease) {
                mode = oldMode;
                turnOffLampNumber(lampKeyRelease);
                turnOffLampNumber(lampVerb);
                count = 0;
                fresh = false;
                if (verb == verbNone) {
                    ledControl.setRow(0,0,0);
                    ledControl.setRow(0,1,0);
                }
                else {
                    setDigits(0, 0, verbOld[0]);
                    setDigits(0, 1, verbOld[1]);
                }
            }
            else if (keyValue == keyNoun) {
                mode = modeInputNoun;
                turnOffLampNumber(lampVerb);
                count = 0;
                fresh = false;
            }
            else if (keyValue == keyProceed) {
                //program
                mode = modeInputProgram;
                turnOffLampNumber(lampVerb);
                count = 0;
                fresh = false;
            }

        }

        if ((keyValue <= keyNumber9) && (count < 2)) {
            verbNew[count] = keyValue;
            setDigits(0, count, keyValue);
            count++;
            fresh = false;
        }
    }
}

void executeNounInputMode() {
    // inputting the noun
    illuminateWithRGBAndLampNumber(0, 150, 0, lampNoun);
    toggleKeyReleaseLamp();
    if (error == 1) {
        flasher();
    }
    keyValue = readKeyboard();
    processNounInputMode();
}

void processNounInputMode() {
    if (keyValue == oldKey) {
        fresh = false;
    }
    else {
        fresh = true;
        oldKey = keyValue;
        if ((error == 1) && (keyValue == keyReset) && (fresh == true)) {
            error = 0;
            turnOffLampNumber(lampOprErr);
            fresh = false;
        } //resrt reeor

        if ((keyValue == keyEnter) && (fresh == true)) {
            fresh = false;
            noun = ((nounNew[0] * 10) + (nounNew[1]));
            fresh = false;
            if ((noun != nounIMUAttitude)
                && (noun != nounClockTime)
                && (noun != nounLatLongAltitude)
                && (noun != nounRangeTgoVelocity)
                && (noun != nounNone)) {
                noun = ((nounOld[0] * 10) + nounOld[1]);    // restore prior noun
                error = 1;
            }
            else {
                turnOffLampNumber(lampOprErr);
                turnOffLampNumber(lampKeyRelease);
                turnOffLampNumber(lampNoun);
                mode = modeIdle;
                count = 0;
                fresh = false;
                error = 0;
                newAction = true;
            }}

        if ((keyValue == keyRelease) && (fresh == true)) {
            mode = oldMode;
            turnOffLampNumber(lampKeyRelease);
            turnOffLampNumber(lampNoun);
            count = 0;
            fresh = false;
            if (noun == 0) {
                //verb
                ledControl.setRow(0, 4, 0);
                ledControl.setRow(0, 5, 0);
            }
            else {
                setDigits(0, 4, nounOld[0]);
                setDigits(0, 5, nounOld[1]);
            }}
        if ((keyValue == keyVerb) && (fresh == true)) {
            //verb
            mode = modeInputVerb;
            turnOffLampNumber(lampNoun);
            count = 0;
            fresh = false;
        }
        if ((keyValue == keyProceed) && (fresh == true)) {
            mode = modeInputProgram;
            turnOffLampNumber(lampNoun);
            count = 0;
            fresh = false;
            //program
        }
        if ((keyValue <= keyNumber9)
            && (count < 2)) {
            nounNew[count] = keyValue;
            setDigits(0, (count + 4), keyValue);
            count++;

        }
    }
}

void executeProgramInputMode() {
    // inputting the program
    illuminateWithRGBAndLampNumber(0, 150, 0, lampProg);
    toggleKeyReleaseLamp();
    if (error == 1) {
        flasher();
    }
    keyValue = readKeyboard();
    processProgramInputMode();
}

void processProgramInputMode() {
    if ((error == 1) && (keyValue == keyReset) && (fresh == true)) {
        error = 0;
        turnOffLampNumber(13);
        fresh = false;
    } //resrt reeor

    if ((keyValue == keyEnter) && (fresh == true)) {
        currentProgram = ((progNew[0] * 10) + (progNew[1]));
        fresh = false;
        if ((currentProgram != 16)
            && (currentProgram != 21)
            && (currentProgram != 35)
            && (currentProgram != programJFKAudio)
            && (currentProgram != programApollo11Audio)
            && (currentProgram != programApollo13Audio)
            && (currentProgram != programNone)) {
            error = 1;
        }
        else {
            progOld[0] = progNew[0];
            progOld[1] = progNew[1];
            turnOffLampNumber(13);
            mode = modeIdle;
            turnOffLampNumber(lampKeyRelease);
            turnOffLampNumber(lampProg);
            count = 0;
            fresh = false;
            error = 0;
            newAction = true;
        }
    }

    if (keyValue != oldKey) {
        fresh = true;
        oldKey = keyValue;
    }
    if ((keyValue == keyRelease) && (fresh == true)) {
        // verb
        mode = oldMode;
        turnOffLampNumber(lampKeyRelease);
        turnOffLampNumber(lampProg);
        count = 0;
        fresh = false;
    }
    if ((keyValue == keyNoun) && (fresh == true)) {
        // noun
        mode = modeInputNoun;
        turnOffLampNumber(lampProg);
        count = 0;
        fresh = false;
    }
    if ((keyValue == keyVerb) && (fresh == true)) {
        // verb
        mode = modeInputVerb;
        turnOffLampNumber(lampProg);
        count = 0;
        fresh = false;
    }
    if ((keyValue <= keyNumber9) && (count < 2)) {
        progNew[count] = keyValue;
        setDigits(0, (count + 2), keyValue);
        count++;
    }
}

//void processkeytime() {
//}

void executeLampTestModeWithDuration(int durationInMilliseconds) {
    for (int index = 11; index < 18; index++) {
        // Uplink Acty, No Att, Stby, Key Rel, Opr Err, --, --
        illuminateWithRGBAndLampNumber(100, 100, 60, index);    // less blue = more white
    }

    for (int index = 4; index < 11; index++) {
        // Temp, Gimbal Loc, Prog, Restart, Tracker, Alt, Vel
        illuminateWithRGBAndLampNumber(120, 110, 0, index);     // more yellow
    }

    for (int lampNumber = 0; lampNumber < 4; lampNumber++) {
        // Comp Acty, Prog, Verb, Noun
        illuminateWithRGBAndLampNumber(0, 150, 0, lampNumber);
    }

    int lampTestDigitValue = 8;
    // passes number "8" to all the 7-segment numeric displays
    for (int row = 0; row < 4; row++) {
        // row 0 = Prog/Verb/Noun
        // row 1 = Register 1
        // row 2 = Register 2
        // row 3 = Register 3
        // ... each has six positions
        // note: 'digit' # 0 in the three registers is the plus/minus sign
        for (int digitPosition = 0; digitPosition < 6; digitPosition++) {
            setDigits(row, digitPosition, lampTestDigitValue);
        }
    }

    delay(durationInMilliseconds);

    // reset all lamps
    for (int index = 0; index < 4; index++) {
        turnOffLampNumber(index);
    }
    for (int index = 4; index < 11; index++) {
        turnOffLampNumber(index);
    }
    for (int index = 11; index < 18; index++) {
        turnOffLampNumber(index);
    }
    for (int index = 0; index < 4; index++) {
        ledControl.clearDisplay(index);
    }

    // restore previously-displayed values for Verb and Noun
    verbNew[0] = verbOld[0];
    verbNew[1] = verbOld[1];

    // blank Verb readout if needed
    verb = ((verbOld[0] * 10) + verbOld[1]);
    if (verb == verbNone) {
        ledControl.setRow(0, 0, 0); ledControl.setRow(0, 1, 0);
    }
    else {
        setDigits(0, 0, verbOld[0]);
        setDigits(0, 1, verbOld[1]);
    }

    // blank Prog readout if needed
    if (currentProgram == programNone) {
        ledControl.setRow(0, 2, 0);
        ledControl.setRow(0, 3, 0);
    }
    else {
        setDigits(0, 0, progNew[0]);
        setDigits(0, 1, progNew[1]);
    }

    // blank Noun readout if needed
    if (noun == 0) {
        ledControl.setRow(0, 4, 0);
        ledControl.setRow(0, 5, 0);
    }
    else {
        setDigits(0, 4, nounNew[0]);
        setDigits(0, 5, nounNew[1]);
    }
    keyValue = keyNone;
    mode = modeIdle;
    validateAction();
}

void actionReadIMU() {
    readIMU();
}

void actionReadTime() {
    // read time from real-time clock (RTC)
    DateTime now = realTimeClock.now();
    valueForDisplay[register1Position] = (now.hour());
    valueForDisplay[register2Position] = (now.minute());
    valueForDisplay[register3Position] = (now.second() * 100);
    setDigits();
}

void actionReadGPS() {
    // read GPS
    digitalWrite(7, HIGH);
    delay(20);
    byte data[83];
    while (Serial.available() > 0) {
        int x =  Serial.read();
    }
    while (Serial.available() < 1) {
        int x = 1;
    }
    delay(6);
    int index = 0;
    while (Serial.available() > 0) {
        data[index] = Serial.read();
        delayMicroseconds(960);
        index++;
        if (index >= 72) {
            index = 71;
        }
    }

    int latitude = 0;
    int longitude = 0;
    int altitude = 0;

    if (count < 10) {
        count++;
        latitude = (((data[18] - 48) * 1000) + ((data[19] -48) * 100) + ((data[20] - 48) * 10) + ((data[21] - 48)));
        longitude = (((data[30] - 48) * 10000) + ((data[31] - 48) * 1000) + ((data[32] -48) * 100) + ((data[33] - 48) * 10) + ((data[34] - 48)));
        altitude = (((data[52] -48) * 100) + ((data[53] - 48) * 10) + ((data[54] - 48)));
    }
    else {
        count++;
        latitude = (((data[21] - 48) * 10000) + ((data[23] - 48) * 1000) + ((data[24] -48) * 100) + ((data[25] - 48) * 10) + ((data[26] - 48)));
        longitude = (((data[34] - 48) * 10000) + ((data[36] - 48) * 1000) + ((data[37] -48) * 100) + ((data[38] - 48) * 10) + ((data[39] - 48)));
        altitude = (((data[52] -48) * 100) + ((data[53] - 48) * 10) + ((data[54] - 48)));
    }

    if (count > 25) {
        count = 0;
    }

    if (data[28] != 78) {
        latitude = ((latitude - (latitude + latitude)));
    }

    if (data[41] != 69) {
        longitude = ((longitude - (longitude + longitude)));
    }

    valueForDisplay[register1Position] = latitude;
    valueForDisplay[register2Position] = longitude;
    valueForDisplay[register3Position] = altitude;
    digitalWrite(7, LOW);
    setDigits();
}

void actionSetTime() {
    // read & display time from hardware real-time clock (RTC)
    DateTime now = realTimeClock.now();
    int nowYear = now.year();
    int nowMonth = now.month();
    int nowDay = now.day();
    int nowHour = now.hour();
    int nowMinute = now.minute();
    int nowSecond = now.second();

    while (keyValue == keyEnter) {
        keyValue = readKeyboard();
    }

    while (keyValue != keyEnter) {
        Serial.println(keyValue);
        keyValue = readKeyboard();
        if (keyValue != oldKey) {
            oldKey = keyValue;
            if (keyValue == keyPlus) {
                nowHour++;
            }
            if (keyValue == keyMinus) {
                nowHour--;
            }
            if (nowHour > 23) {
                nowHour = 0;
            }
            if (nowHour < 0) {
                nowHour = 23;
            }
        }
        valueForDisplay[register1Position] = nowHour;
        valueForDisplay[register2Position] = nowMinute;
        valueForDisplay[register3Position] = (nowSecond * 100); // emulate milliseconds
        setDigits();
        delay(200);
        ledControl.clearDisplay(1);
        delay(50);
    }

    while (keyValue == keyEnter) {
        keyValue = readKeyboard();
    }

    while (keyValue != keyEnter) {
        keyValue = readKeyboard();
        if (keyValue != oldKey) {
            oldKey = keyValue;
            if (keyValue == keyPlus) {
                nowMinute++;
            }
            if (keyValue == keyMinus) {
                nowMinute--;
            }
            if (nowMinute > 59) {
                nowMinute = 0;
            }
            if (nowMinute < 0) {
                nowMinute = 59;
            }
        }
        valueForDisplay[register1Position] = nowHour;
        valueForDisplay[register2Position] = nowMinute;
        valueForDisplay[register3Position] = (nowSecond * 100);
        setDigits();
        delay(200);
        ledControl.clearDisplay(2);
        delay(50);
    }

    while (keyValue == keyEnter) {
        keyValue = readKeyboard();
    }

    while (keyValue != keyEnter) {
        keyValue = readKeyboard();
        if (keyValue != oldKey) {
            oldKey = keyValue;
            if (keyValue == keyPlus) {
                nowSecond++;
            }
            if (keyValue == keyMinus) {
                nowSecond--;
            }
            if (nowSecond > 59) {
                nowSecond = 0;
            }
            if (nowSecond < 0) {
                nowSecond = 59;
            }
        }

        valueForDisplay[register1Position] = nowHour;
        valueForDisplay[register2Position] = nowMinute;
        valueForDisplay[register3Position] = (nowSecond *100);
        setDigits();
        delay(200);
        ledControl.clearDisplay(3);
        delay(50);
    }
    realTimeClock.adjust(DateTime(nowYear, nowMonth, nowDay, nowHour, nowMinute, nowSecond));
    action = displayRealTimeClock;
    setDigits(0, 0, 1);
    setDigits(0, 1, 6);
    verb = verbDisplayDecimal;
    verbOld[0] = 1;
    verbOld[1] = 6;
}

void actionSetDate() {
    byte yearToSet[4];
    byte monthToSet[2];
    byte dayToSet[2];
    byte hourToSet[2];
    byte minuteToSet[2];
    byte secondToSet[2];

    DateTime now = realTimeClock.now();
    int nowYear = now.year();
    int nowMonth = now.month();
    int nowDay = now.day();
    int nowHour = now.hour();
    int nowMinute = now.minute();
    int nowSecond = now.second();

    realTimeClock.adjust(DateTime(
                                  ((yearToSet[0] * 10^3) + (yearToSet[1] * 10^2) + (yearToSet[2] * 10) + yearToSet[3]),
                                  ((monthToSet[0] * 10) + monthToSet[1]),
                                  ((dayToSet[0] * 10) + dayToSet[1]),
                                  nowHour,
                                  nowMinute,
                                  nowSecond)
                         );
}

/*
void mode11() {
    flashUplinkAndComputerActivityRandomly();
}
 */

int readKeyboard() {
    int oddRowDividerVoltage1 = 225;
    int oddRowDividerVoltage2 = 370;
    int oddRowDividerVoltage3 = 510;
    int oddRowDividerVoltage4 = 650;
    int oddRowDividerVoltage5 = 790;
    int oddRowDividerVoltage6 = 930;

    int evenRowDividerVoltage1 = 200;
    int evenRowDividerVoltage2 = 330;
    int evenRowDividerVoltage3 = 455;
    int evenRowDividerVoltage4 = 577;
    int evenRowDividerVoltage5 = 700;
    int evenRowDividerVoltage6 = 823;
    int evenRowDividerVoltage7 = 930;

    int value_row1 = analogRead(A0);
    int value_row2 = analogRead(A1);
    int value_row3 = analogRead(A2);
    if ((value_row1 > oddRowDividerVoltage6)
        && (value_row2 > oddRowDividerVoltage6)
        && (value_row3 > oddRowDividerVoltage6))
    {
        return keyNone;  // no key
    }

    // keyboard ~top row
    else if (value_row1 < oddRowDividerVoltage1) return keyVerb;
    else if (value_row1 < oddRowDividerVoltage2) return keyPlus;
    else if (value_row1 < oddRowDividerVoltage3) return keyNumber7;
    else if (value_row1 < oddRowDividerVoltage4) return keyNumber8;
    else if (value_row1 < oddRowDividerVoltage5) return keyNumber9;
    else if (value_row1 < oddRowDividerVoltage6) return keyClear;

    // keyboard ~middle row
    else if (value_row2 < evenRowDividerVoltage1) return keyNoun;
    else if (value_row2 < evenRowDividerVoltage2) return keyMinus;
    else if (value_row2 < evenRowDividerVoltage3) return keyNumber4;
    else if (value_row2 < evenRowDividerVoltage4) return keyNumber5;
    else if (value_row2 < evenRowDividerVoltage5) return keyNumber6;
    else if (value_row2 < evenRowDividerVoltage6) return keyProceed;
    else if (value_row2 < evenRowDividerVoltage7) return keyEnter;

    // keyboard ~bottom row
    else if (value_row3 < oddRowDividerVoltage1) return keyNumber0;
    else if (value_row3 < oddRowDividerVoltage2) return keyNumber1;
    else if (value_row3 < oddRowDividerVoltage3) return keyNumber2;
    else if (value_row3 < oddRowDividerVoltage4) return keyNumber3;
    else if (value_row3 < oddRowDividerVoltage5) return keyRelease;
    else if (value_row3 < oddRowDividerVoltage6) return keyReset;
}

void flashUplinkAndComputerActivityRandomly() {
    int randomNumber = random(10, 30);

    if ((randomNumber == 15) || (randomNumber == 25)) {
        illuminateWithRGBAndLampNumber(0, 150, 0, lampCompActy);
    }
    else {
        turnOffLampNumber(lampCompActy);
    }

    if ((randomNumber == 17) || (randomNumber == 25)) {
        illuminateWithRGBAndLampNumber(90, 90, 90, lampUplinkActy);
    }
    else {
        turnOffLampNumber(lampUplinkActy);
    }
}

void turnOffLampNumber(int lampNumber) {
    illuminateWithRGBAndLampNumber(0, 0, 0, lampNumber);
}

void illuminateWithRGBAndLampNumber(byte r, byte g, byte b, int lamp) {
    neoPixels.setPixelColor(lamp, neoPixels.Color(r,g,b));
    neoPixels.show();   // show the updated pixel color on the hardware
}

void toggleKeyReleaseLamp() {
    if (toggle == false) {
        illuminateWithRGBAndLampNumber(100, 100, 100, lampKeyRelease);
    }
    else {
        turnOffLampNumber(lampKeyRelease);
    }
}
void flasher() {
    if (toggle == false) {
        illuminateWithRGBAndLampNumber(100, 100, 100, lampOprErr);
    } else {
        turnOffLampNumber(lampOprErr);
    }
}

void validateAction() {
    if (verb == verbLampTest) {
        mode = modeLampTest;
        newAction = false;
    }
    else if ((verb == verbDisplayDecimal) && (noun == nounIMUAttitude)) {
        action = displayIMUAttitude;
        newAction = false;
    }
    else if ((verb == verbDisplayDecimal) && (noun == nounClockTime)) {
        action = displayRealTimeClock;
        newAction = false;
    }
    else if ((verb == verbDisplayDecimal) && (noun == nounLatLongAltitude)) {
        // Display current GPS
        action = displayGPS;
        newAction = false;
        count = 0;
    }
    else if ((verb == verbDisplayDecimal) && (noun == nounRangeTgoVelocity)) {
        // Display Range With 1202 ERROR
        action = displayRangeWith1202Error;
        newAction = false;
    }
    else if ((verb == verbSetComponent) && (noun == nounClockTime)) {
        action = setTime;
        newAction = false;
    }
    else if ((verb == verbSetComponent) && (noun == nounDate)) {
        action = setDate;
        newAction = false;
    }
    else {
        // not (yet) a valid verb/noun combination
        action = none;
        newAction = false;
    }
}

void readIMU() {
    flashUplinkAndComputerActivityRandomly();

    Wire.beginTransmission(MPU_addr);
    Wire.write(0x3B);  // starting with register 0x3B (ACCEL_XOUT_H)
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_addr,14,true);  // request a total of 14 registers

    valueForDisplay[0] = (Wire.read() << 8) | Wire.read();  // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
    valueForDisplay[1] = (Wire.read() << 8) | Wire.read();  // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
    valueForDisplay[2] = (Wire.read() << 8) | Wire.read();  // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
    valueForDisplay[3] = (Wire.read() << 8) | Wire.read();  // 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
    valueForDisplay[register1Position] = (Wire.read() << 8) | Wire.read();  // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
    valueForDisplay[register2Position] = (Wire.read() << 8) | Wire.read();  // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
    valueForDisplay[register3Position] = (Wire.read() << 8) | Wire.read();  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)
    valueForDisplay[3] = (valueForDisplay[3] / 340.00 + 36.53); //equation for temperature in degrees C from datasheet

    /* Serial.print("AcX = "); Serial.print(valueForDisplay[0]);
     Serial.print(" | AcY = "); Serial.print(valueForDisplay[1]);
     Serial.print(" | AcZ = "); Serial.print(valueForDisplay[2]);
     Serial.print(" | Tmp = "); Serial.print(valueForDisplay[3]);
     Serial.print(" | GyX = "); Serial.print(valueForDisplay[4]);
     Serial.print(" | GyY = "); Serial.print(valueForDisplay[5]);
     Serial.print(" | GyZ = "); Serial.println(valueForDisplay[6]);
     */

    setDigits();
}

void setDigits(byte maximum, byte digit, byte value) {//Serial.println("setDigits(byte ...)");
    ledControl.setDigit(maximum, digit, value, false);
}

void setDigits() {
    for (int indexa = 0; indexa < 8; indexa ++) {
        for (int index = 0; index < 7; index++) {
            digitValue[indexa][index] = 0;
        }
    }

    for (int indexa = 0; indexa < 7; indexa ++) {
        if (valueForDisplay[indexa] < 0) {
            valueForDisplay[indexa] = (valueForDisplay[indexa] - (valueForDisplay[indexa] + valueForDisplay[indexa]));
            digitValue[indexa][0] = 1;
        }
        else {
            digitValue[indexa][0] = 0;
        }
        for (int index = 0; valueForDisplay[indexa] >= 100000; valueForDisplay[indexa] = (valueForDisplay[indexa] - 100000)) {
            index++;
        }
        for (int index = 0; valueForDisplay[indexa] >= 10000; valueForDisplay[indexa] = (valueForDisplay[indexa] - 10000)) {
            index++;
            digitValue[indexa][1] = index;
        }
        for (int index = 0; valueForDisplay[indexa] >= 1000; valueForDisplay[indexa] = (valueForDisplay[indexa] - 1000)) {
            index++;
            digitValue[indexa][2] = index;
        }
        for (int index = 0; valueForDisplay[indexa] >= 100; valueForDisplay[indexa] = (valueForDisplay[indexa] - 100)) {
            index++;
            digitValue[indexa][3] = index;
        }
        for (int index = 0; valueForDisplay[indexa] >= 10; valueForDisplay[indexa] = (valueForDisplay[indexa] - 10)) {
            index++;
            digitValue[indexa][4] = index;
        }
        for (int index = 0; valueForDisplay[indexa] >= 1; valueForDisplay[indexa] = (valueForDisplay[indexa] - 1)) {
            index++;
            digitValue[indexa][5] = index;
        }
    }

    for (int index = 0; index < 3; index++) {
        // ledControl.clearDisplay(index+1);
        for (int i = 0; i < 6; i++) {
            if (i == 0) {
                if (digitValue[(index+4)][i] == 1) {
                    ledControl.setRow(index+1, i, B00100100);
                }
                else {
                    ledControl.setRow(index+1, i, B01110100);
                }
            }
            else {
                ledControl.setDigit(index+1, i, digitValue[index + 4][i], false);
            }
        }
    }
}

void jfk(byte jfk) {
    if (audioTrack > 3) {
        audioTrack = 1;
    }

    while (audioTrack != jfk) {
        pinMode(9, OUTPUT);
        delay(100);
        pinMode(9, INPUT);
        delay(100);
        audioTrack++;
        if (audioTrack > 3) {
            audioTrack = 1;
        }
    }

    pinMode(9, OUTPUT);
    delay(100);
    pinMode(9, INPUT);
    audioTrack++;
    currentProgram = programNone;
}
