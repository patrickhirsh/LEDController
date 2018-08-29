// Written by Patrick Hirsh, August 2018
// https://github.com/patrickhirsh/LEDController

#include <bitswap.h>
#include <chipsets.h>
#include <color.h>
#include <colorpalettes.h>
#include <colorutils.h>
#include <controller.h>
#include <cpp_compat.h>
#include <dmx.h>
#include <FastLED.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <fastpin.h>
#include <fastspi.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>
#include "IRremote.h"

/*
 *                  SETUP INFORMATION
 * 
 * This LED controller is tailored for my personal setup,
 * so if you stumbled accross this through GitHub, here's
 * a basic rundown:
 * 
 * I'm using an arduino UNO with an IR sensor + remote for
 * switching sequences. IRHandler() implements this functionality.
 * The strips themselves are in my computer case and on the back
 * of my desk. The case LEDs (ledsC) are 0-99 in a square (25 per side)
 * on the back of my case, and 100-111 in a line in front of my GPU.
 * My desk LEDs (ledsD) are 0-90 in a straight line accross the back
 * of my desk. The visualizer presets utilize my processing script
 * (also provided in the github repo) and transfer audio data from the
 * PC through the Arduino serial port.
 * 
 * Hopefully this information makes it easy to take some of the things
 * I've done and use them in your own setup!
 */

#define LED_PIN_CASE        7     // the case and desk strips recieve data from separate pins
#define LED_PIN_DESK        6
#define NUM_LEDS_CASE       112
#define NUM_LEDS_DESK       91
#define IR_PIN              8
#define BRIGHTNESS_0        5     // brightness presets for adjustGlobalBrightness()
#define BRIGHTNESS_1        20
#define BRIGHTNESS_2        65
#define BRIGHTNESS_3        127
#define BRIGHTNESS_4        255
#define GPU_SCALE_FACTOR    5     // scale gpu LED brightness cutoff by this value (GPU LEDs should be brighter than back LEDs) for adjustGlobalBrightness()

// control globals
CRGB ledsC[NUM_LEDS_CASE];        // 0-99: back LEDs (bottom right moving CC); 100-111: GPU LEDs (left to right)
CRGB ledsD[NUM_LEDS_DESK];        // 0-91: Moving from left to right on the back of the desk
byte ledMode = 0;                 // determines which LED sequence to run (if ledStatus = 1)
byte ledStatus = 1;               // 1 = on; 0 = off
byte ledBrightness = 255;         // global LED brightness, used in the brightness control functions
byte ledBrightnessPreset = 4;     // 5 presets (0-4). Because of IR collusion with FastLED, presets make more sense (see IRHandler())

// IR remote globals
IRrecv irrecv(IR_PIN);
decode_results results;

// stores the current global hue/sat/val for rainbow effects - See dynamicHueShift();
// modes that use this global typically call dynamicHueShift() to shift hue/sat/val every loop before applying to LEDs
CHSV dynamicHue = CHSV(0, 255, 255);   
byte scrollingHue = 0;
float visSpread = 0;


// audio globals (updated with sampleAudio())
byte audioSamples[100];            // buffer containing the last 100 audio samples (0 = most recent)
byte audioSample = 0;              // processed audio signal normalized to range 0-255



// -------------------- STARTUP -------------------- //


void setup() 
{
  // clear LEDs
  off();
  
	// tell FastLED about the LED strip configuration
	FastLED.addLeds<WS2812B, LED_PIN_CASE, GRB>(ledsC, NUM_LEDS_CASE);
  FastLED.addLeds<WS2812B, LED_PIN_DESK, GRB>(ledsD, NUM_LEDS_DESK);

  // initialize the serial communication:
  Serial.begin(9600);

  // start the IR reciever
  irrecv.enableIRIn();

  // used in various visualizers for random selection
  randomSeed(analogRead(0));

  // initialize audioSamples buffer
  for (int i = 0; i < 100; i++)
    audioSamples[i] = 0;
}




// -------------------- MAIN LOOP -------------------- //


void loop()
{
  // handle IR signals
  if (irrecv.decode(&results))
  {
    IRHandler();
    irrecv.resume();
  } 
  
  // are LEDs active?
  if (ledStatus == 1)
  {
    // run mode specified by ledMode
    if (ledMode == 0)      {red();}
    else if (ledMode == 8) {caseVisualizer();}
    else if (ledMode == 9) {deskVisualizer();}
       
    // non-implemented mode selected. Turn LEDs off
    else {ledStatus = 0;}       
  }
}




// -------------------- LED SEQUENCES -------------------- //


// simple all-red preset
void red()
{
  // only update LEDs once
  if (ledsC[0].red != 255)
	  for (int i = 0; i < NUM_LEDS_CASE; i++)
		  ledsC[i] = CRGB(255, 0, 0);
  if (ledsD[0].red != 255)
    for (int i = 0; i < NUM_LEDS_DESK; i++)
      ledsD[i] = CRGB(255, 0, 0);

  setGlobalBrightness();
  FastLED.show();
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// a rainbow waterfall visualizer
void deskVisualizer()
{
  // make sure we're getting input from AudioInputProcessor.exe
  if (Serial.available())
  {    
    // update audioSample and audioSamples buffer
    sampleAudio(Serial.read());

    // set case LEDs to static red
    if (ledsC[0].red != 255)
      for (int i = 0; i < NUM_LEDS_CASE; i++)
        ledsC[i] = CRGB(255, 0, 0);

    // reduce the visualizer spread by a constant every loop
    if (visSpread < 5) {visSpread -= .2;}     
    else if (visSpread < 10) {visSpread -= .4;}
    else if (visSpread < 20) {visSpread -= .6;}
    else {visSpread -= 1;}

    // ensure we never get a visSpread value below zero
    if (visSpread < 0) 
      visSpread = 0;      

    // increase the spread if the audio signal is greater than the current spread
    if (audioSample/5.7 > visSpread)
      visSpread = audioSample/5.7;

    // reset LEDs
    for (int i = 0; i < NUM_LEDS_DESK; i++)
      ledsD[i] = CRGB(0, 0, 0);

    // update spread
    for (int i = 0; i < (int)visSpread; i++)
    {
      ledsD[45 + i] = CHSV(scrollingHue + i*2, 255, 255);
      ledsD[45 - i] = CHSV(scrollingHue + i*2, 255, 255);
    }

    // scrolling hue should increase by a constant every loop
    scrollingHue += 1;

    // create a "feathering" effect on the last LED in the spread (using the decimal of visSpread)
    float edgeScale = visSpread - (int)visSpread;
    ledsD[45 + (int)visSpread] = CHSV(scrollingHue + (int)visSpread*2, 255, 255*edgeScale);
    ledsD[45 - (int)visSpread] = CHSV(scrollingHue + (int)visSpread*2, 255, 255*edgeScale);    
  }
  setGlobalBrightness();
  FastLED.show();
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// 
void caseVisualizer()
{
  // make sure we're getting input from AudioInputProcessor.exe
  if (Serial.available())
  { 
    // update audioSample and audioSamples buffer
    sampleAudio(Serial.read());
    
    // shift LEDs away from the center Case LED (37)   
    for (int i = 0; i < 37; i++)
      ledsC[i] = ledsC[i+1];
    for (int i = 74; i > 37; i--)
      ledsC[i] = ledsC[i-1];

    // shifting hue by a factor of audioSample causes colors to shift faster with higher audioSample values
    if (audioSample > 200) {dynamicHueShift(audioSample/50);}      
    else {dynamicHueShift(audioSample/100);}      

    // brightness is directly related to audioSample strength
    dynamicHue.val = audioSample;

    // set center LED
    ledsC[37] = dynamicHue;   

    // sudo "bass reactive" GPU LEDs through additive lighting
    // additive lighting usually reacts more to drawn-out noise (bass frequencies)
    for (int i = 75; i < 112; i++)
    {
      ledsC[i] += CRGB(audioSample/5, 0, 0);   // additive factor of audioSample
      ledsC[i] -= CRGB(8, 0, 0);               // reductive constant
    }
  }

  // set desk LEDs to static red
  for (int i = 0; i < NUM_LEDS_DESK; i++)
    ledsD[i] = CRGB(255, 0, 0);
  
  setGlobalBrightness();
  FastLED.show();
}




// -------------------- HELPER FUNCTIONS -------------------- //


/*
 * IRHandler is responsible for detecting IR remote signals and
 * responding accordingly. Note that when the FastLED library is
 * active and updating (with FastLED.show), it blocks interrupts
 * causing jumbled signals. I implemented behavior to combat this:
 * 
 * pressing ANY button while ledStatus = 1 (LEDs active) results
 * in a jumbled invalid signal. This signal triggers the default
 * case and turns the LEDs off. From here, a valid signal will
 * start the specifed mode and turn the LEDs on (ledStatus = 1)
 * 
 * USAGE: 
 * when LEDs are off, pressing any of the mode buttons will start that mode. 
 * when LEDs are on, pressing any button will turn the LEDs off.
 */
void IRHandler()
{
  switch(results.value)
  {
    case 0xFFFFFFFF: break;  // Repeat
    case 0xFFA25D: break;    // Power
    case 0xFFE21D: break;    // Func/Stop
    case 0xFF9867: break;    // EQ
    case 0xFFB04F: break;    // ST/REPT
    case 0xFFC23D: break;    // Skip Forward
    case 0xFF22DD: break;    // Skip Back
    case 0xFF02FD: break;    // Play/Pause
    case 0xFF629D: break;    // Vol Up
    case 0xFFA857: break;    // Vol Down
    case 0xFF906F: adjustGlobalBrightness(1); break;  // Arrow Up
    case 0xFFE01F: adjustGlobalBrightness(0); break;  // Arrow Down
    
    case 0xFF6897: ledMode = 0; ledStatus = 1; break; // 0 - red
    case 0xFF30CF: ledMode = 1; ledStatus = 1; break; // 1
    case 0xFF18E7: ledMode = 2; ledStatus = 1; break; // 2
    case 0xFF7A85: ledMode = 3; ledStatus = 1; break; // 3
    case 0xFF10EF: ledMode = 4; ledStatus = 1; break; // 4
    case 0xFF38C7: ledMode = 5; ledStatus = 1; break; // 5
    case 0xFF5AA5: ledMode = 6; ledStatus = 1; break; // 6
    case 0xFF42BD: ledMode = 7; ledStatus = 1; break; // 7
    case 0xFF4AB5: ledMode = 8; ledStatus = 1; break; // 8 - caseVisualizer
    case 0xFF52AD: ledMode = 9; ledStatus = 1; break; // 9 - deskVisualizer
    
    default:
      off();
      ledStatus = 0;   
  }
  // prevent repeated signals
  delay(100); 
}


/*
 * Used to control the global LED brightness
 * upOrDown = 1 -> Brightness Up; upOrDown = 0 -> Brightness Down
 * 
 * While I could use FastLED.setBrightness(), I don't want a scaled brightness
 * with these brightness settings. I simply want a brightness cutoff. Scaling
 * brightness evenly causes most of the visual effects within certain sequences
 * (particularly the visualizers) to be lost.
 */
void adjustGlobalBrightness(int upOrDown)
{
  // set the brightness preset
  if (upOrDown == 0)
  {
    if (ledBrightnessPreset > 0)
      ledBrightnessPreset--;
    else
      ledBrightnessPreset = 0;
  }
  if (upOrDown == 1)
  {
    if (ledBrightnessPreset < 4)
      ledBrightnessPreset++;
    else
      ledBrightnessPreset = 4;
  }

  // display selected brightness preset
  if (ledBrightnessPreset == 0)
  {
    ledBrightness = BRIGHTNESS_0;
    ledsD[90] = CRGB(BRIGHTNESS_0, BRIGHTNESS_0, BRIGHTNESS_0);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 1)
  {
    ledBrightness = BRIGHTNESS_1;
    ledsD[90] = CRGB(BRIGHTNESS_1, BRIGHTNESS_1, BRIGHTNESS_1);
    ledsD[85] = CRGB(BRIGHTNESS_1, BRIGHTNESS_1, BRIGHTNESS_1);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 2)
  {
    ledBrightness = BRIGHTNESS_2;
    ledsD[90] = CRGB(BRIGHTNESS_2, BRIGHTNESS_2, BRIGHTNESS_2);
    ledsD[85] = CRGB(BRIGHTNESS_2, BRIGHTNESS_2, BRIGHTNESS_2);
    ledsD[80] = CRGB(BRIGHTNESS_2, BRIGHTNESS_2, BRIGHTNESS_2);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 3)
  {
    ledBrightness = BRIGHTNESS_3;
    ledsD[90] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    ledsD[85] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    ledsD[80] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    ledsD[75] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 4)
  {
    ledBrightness = BRIGHTNESS_4;
    ledsD[90] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
    ledsD[85] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
    ledsD[80] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
    ledsD[75] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
    ledsD[70] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
    FastLED.show();
    delay(200);
    off();
  }

  // note that we don't change the ledMode in this IR signal handler
  // this causes the ledMode to default back to off after adjusting
  // brightness. We also only briefly "display" the brightness setting -
  // this is to prevent IR signal blocking by the FastLED library.
  // Once the displayed brightness turns off, the handler is ready for
  // another IR signal.
}


// should be ran before updating any LEDs with FastLED.show()
void setGlobalBrightness()
{
  // adjust brightness for desk LEDs
  for (int i = 0; i < NUM_LEDS_DESK; i++)
  {
    if (ledsD[i].red > ledBrightness) {ledsD[i].red = ledBrightness;}
    if (ledsD[i].green > ledBrightness) {ledsD[i].green = ledBrightness;}
    if (ledsD[i].blue > ledBrightness) {ledsD[i].blue = ledBrightness;}
  }
  
  // adjust brightness for back case LEDs
  for (int i = 0; i < 100; i++)
  {
    if (ledsC[i].red > ledBrightness) {ledsC[i].red = ledBrightness;}
    if (ledsC[i].green > ledBrightness) {ledsC[i].green = ledBrightness;}
    if (ledsC[i].blue > ledBrightness) {ledsC[i].blue = ledBrightness;}
  }

  // GPU LEDs should be brighter than back LEDs
  for (int i = 100; i < NUM_LEDS_CASE; i++)
  {
    if (ledsC[i].red > ledBrightness*GPU_SCALE_FACTOR) {ledsC[i].red = ledBrightness*GPU_SCALE_FACTOR;}
    if (ledsC[i].green > ledBrightness*GPU_SCALE_FACTOR) {ledsC[i].green = ledBrightness*GPU_SCALE_FACTOR;}
    if (ledsC[i].blue > ledBrightness*GPU_SCALE_FACTOR) {ledsC[i].blue = ledBrightness*GPU_SCALE_FACTOR;}
  }
}


// shifts dynamicHue by the given strength value
void dynamicHueShift(int strength)
{
  for (int i = strength; i > 0; i--)
  {
    if (dynamicHue.hue == 255)
      dynamicHue.hue = 0;
    else
      dynamicHue.hue++;
  }
}


// updates audioSample and the audioSamples buffer (0 = most current). Should be called every loop
void sampleAudio(byte rawAudio)
{
  // rawAudio is calibrated for max ~127 range at normal listening volume
  // this is so we could potentially utilize the upper range at louder volumes
  // for regular use, scale the samples by 2 to utilize the full 255 range
    if (rawAudio*2 > 255) {rawAudio = 255;}    
    else {rawAudio *= 2;}

  // shift the buffer and add update latest sample
  for (int i = 99; i > 0; i--)
    audioSamples[i] = audioSamples[i-1]; 
  audioSamples[0] = rawAudio;
  audioSample = rawAudio;
}

// turn all LEDs off
void off()
{
  FastLED.clear();
  FastLED.show();
}




// -------------------- PROTOTYPE CODE -------------------- //


/*
// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
void protoVisualizer()
{
  // make sure we're getting input from AudioInputProcessor.exe
  if (Serial.available())
  {
    // audioIn is typically a value from 0-127 (but can be as high as 255)
    audioIn = Serial.read();

    // scale audioIn for better visuals
    if (audioIn*2 > 255)
      audioIn = 255;
    else
      audioIn *= 2;

    // shifting hue by a factor of audioIn causes colors to shift faster with higher audioIn values
    dynamicHueShift(audioIn/70);

    int brightness = audioIn;
    int origin = random(112);
    int spread = audioIn/5;
    int upper = origin;
    int lower = origin;

    ledsC[origin] += CHSV(dynamicHue.hue, 255, brightness);

    for (int i = 0; i < spread; i++)
    {
      upper++;
      if (upper > 111) {upper = 0;}
      ledsC[upper] += CHSV(dynamicHue.hue, 255, brightness);
      brightness /= 1.2;
    }

    brightness = audioIn;
    
    for (int i = 0; i < spread; i++)
    {
      if (lower == 0) {lower = 112;}
      lower--;
      ledsC[lower] += CHSV(dynamicHue.hue, 255, brightness);
      brightness /= 1.2;
    }

    for (int i = 0; i < NUM_LEDS_CASE; i++)
      ledsC[i] -= CHSV(0, 0, 2);
    
  }
  setGlobalBrightness();
  FastLED.show();
}
*/
