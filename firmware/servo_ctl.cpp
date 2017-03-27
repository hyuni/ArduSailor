#include "servo_ctl.h"

#include <Servo.h>
#include "logger.h"

#define WINCH_PORT 11
#define RUDDER_PORT 10

#define WINCH_EN 27
#define RUDDER_EN 26

#define SP_EN 25

// degrees per second
#define RUDDER_SPEED 300.0
#define WINCH_SPEED 25.0

int heel_offset = 0;
uint8_t current_rudder = 0;
uint8_t current_winch = 0;

Servo sv_winch, sv_rudder;

void servoInit() {
	pinMode(SP_EN, OUTPUT);
	digitalWrite(SP_EN, LOW);
    
	pinMode(WINCH_EN, OUTPUT);
	digitalWrite(WINCH_EN, LOW);

	pinMode(RUDDER_EN, OUTPUT);
	digitalWrite(RUDDER_EN, LOW);

	sv_winch.attach(WINCH_PORT);
	sv_rudder.attach(RUDDER_PORT);  
}

void centerWinch() {
	winchTo(WINCH_MAX);
}

void centerRudder() {
	rudderTo(90 + heel_offset);
}

void winchTo(int value) {
	logln(F("Winch to %d"), value);
  
	int v = constrain(value, min(WINCH_MIN, WINCH_MAX), max(WINCH_MIN, WINCH_MAX));
    
	if (current_winch == v)
		return;

	digitalWrite(SP_EN, HIGH);
	delay(10);
	digitalWrite(WINCH_EN, HIGH);
	delay(10);
	sv_winch.write(v);
	delay(1000 * (abs((current_winch - v))/WINCH_SPEED) + 150);
	digitalWrite(WINCH_EN, LOW);
	digitalWrite(SP_EN, LOW);
    
	current_winch = v;
}

void normalizedWinchTo(int value) {
	winchTo(map(value, 0, 90, WINCH_MIN, WINCH_MAX));
}

void normalizedWinchTo(int value, int min, int max) {
	winchTo(map(value, min, max, WINCH_MIN, WINCH_MAX));
}

void rudderFromCenter(int value) {
	rudderTo(90 + value);
}

void rudderTo(int value) {
	logln(F("Rudder to %d"), value);

	int v = constrain(value + heel_offset, RUDDER_MIN, RUDDER_MAX);
    
	if (current_rudder == v)
		return;
    
	digitalWrite(SP_EN, HIGH);
	delay(10);
	digitalWrite(RUDDER_EN, HIGH);
	delay(10);
	sv_rudder.write(v);
	delay(1000 * (abs((current_rudder - v))/RUDDER_SPEED) + 150);
	digitalWrite(RUDDER_EN, LOW);
	digitalWrite(SP_EN, LOW);
    
	current_rudder = v;
}
