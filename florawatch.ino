#include <Adafruit_NeoPixel.h>

#include <Wire.h>
#include <LSM303.h>

#include "Time.h"
#include "OneButton.h"


const unsigned long sleepTimeout = 90; // in seconds, set to 0 to disable

const unsigned int neoPin = 9;
const unsigned int buttonPin = 6;
const unsigned int ledPin = 7;

const unsigned int ringSize = 16;
const int ringOffset = 4;
const float lmsOffset = 3.5;
const bool compassInvert = true;



Adafruit_NeoPixel ring = Adafruit_NeoPixel(ringSize, neoPin, NEO_GRB + NEO_KHZ800);

uint8_t* pixels = ring.getPixels();

const uint8_t PROGMEM gammaTable[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
	2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5,
	5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
	10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
	17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
	25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
	37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
	51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
	69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
	90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
	115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
	144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
	177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
	215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};

LSM303 lsm;

OneButton button(buttonPin, 1);


int currentMenu = 0;
int currentSubmenu = 0;

int currentHour, currentMinute, currentDay, currentMonth, currentYear, monthMaxDays;
int compassDirection = -1;
float currentHeading = 0.0;

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
uint8_t gamma(uint8_t input) {
	return pgm_read_byte(&gammaTable[input]);
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

void smooth(double data, float filterVal, double &smoothedVal) {

	if (filterVal > 1) {
		filterVal = 1.0;
	} else if (filterVal < 0) {
		filterVal = 0.0;
	}

	smoothedVal = (data * (1 - filterVal)) + (smoothedVal  *  filterVal);
}

double distance(double x1, double y1, double x2, double y2) {
	return sqrt((x1-x2)*(x1-x2)+(y1-y2)*(y1-y2));
}

void hsv2rgb(int hue, int sat, int val, uint8_t &r, uint8_t &g, uint8_t &b) {
	int base;
	// hue: 0-359, sat: 0-255, val (lightness): 0-255

	if (sat == 0) { // Achromatic color (gray).
		r=val;
		g=val;
		b=val;
	} else  {
		base = ((255 - sat) * val)>>8;
		switch(hue/60) {
			case 0:
				r = val;
				g = (((val-base)*hue)/60)+base;
				b = base;
				break;
			case 1:
				r = (((val-base)*(60-(hue%60)))/60)+base;
				g = val;
				b = base;
				break;
			case 2:
				r = base;
				g = val;
				b = (((val-base)*(hue%60))/60)+base;
				break;
			case 3:
				r = base;
				g = (((val-base)*(60-(hue%60)))/60)+base;
				b = val;
				break;
			case 4:
				r = (((val-base)*(hue%60))/60)+base;
				g = base;
				b = val;
				break;
			case 5:
				r = val;
				g = base;
				b = (((val-base)*(60-(hue%60)))/60)+base;
				break;
		}
	}
}
uint32_t hsv2color(int hue, int sat, int val) {
	uint8_t r, g, b;
	hsv2rgb(hue, sat, val, r, g, b);
	return ring.Color(gamma(r), gamma(g), gamma(b));
}

// Rainbow Cycle Program - Equally distributed
void rainbowCycle(uint16_t j, uint8_t brightness) {
	for (int i = 0; i < ringSize; i++) {
		setPixel(i, hsv2color(((i * 360 / ringSize) + j) % 360, 255, brightness));
	}
	ring.show();
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

	//min: {  -512,   -704,   -335}    max: {  +591,   +502,   +691}
	//min: {  -534,   -681,   -373}    max: {  +594,   +427,   +624}
	//min: {  -674,   -837,   -335}    max: {  +507,   +261,   +651}
	//min: {  -655,   -862,   -344}    max: {  +491,   +283,   +642}


	lsm.m_min = (LSM303::vector<int16_t>){  -655,   -862,   -344};
	lsm.m_max = (LSM303::vector<int16_t>){  +491,   +283,   +642};

	pinMode(ledPin, OUTPUT);

	keepActive();
}

void loop() {
	button.tick();
	syncMillis();
	flashTimeout = (millis() - lastFlash) % 1000 < 500;
	checkSleep();


	if (!isSleeping) {
		showPage();
	} else {

	}
	/*if (second() % 2 == 0 && millisecond < 50) {
		digitalWrite(ledPin, HIGH);
		digitalWrite(ledPin, LOW);
	}*/
	
}

void btnClick() {
	if (isSleeping) {
		wakeUp();
		return;
	} else {
		keepActive();
	}

	if (currentSubmenu == 0 || currentMenu == 3 || currentMenu == 4) {
		currentMenu++;
		if (currentMenu > 4) {
			currentMenu = 0;
		}
		currentSubmenu = 0;

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
	} else if (currentMenu == 3) { // Accelerometer stuff
		currentSubmenu++;
		if (currentSubmenu > 1) {
			currentSubmenu = 0;
		}
	} else if (currentMenu == 4) { // Animations
		currentSubmenu++;
		if (currentSubmenu > 1) {
			currentSubmenu = 0;
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

void showPage() {
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
		showCompass();
	} else if (currentMenu == 3) {
		if (currentSubmenu == 0) {
			showActivity();
		} else {
			showSpiritLevel();
		}
	} else if (currentMenu == 4) {
		if (currentSubmenu == 0) {
			showRainbow();
		} else {
			showRadar();
		}
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

	if (millisecond < 500) {
		showLine(15, 2, ring.Color(1, 1, 1));
	}

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

	if (millisecond < 500) {
		showLine(15, 2, ring.Color(1, 1, 1));
	}

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
	lsm.read();
	//smooth(lsm.heading(), 0.001, currentHeading);
	currentHeading = lsm.heading();
	//Serial.println(currentHeading);
	
	if (compassInvert) {
		currentHeading = 360-currentHeading;
	}

	clearPixels();

	float pixel = round(currentHeading*ringSize/360.0+lmsOffset);

	setPixel(pixel, ring.Color(20, 0, 0));
	setPixel(pixel+ringSize/2, ring.Color(10, 10, 10));

	/*setPixel(floor(pixel-1.5), ring.Color(2, 0, 0));
	setPixel(floor(pixel-0.5), ring.Color(20, 0, 0));
	setPixel(floor(pixel+0.5), ring.Color(20, 0, 0));
	setPixel(floor(pixel+1.5), ring.Color(2, 0, 0));

	setPixel(floor(pixel+ringSize/2-1.5), ring.Color(2, 2, 2));
	setPixel(floor(pixel+ringSize/2-0.5), ring.Color(10, 10, 10));
	setPixel(floor(pixel+ringSize/2+0.5), ring.Color(10, 10, 10));
	setPixel(floor(pixel+ringSize/2+1.5), ring.Color(2, 2, 2));*/

	if (compassDirection != -1) {
		//Serial.println(compassDirection);
		pixel = (360+currentHeading-compassDirection)*ringSize/360.0;
		setPixel(pixel, ring.Color(0, 20, 0));
		setPixel(pixel+ringSize/2, ring.Color(0, 20, 10));
	}

	ring.show();
}

void showActivity() {
	static unsigned long oldTime = 0.0;
	unsigned long curTime = millis();
	int delay = 15;

	lsm.read();
	long x = lsm.a.x;
	long y = lsm.a.y;
	long z = lsm.a.z;
	double v = sqrt(x*x+y*y+z*z) - 16000;

	int brightness = gamma(map(abs(v), 0, 45000, 20, 150));

	if (curTime - oldTime > delay) {
		oldTime = curTime;
		for (int i = 0; i < ringSize*3; i++) {
			pixels[i] *= 0.99999;
		}
	}
	
	uint8_t r, g, b;
	int pixel = curTime/15;
	hsv2rgb((curTime/5) % 360, 255, brightness, r, g, b);
	
	pixels[pixelPos(pixel)*3+1] += r;
	pixels[pixelPos(pixel)*3]   += g;
	pixels[pixelPos(pixel)*3+2] += b;

	ring.show();
}


void showSpiritLevel() {
	lsm.read();
	long x = lsm.a.x;
	long y = lsm.a.y;
	double dir = atan2(x, y) * 180.0 / PI;
	double length = sqrt(x*x + y*y);

	int pixel = round(dir*ringSize/360.0+lmsOffset);

	unsigned long max = 45413;
	unsigned long brightness = map(length, 0.0, max, 20, 255);

	clearPixels();

	setPixel(pixel, ring.Color(gamma(brightness-20), gamma(brightness), 0));

	ring.show();
}


void showRainbow() {
	unsigned long curTime = millis();

	rainbowCycle(round((curTime/4000.0)*360), 90+10*sin(curTime/1500.0));
}
	

void showRadar() {
	static unsigned long oldTimeAdd = 0;
	static unsigned long oldTimeFade = 0;
	unsigned long curTime = millis();
	static int addDelay = 1000;
	int fadeDelay = 15;
	static uint8_t objects[ringSize] = {0};

	if (curTime - oldTimeFade > fadeDelay) {
		oldTimeFade = curTime;
		for (int i = 0; i < ringSize; i++) {
			objects[i] *= 0.9999;
			if (objects[i] < 0.0) {
				objects[i] = 0.0;
			}
		}
	}

	float pixel = (curTime/4000.0)*ringSize;
	int pixel1 = ceil(pixel);
	int pixel2 = floor(pixel);
	float bright1 = pixel-pixel2;
	float bright2 = pixel1-pixel;

	if (curTime - oldTimeAdd > addDelay) {
		oldTimeAdd = curTime;
		addDelay = 1000+random(5000);
		objects[pixel1 % ringSize] = 150;
	}

	for (int i = 0; i < ringSize; i++) {
		setPixel(i, ring.Color(objects[i]*5/100, objects[i]*10/100, 0));
	}

	bright2 = max(bright2, objects[pixel2]/100);

	pixels[pixelPos(pixel1)*3+1] += bright1*5;
	pixels[pixelPos(pixel1)*3]   += bright1*10;
	pixels[pixelPos(pixel1)*3+2] += 0;

	pixels[pixelPos(pixel2)*3+1] += bright2*5;
	pixels[pixelPos(pixel2)*3]   += bright2*10;
	pixels[pixelPos(pixel2)*3+2] += 0;

	ring.show();
}


	//static double v = 0.0;


	/*double dir = atan2(x, y) * 180.0 / 3.14159;
	double length = sqrt(x*x + y*y);
	double v = sqrt(x*x+y*y+z*z)-16000;

	float pitch = atan(x/sqrt(y*y + z*z)) * (180.0/3.14159);
	float roll = atan(y/sqrt(x*x + z*z)) * (180.0/3.14159);

	

	clearPixels();*/

	/*Serial.print(pedi_step_counter);
	Serial.print(" | ");
	Serial.print(pedi_threshold);
	Serial.print(" | ");
	Serial.println(v);*/

	/*float pixel = dir*ringSize/360.0+lmsOffset;

	unsigned long max = 45413;
	unsigned long brightness = map(length, 0.0, max, 20, 255);*/

	//unsigned long zbright = map(abs(max(v, 0)), 0, 16000, 20, 100);
	//unsigned long zbright2 = map(abs(min(v, 0)), 0, 16000, 20, 100);
	
	/*if (v < -7000) {
		showLine(0, 16, ring.Color(20, 0, 0));
	} else if (v > 7000) {
		showLine(0, 16, ring.Color(0, 0, 20));
	}*/
	//showLine(0, 16, ring.Color(gamma(zbright), gamma(g), gamma(zbright2)));

	//setPixel(pixel, ring.Color(0, gamma(brightness), 0));

	//showBinary(0, pedi_step_counter, 16, false, ring.Color(20, 0, 0), ring.Color(1, 0, 0));

	//ring.show();

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



