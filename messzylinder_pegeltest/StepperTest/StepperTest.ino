#include <Arduino.h>
#include "A4988.h"

// using a 200-step motor (most common)
#define MOTOR_STEPS 200
// Configure the stepper pins connected.
#define DIR 8
#define STEP 9
#define MS1 10
#define MS2 11
#define MS3 12
// Configure the mf.Pegeltest pins connected.
#define PROBE_ON 6
#define PROBE_IN A0
// UART.
#define MAX_CMD_LEN 64

// Probing.
#define MAX_PROBE_STEPS 4000
#define PROBE_STEP_DELAY 5000 // Micros.

A4988 stepper(MOTOR_STEPS, DIR, STEP, MS1, MS2, MS3);

int parseCommand();
bool readProbe(int minMax); // -1: min, +1: max

void setup() {
  // Initialize Serial.
  Serial.begin(115200);

  // Initialize Stepper.
  stepper.begin(200, 4);
  stepper.setSpeedProfile(BasicStepperDriver::LINEAR_SPEED);

  // Init probe pins.
  pinMode(PROBE_ON, OUTPUT);
  digitalWrite(PROBE_ON, LOW);
  pinMode(PROBE_IN, INPUT);

  // Say hello.
  Serial.println("OK Init");
}

// Position.
long pos = 0;
long destPos = 0;

// Probing.
long probeStart = 0;
int probe;
int8_t probeDir = 0; // 1=Up, -1=Down.
uint16_t probeSteps = 0;
int probeThreshMin = 200;
int probeThreshMax = 750;

// UART

int ch, r;
size_t cmdLen = 0;
char cmd[MAX_CMD_LEN + 1];
char cmdCh = ' ';

void loop() {

  if (probeDir) {
    // Continue probing.
    if (probeSteps > MAX_PROBE_STEPS) {
      probeDir = 0;
      Serial.println("ERR PROBE");
    } else {

      if (probeDir > 0) {
        stepper.move(1);
        pos++;
      } else {
        stepper.move(-1);
        pos--;
      }

      probeSteps++;

      delayMicroseconds(PROBE_STEP_DELAY);

      probe = readProbe(-probeDir);

      if ((probeDir > 0 && !probe) || (probeDir < 0 && probe)) {
        probeDir = 0;
        Serial.print("PROBE ");
        Serial.println(pos);
      }

    }

    destPos = pos;

  } else {
    // Process Stepper move commands.
    stepper.nextAction();
    if (stepper.getCurrentState() == BasicStepperDriver::STOPPED) {
      pos = destPos;
    }
  }

  // Read UART
  if (Serial.available()) {

    ch = Serial.read();

    if (
      (ch >= '0' && ch <= '9') ||
      (ch >= 'a' && ch <= 'z') ||
      (ch >= 'A' && ch <= 'Z') ||
      ch == ' ' || ch == '.' || ch == '-'
    ) {

      // Received normal char.
      if (cmdLen >= MAX_CMD_LEN) {
        Serial.println("\r\nERR: TooLong");
        cmdLen = 0;
      } else {
        cmdCh = (char) ch;
        cmd[cmdLen] = cmdCh;
        cmdLen++;
        Serial.write(cmdCh);
      }

    } else if (ch == 13) {
      // Received return.
      Serial.println("\r\nOK Msg");

      r = parseCommand();
      cmdLen = 0;

      if (r < 0) {
        Serial.print("ERR Code ");
        Serial.println(r);
      } else {
        Serial.print("OK Code ");
        Serial.println(r);
      }

    } else if (ch > 20 && ch != 8) {

      // Unexpected char, abort current command.
      cmdLen = 0;
      Serial.print("\r\nERR Char ");
      Serial.println((uint8_t) ch);
    }
  }
}

void fullStop(void) {
  long stepsRemaining = stepper.stop();
  if (probeDir) {
    // Stop probing.
    probeDir = 0;
    destPos = pos;
  } else {
    // Stop moving.
    if (destPos < pos) {
      pos -= stepsRemaining;
    } else {
      pos += stepsRemaining;
    }
    destPos = pos;
  }
}

#define MAX_CMD_ARGS 4
int parseCommand() {
  if (cmdLen == 0) {
    return -1;
  }

  // Escape command
  cmd[cmdLen] = '\0';

  char* pFind = strchr(cmd, ' ');
  uint8_t iSpace = (pFind == NULL) ? cmdLen : (pFind - cmd);
  uint8_t iArg = iSpace + 1;
  uint8_t iArgLen = 0;
  uint8_t nArgs = 0;
  long args[MAX_CMD_ARGS];

  // Parse extra arguments
  while (iArg < cmdLen && nArgs < MAX_CMD_ARGS) {
    pFind = strchr(cmd + iArg, ' ');
    iArgLen = (pFind == NULL) ? cmdLen - iArg : ((uint8_t)(pFind - cmd) - iArg);
    if (iArgLen == 0) break;
    if (pFind == NULL) {
      cmd[iArg + iArgLen] = '\0';
    } else {
      *pFind = '\0';
    }
    args[nArgs] = atol(cmd + iArg);
    nArgs++;

    iArg += iArgLen + 1;
  }

  //  for(uint8_t i=0; i< nArgs;i++) {
  //    Serial.print(i);
  //    Serial.print(": ");
  //    Serial.println(args[i]);
  //  }
  //

  if (iSpace == 0) {
    return -2;
  }

  // Move command.
  if (iSpace == 1 && cmd[0] == 'm') {
    fullStop();
    if (nArgs > 0) {
      destPos = args[0];
      //      Serial.print(pos);
      //      Serial.print(" -> ");
      //      Serial.println(destPos);
      stepper.startMove(destPos - pos);
    }
    return 1;
  }

  // RPM command.
  if (iSpace == 1 && cmd[0] == 'r') {
    if (nArgs > 0) {
      stepper.setRPM((float) args[0]);
    }
    return 1;
  }

  // Home command.
  if (iSpace == 1 && cmd[0] == 'h') {
    fullStop();
    pos = 0;
    destPos = 0;
    return 1;
  }

  // POS query.
  if (iSpace == 1 && cmd[0] == 'p') {
    long curPos = pos;
    if (stepper.getCurrentState() != BasicStepperDriver::STOPPED) {
      long remaining = stepper.getStepsRemaining();
      if (destPos < pos) {
        curPos -= (pos - destPos) - remaining;
      } else {
        curPos -= (destPos - pos) - remaining;
      }
    }
    Serial.print("RET ");
    Serial.println(curPos);

    return 1;
  }

  // A query (analog probe).
  if (iSpace == 1 && cmd[0] == 'a') {
    digitalWrite(PROBE_ON, HIGH);
    probe = analogRead(PROBE_IN);
    digitalWrite(PROBE_ON, LOW);

    Serial.print("RET ");
    Serial.println(probe);

    return 1;
  }

  // D query (digital probe).
  if (iSpace == 1 && cmd[0] == 'd') {
    digitalWrite(PROBE_ON, HIGH);
    probe = digitalRead(PROBE_IN);
    digitalWrite(PROBE_ON, LOW);

    Serial.print("RET ");
    Serial.println(probe);

    return 1;
  }

  // G command (start probe).
  if (iSpace == 1 && cmd[0] == 'g') {
    probeDir = readProbe(-1) ? 1 : -1;
    probeSteps = 0;
    return 1;
  }

  // tmin command (probe threshold min)
  if (iSpace == 4 && memcmp(cmd, "tmin", 4) == 0) {
    if (nArgs && args[0] >= 0 && args[0] <= 900) {
      probeThreshMin = (int) args[0];
    } else {
      Serial.print("THRESH: ");
      Serial.println(probeThreshMin);
    }

    return 1;
  }

  if (iSpace == 4 && memcmp(cmd, "tmax", 4) == 0) {
    if (nArgs && args[0] >= 0 && args[0] <= 800) {
      probeThreshMax = (int) args[0];
    } else {
      Serial.print("THRESH: ");
      Serial.println(probeThreshMax);
    }

    return 1;
  }

  // Unknown command or stop command.
  fullStop();

  // Return 1 if was stop command and -1 if was unknown command.
  if (iSpace == 4 && memcmp(cmd, "STOP", 4) == 0) {
    // Motor stop.
    return 1;
  } else {
    return -1;
  }
}

bool readProbe(int minMax) {
  int probeThresh = minMax > 0 ? probeThreshMax : probeThreshMin;

  digitalWrite(PROBE_ON, HIGH);
  probe = analogRead(PROBE_IN);
  digitalWrite(PROBE_ON, LOW);
  return probe >= probeThresh;
}
