// tinyFMtuner
//
// FM Tuner with RDS using ATtiny85, RDA5807
// I2C OLED and rotary encoder.
//
// This is just a demo sketch that implements basic functionality. By pressing
// the rotary encoder button the RDA5807 seeks the next radio station. Turning
// the rotary encoder increases/decreases the volume. Selected frequency and
// volume are stored in the EEPROM.
//
//                                 +-\/-+
// ----------------- A0 (D5) PB5  1|    |8  Vcc
// Encoder A ------- A3 (D3) PB3  2|    |7  PB2 (D2) A1 ----- I²C (SCK)
// Encoder B ------- A2 (D4) PB4  3|    |6  PB1 (D1) --- Encoder Switch
//                           GND  4|    |5  PB0 (D0) -------- I²C (SDA)
//                                 +----+
//
// Controller:  ATtiny85
// Core:        ATTinyCore (https://github.com/SpenceKonde/ATTinyCore)
// Clockspeed:  8 MHz internal
// Millis:      enabled
//
// 2019/2020 by Stefan Wagner (https://easyeda.com/wagiminator)
// License: http://creativecommons.org/licenses/by-sa/3.0/


// Libraries
#include <TinyI2CMaster.h>      // https://github.com/technoblogy/tiny-i2c
#include <Tiny4kOLED.h>         // https://github.com/datacute/Tiny4kOLED
#include <TinyRDA.h>            // https://github.com/wagiminator/TinyRDA
#include <EEPROM.h>             // for storing user settings into EEPROM
#include <avr/pgmspace.h>       // for reading data from program memory
#include <avr/sleep.h>          // for sleep functions
#include <avr/interrupt.h>      // for interrupt functions
#include <avr/power.h>          // for power saving functions
#include <avr/wdt.h>            // for watch dog timer

// Pin assignments
#define ENCODERSW   1
#define ENCODERA    3
#define ENCODERB    4

// OLED contrast levels
#define BRIGHT      127
#define DIMM        50

// EEPROM identifier
#define EEPROM_IDENT   0x6CE7   // to identify if EEPROM was written by this program

// Radio class
Radio radio;

// Text strings
const char Header[]  PROGMEM = "Tiny FM Tuner";

// Variables
uint16_t  VCC;
uint8_t   volume    = 1;      // 0 .. 15
float     frequency = 102.6;   //95.7 100.8 102.6 103.0 104.4 106.1

volatile int      encoderValue  = volume;
volatile uint8_t  encoderLast   = volume;
volatile uint16_t encoderMax    = 60;



void setup() {
  // reset watchdog timer
  resetWatchdog ();
  
  // setup rotary encoder pins
  pinMode(ENCODERSW, INPUT_PULLUP);
  pinMode(ENCODERA,  INPUT_PULLUP);
  pinMode(ENCODERB,  INPUT_PULLUP);

  // setup pin change interrupt
  GIMSK = 0b00100000;                                       // turns on pin change interrupts
  PCMSK = bit (ENCODERSW) | bit (ENCODERA) | bit (ENCODERB);// turn on interrupt on encoder pins

  // setup and disable ADC for energy saving
  ADCSRA  = bit (ADPS1) | bit (ADPS2);                      // set ADC clock prescaler to 64
  ADCSRA |= bit (ADIE);                                     // enable ADC interrupt
  interrupts ();                                            // enable global interrupts
  power_adc_disable();                                      // turn off ADC

  // start I2C
  TinyI2C.init();

  // prepare and start OLED
  oled.begin();
  oled.setFont(FONT6X8);
  oled.setContrast(BRIGHT);
  oled.clear();
  oled.on();
  oled.switchRenderFrame();

  // start the tuner
  radio.init();
  readEEPROM();
  radio.tuneTo(frequency);
  radio.setVolume(volume);
  radio.setBassBoost(true);
  radio.setMute(false);
  waitTuning();
  updateScreen();
}


void loop() {
  sleep();                              // sleep for a while ...

  if (!digitalRead(ENCODERSW)) {        // seek up if encoder button is pressed
    oled.clear();
    oled.setCursor(0, 0);
    oled.print(F("Tuning ..."));
    oled.switchFrame();
    radio.seekUp();
    waitTuning();
    while (!digitalRead(ENCODERSW));
    updateEEPROM();
  }

  if (volume != encoderValue >> 2) {    // change volume if encoder was turned
    volume = encoderValue >> 2;
    radio.setVolume(volume);
    updateEEPROM();
  }

  updateScreen();                       // update information on the OLED
}


// updates main screen
void updateScreen() {
  radio.updateStatus();
  VCC = getVcc();
  oled.clear();
  oled.setCursor(0, 0);
  printP(Header);
  if (radio.state.hasStationName) {
    oled.setCursor(0, 1);
    oled.print(F("Station:  "));
    oled.print(radio.state.stationName);
  }
  oled.setCursor(0, 2);
  oled.print(F("Vol: "));
  printDigits(volume);
  oled.setCursor(60, 2);
  oled.print(F("Frq: "));
  oled.print(radio.state.frequency);
  oled.setCursor(0, 3);
  oled.print(F("Sig: "));
  printDigits(radio.state.signalStrength);
  oled.setCursor(60, 3);
  oled.print(F("Bat: "));
  printBatLevel();
  oled.switchFrame();
}


// waits for tuning completed
void waitTuning() {
  do {
      delay(100);
      radio.updateStatus();
    } while (radio.state.isTuning);
}


// prints a string from progmem on the OLED
void printP(const char* p) {
  char ch = pgm_read_byte(p);
  while (ch != 0)
  {
    oled.print(ch);
    ++p;
    ch = pgm_read_byte(p);
  }
}


// converts number to 2-digits and prints it on OLED
void printDigits(uint8_t digits) {
  if(digits < 10) oled.print(" ");
  oled.print(digits);
}


// prints battery charging level on OLED
void printBatLevel() {
  if (VCC < 3200) oled.print(F("weak")); else oled.print(F("OK"));
}


// reads frequency and volume stored in EEPROM
void readEEPROM() {
  uint16_t identifier = (EEPROM.read(0) << 8) | EEPROM.read(1);
  if (identifier == EEPROM_IDENT) {
    uint16_t efreq = (EEPROM.read(2) << 8) | EEPROM.read(3);
    frequency = (float)efreq / 100;
    volume = EEPROM.read(4);;
  }
}


// updates frequency and volume stored in EEPROM
void updateEEPROM() {
  uint16_t efreq = radio.state.frequency * 100;
  EEPROM.update(0, EEPROM_IDENT >> 8); EEPROM.update(1, EEPROM_IDENT & 0xFF);
  EEPROM.update(2, efreq >> 8); EEPROM.update(3, efreq & 0xFF);
  EEPROM.update(4, volume);
}

// get vcc by reading against 1.1V reference
uint16_t getVcc() {
  power_adc_enable();                   // power on ADC
  ADCSRA |= bit (ADEN) | bit (ADIF);    // enable ADC, turn off any pending interrupt
  ADMUX = bit (MUX3) | bit (MUX2);      // set Vcc measurement against 1.1V reference
  delay(1);                             // wait for Vref to settle
  set_sleep_mode (SLEEP_MODE_ADC);      // sleep during sample for noise reduction
  sleep_mode();                         // go to sleep while taking ADC sample
  while (bitRead(ADCSRA, ADSC));        // make sure sampling is completed
  uint16_t vcc = ADC;                   // read ADC sample result
  bitClear (ADCSRA, ADEN);              // disable ADC
  power_adc_disable();                  // and save some energy
  vcc = 1125300L / vcc;                 // calculate Vcc in mV; 1125300 = 1.1*1023*1000
  return vcc;                           // return VCC in mV
}


// go to sleep in order to save energy, wake up again by wdt or pin change interrupt
void sleep() {
  set_sleep_mode (SLEEP_MODE_PWR_DOWN); // set sleep mode to power down
  bitSet (GIFR, PCIF);                  // clear any outstanding interrupts
  power_all_disable ();                 // power off ADC, Timer 0 and 1, serial interface
  noInterrupts ();                      // timed sequence coming up
  resetWatchdog ();                     // get watchdog ready
  sleep_enable ();                      // ready to sleep
  interrupts ();                        // interrupts are required now
  sleep_cpu ();                         // sleep              
  sleep_disable ();                     // precaution
  power_all_enable ();                  // power everything back on
  power_adc_disable();                  // except ADC
}


// reset watchdog timer
void resetWatchdog () {
  MCUSR = 0;                            // clear various "reset" flags
  WDTCR = bit (WDCE) | bit (WDE)  | bit (WDIF);  // allow changes, disable reset, clear existing interrupt
  WDTCR = bit (WDIE) | bit (WDP1) | bit (WDP0);  // set interval to 125 millisecond
  wdt_reset();                          // pat the dog
}


// watchdog interrupt service routine
ISR (WDT_vect) {
   wdt_disable();                       // disable watchdog
}


// pin change interrupt service routine
ISR (PCINT0_vect) {
  int MSB = digitalRead(ENCODERA);      // MSB = most significant bit
  int LSB = digitalRead(ENCODERB);      // LSB = least significant bit
  int encoded = (MSB << 1) | LSB;       // converting the 2 pin value to single number
  int sum  = (encoderLast << 2) | encoded; //adding it to the previous encoded value
 
  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderValue ++;
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderValue --;
 
  encoderLast = encoded;                // store this value for next time
  encoderValue = constrain(encoderValue, 0, encoderMax); // constrain the value
}


// ADC interrupt service routine
EMPTY_INTERRUPT (ADC_vect);
