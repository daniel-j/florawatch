#include <Adafruit_NeoPixel.h>

#include <Wire.h>
#include <LSM303.h>

#include "Time.h"
#include "OneButton.h"


const unsigned long sleepTimeout = 5*60; // in seconds, set to 0 to disable

const unsigned int neoPin = 9;
const unsigned int buttonPin = 6;

const unsigned int ringSize = 16;
const int ringOffset = 4;
const int northOffset = 3;
const bool compassInvert = true;



Adafruit_NeoPixel ring = Adafruit_NeoPixel(ringSize, neoPin, NEO_GRB + NEO_KHZ800);

uint8_t* pixels = ring.getPixels();

LSM303 lsm;

OneButton button(buttonPin, 1);

int currentMenu = 0;
int currentSubmenu = 0;

int currentHour, currentMinute, currentDay, currentMonth, currentYear, monthMaxDays;
int compassDirection = -1;

bool flashTimeout = false;
unsigned long lastFlash = 0;
unsigned long lastActive;
bool isSleeping = false;

unsigned int millisecond = 0;

String zf(int n) {
	String v;
	if (n < 10) {
		v += "0";
		v += n;
	} else {
		v += n;
	}
	return v;
}

void setPixel(uint16_t pos, uint32_t color) {
	ring.setPixelColor(pixelPos(pos), color);
}
void setPixel(uint16_t pos, uint8_t r, uint8_t g, uint8_t b) {
	setPixel(pos, ring.Color(r, g, b));
}

// uses RGB order
void setChannel(uint16_t pos, uint8_t channel, uint8_t value) {
	uint8_t *p = &pixels[pixelPos(pos) * 3];
	p[channel] = value;
}
uint16_t pixelPos(uint16_t pos) {
	return ((ringSize-1)-((pos+ringOffset) % ringSize));
}
void clearPixels() {
	// from Adafruit_NeoPixel repo
	memset(pixels, 0, ringSize*3);
}

void syncMillis() {
	static unsigned long milli;
	static int oldSecond = 0;
	int newSecond = second();
	if (oldSecond != newSecond) {
		oldSecond = newSecond;
		milli = millis();
	}
	millisecond = (millis() - milli) % 1000;
}

void showLine(uint16_t pos, uint16_t length, uint32_t c) {
	for (int i = 0; i < length; i++) {
		setPixel(pos+i, c);
	}
}

void showBinary(uint16_t pos, int n, uint16_t length, bool direction, uint32_t c1, uint32_t c2) {
	int inv1 = 0;
	int inv2 = 1;
	if (direction) {
		inv1 = length-1;
		inv2 = -1;
	}
	for (int i = 0; i < length; i++) {
		if (bitRead(n, i) == 1) {
			setPixel(pos+inv1+i*inv2, c1);
		} else {
			setPixel(pos+inv1+i*inv2, c2);
		}
	}
}

void checkSleep() {
	if (!isSleeping && sleepTimeout > 0 && (millis()-lastActive)/1000 > sleepTimeout) {
		isSleeping = true;
		clearPixels();
		ring.show();
		if (currentMenu == 0 || currentMenu == 1) {
			lastFlash = 0;
			currentSubmenu = 0;
		}
	}
}
void keepActive() {
	lastActive = millis();
}
void wakeUp() {
	isSleeping = false;
	keepActive();
}



void setup() {
	Serial.begin(9600);

	ring.begin();
	ring.show();
	
	button.setClickTicks(40);
	button.setPressTicks(800);
	button.attachClick(btnClick);
	button.attachLongPressStart(btnPress);

	Wire.begin();
	lsm.init();
	lsm.enableDefault();

	lsm.m_min = (LSM303::vector<int16_t>){  -512,   -704,   -335};
	lsm.m_max = (LSM303::vector<int16_t>){  +591,   +502,   +691};

	keepActive();
}

void loop() {
	button.tick();
	syncMillis();
	flashTimeout = (millis() - lastFlash) % 1000 < 500;
	checkSleep();

	Serial.println(digitalRead(buttonPin));


	if (!isSleeping) {
		showClock();
	}
	
}

void btnClick() {
	if (isSleeping) {
		wakeUp();
		return;
	} else {
		keepActive();
	}

	if (currentSubmenu == 0) {
		currentMenu++;
		if (currentMenu > 2) {
			currentMenu = 0;
		}

	} else {
		if (currentMenu == 0 || currentMenu == 1) {
			lastFlash = millis();
			switch (currentSubmenu) {
				case 1:
					currentHour++;
					if (currentHour == 24) {
						currentHour = 0;
					}
					break;

				case 2:
					currentMinute++;
					if (currentMinute == 60) {
						currentMinute = 0;
					}
					break;
			}
		} else if (currentMenu == 2) {
			
		}
		/*else if (currentMenu == 1) {
			lastFlash = millisecond;
			switch (currentSubmenu) {
				case 1:
					currentYear++;
					if (currentYear > 2050) {
						currentYear = 1970;
					}
					break;

				case 2:
					currentMonth++;
					if (currentMonth > 12) {
						currentMonth = 1;
					}
					break;

				case 3:
					currentDay++;
					if (currentDay > monthMaxDays) {
						currentDay = 1;
					}
					break;
			}
		}*/
	}
}


void btnPress() {

	if (isSleeping) {
		wakeUp();
		return;
	} else {
		keepActive();
	}

	if (currentMenu == 0 || currentMenu == 1) { // Analog/Binary clock
		currentSubmenu++;
		if (currentSubmenu > 2) {
			currentSubmenu = 0;
		}
		switch (currentSubmenu) {
			case 0:
				lastFlash = 0;
				setTime(currentHour, currentMinute, 0, day(), month(), year());
				break;

			case 1:
				currentHour = hour();
				currentMinute = minute();
				lastFlash = millis();
				break;

			case 2:
				lastFlash = millis();
				break;
		}

	} else if (currentMenu == 2) { // Compass
		if (compassDirection == -1) {
			lsm.read();
			compassDirection = lsm.heading();
			if (compassInvert) {
				compassDirection = 360-compassDirection;
			}
		} else {
			compassDirection = -1;
		}
	}
	/*else if (currentMenu == 1) {

		
		// date
		
		currentSubmenu++;
		if (currentSubmenu > 3) {
			currentSubmenu = 0;
		}
		switch (currentSubmenu) {
			case 0:
				lastFlash = 0;
				setTime(hour(), minute(), second(), currentDay, currentMonth, currentYear);
				break;

			case 1:
				lastFlash = millis();
				currentDay = day();
				currentMonth = month();
				currentYear = year();
				break;

			case 2:
				lastFlash = millis();
				break;
			case 3:
				monthMaxDays = getMonthLength(currentMonth, currentYear);
				if (currentDay > monthMaxDays) currentDay = monthMaxDays;
				lastFlash = millis();
				break;
		}
	}*/
}

void showClock() {
	if (currentMenu == 0) { // Analog
		if (currentSubmenu == 0) {
			showAnalog();
		} else {
			showAdjAnalog(currentSubmenu-1);
		}
	} else if (currentMenu == 1) { // Binary
		if (currentSubmenu == 0) {
			showBinary();
		} else {
			showAdjBinary(currentSubmenu-1);
		}
	} else if (currentMenu == 2) { // Compass
		lsm.read();
		showCompass();
	}
}


void showAnalog() {
	int h = hour() % 12;
	int hourled1 = h/3+h;
	int hourled2 = h % 3 == 0? hourled1-1 : hourled1;


	int minuteled = minute()*ringSize/60;
	int secondled = second()*ringSize/60;
	
	
	clearPixels();
	// GRB order
	pixels[pixelPos(secondled)*3] += 8;
	pixels[pixelPos(secondled)*3+1] += 10;

	pixels[pixelPos(minuteled)*3] += 5;
	pixels[pixelPos(minuteled)*3+2] += 5;

	pixels[pixelPos(hourled1)*3+1] += 10;
	pixels[pixelPos(hourled2)*3+1] += 10;

	/*int r, g, b;
	for (int i = 0; i < ring.numPixels(); i++) {
		r = 0;
		g = 0;
		b = 0;

		if (i == secondled) {
			g += 7;
		}
		if (i == minuteled) {
			b += 2;
		}
		if (i == hourled1 || i == hourled2) {
			r += 7;
		}
		setPixel(i, r, g, b);
	}*/
	ring.show();
}

void showAdjAnalog(int type) {
	int h = currentHour % 12;
	int hourled1 = h/3+h;
	int hourled2 = h % 3 == 0? hourled1-1 : hourled1;

	int minuteled = currentMinute*ringSize/60;

	clearPixels();

	int hchannel = currentHour < 12? 1 : 0;

	if (type == 0 && flashTimeout) {
		pixels[pixelPos(hourled1)*3+hchannel] = 10;
		pixels[pixelPos(hourled2)*3+hchannel] = 10;
	} else {
		pixels[pixelPos(hourled1)*3+hchannel] = 1;
		pixels[pixelPos(hourled2)*3+hchannel] = 1;
	}
	
	if (type == 1 && flashTimeout) {
		pixels[pixelPos(minuteled)*3+2] = 10;
	} else {
		pixels[pixelPos(minuteled)*3+2] = 1;
	}

	ring.show();
}

void showBinary() {
	int h = hour() % 12;

	clearPixels();

	showBinary(0, second(), 6, true, ring.Color(10, 8, 0), ring.Color(1, 1, 0));
	showBinary(10, minute(), 6, false, ring.Color(0, 10, 5), ring.Color(0, 1, 1));

	if (hour() < 12) {
		showBinary(6, h, 4, true, ring.Color(10, 0, 0), ring.Color(1, 0, 0));
	} else {
		showBinary(6, h, 4, true, ring.Color(10, 0, 5), ring.Color(1, 0, 1));
	}
	
	ring.show();
}

void showAdjBinary(int type) {

	uint32_t hourcolor1;
	uint32_t hourcolor2;
	uint32_t hourcolor3;
	uint32_t hourcolor4;

	uint32_t minutecolor1 = ring.Color(0, 15, 15);
	uint32_t minutecolor2 = ring.Color(0, 8, 10);
	uint32_t minutecolor3 = ring.Color(0, 2, 2);
	uint32_t minutecolor4 = ring.Color(0, 1, 1);

	int h = currentHour % 12;

	clearPixels();

	if (currentHour < 12) {
		hourcolor1 = ring.Color(15, 0, 0);
		hourcolor2 = ring.Color(10, 0, 0);
		hourcolor3 = ring.Color(2, 0, 0);
		hourcolor4 = ring.Color(1, 0, 0);
	} else {
		hourcolor1 = ring.Color(15, 0, 8);
		hourcolor2 = ring.Color(10, 0, 5);
		hourcolor3 = ring.Color(2, 0, 2);
		hourcolor4 = ring.Color(1, 0, 1);
	}

	if (type == 0 && !flashTimeout) {
		showBinary(6, h, 4, true, hourcolor1, hourcolor3);
	} else {
		showBinary(6, h, 4, true, hourcolor2, hourcolor4);
	}
	
	if (type == 1 && !flashTimeout) {
		showBinary(10, currentMinute, 6, false, minutecolor1, minutecolor3);
	} else {
		showBinary(10, currentMinute, 6, false, minutecolor2, minutecolor4);
	}

	showLine(0, 6, ring.Color(1, 1, 0));

	ring.show();
}

void showCompass() {
	int dir = lsm.heading();
	if (compassInvert) {
		dir = 360-dir;
	}

	clearPixels();

	float pixel = dir*ringSize/360.0+northOffset;


	setPixel(floor(pixel-1.5), ring.Color(2, 0, 0));
	setPixel(floor(pixel-0.5), ring.Color(20, 0, 0));
	setPixel(floor(pixel+0.5), ring.Color(20, 0, 0));
	setPixel(floor(pixel+1.5), ring.Color(2, 0, 0));

	setPixel(floor(pixel+ringSize/2-1.5), ring.Color(2, 2, 2));
	setPixel(floor(pixel+ringSize/2-0.5), ring.Color(10, 10, 10));
	setPixel(floor(pixel+ringSize/2+0.5), ring.Color(10, 10, 10));
	setPixel(floor(pixel+ringSize/2+1.5), ring.Color(2, 2, 2));

	if (compassDirection != -1) {
		pixel = (360+dir-compassDirection)*ringSize/360.0+northOffset;
		setPixel(pixel, ring.Color(0, 20, 0));
		setPixel(pixel+ringSize/2, ring.Color(0, 20, 20));
	}

	ring.show();
}


/*void printDate() {
	String dateStr = "";
	dateStr += zf(day());
	dateStr += "/";
	dateStr += zf(month());
	dateStr += "/";
	dateStr += year();
	dateStr += "";

	
	lcd.setCursor(0, 0);
	lcd.print(dateStr);
}

void printAdjDate(int type) {
	String str = "";

	if (flashTimeout || type != 2) {
		str += zf(currentDay);
	} else {
		str += "  ";
	}
	str += "/";
	if (flashTimeout || type != 1) {
		str += zf(currentMonth);
	} else {
		str += "  ";
	}
	str += "/";
	if (flashTimeout || type != 0) {
		str += zf(currentYear);
	} else {
		str += "    ";
	}

	lcd.setCursor(0, 0);
	lcd.print(str);
}

int getMonthLength(int m, int y) {
	char monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
	int monthLength;
	if (m == 2) { // february
		if (LEAP_YEAR(y-1970)) {
			monthLength=29;
		} else {
			monthLength=28;
		}
	} else {
		monthLength = monthDays[m-1];
	}
	return monthLength;
}*/



