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

#define LED_PIN             7
#define IR_PIN              8
#define NUM_LEDS            112
#define BRIGHTNESS_0        5
#define BRIGHTNESS_1        20
#define BRIGHTNESS_2        65
#define BRIGHTNESS_3        127
#define BRIGHTNESS_4        255
#define GPU_SCALE_FACTOR    5     // scale gpu LED brightness cutoff by this value (GPU LEDs should be brighter than back LEDs)

// control globals
CRGB leds[NUM_LEDS];              // 0-99: back LEDs (bottom right moving CC); 100-111: GPU LEDs (left to right)
byte audioIn = 0;                 // processed audio signal normalized to range 0-255
byte ledMode = 0;                 // determines which LED sequence to run (if ledStatus = 1)
byte ledStatus = 1;               // 1 = on; 0 = off
byte ledBrightness = 255;         // global LED brightness, used in the brightness control functions
byte ledBrightnessPreset = 4;     // 5 presets (0-4). Because of IR occlusion with FastLED, presets make more sense (see IRHandler())

// IR remote globals
IRrecv irrecv(IR_PIN);
decode_results results;

// stores the current global hue/sat/val for rainbow effects - See dynamicHueShift();
// modes that use this global typically call dynamicHueShift() to shift hue/sat/val every loop before applying to LEDs
CHSV dynamicHue = CHSV(0, 255, 255);    




// -------------------- STARTUP -------------------- //


void setup() 
{
  // clear LEDs
  off();
  
	// tell FastLED about the LED strip configuration
	FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

  // initialize the serial communication:
  Serial.begin(9600);

  // start the IR reciever
  irrecv.enableIRIn();

  // used in various visualizers for random selection
  randomSeed(analogRead(0));
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
    else if (ledMode == 8) {mellowVisualizer();}
    else if (ledMode == 9) {dynamicVisualizer();}
       
    // non-implemented mode selected. Turn LEDs off
    else {ledStatus = 0;}       
  }
}




// -------------------- LED SEQUENCES -------------------- //


// simple all-red preset
void red()
{
  // only update LEDs once
  if (leds[0].red != 255)
	  for (int i = 0; i < NUM_LEDS; i++)
		  leds[i] = CRGB(255, 0, 0);

  setGlobalBrightness();
  FastLED.show();
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// a more dynamic visualizer utilizing the full color range
void dynamicVisualizer()
{
  // make sure we're getting input from AudioInputProcessor.exe
  if (Serial.available())
  {
    // audioIn is typically a value from 0-127 (but can be as high as 255)
    audioIn = Serial.read();

    // shift LEDs away from the center LED (37)
    for (int i = 0; i < 37; i++)
      leds[i] = leds[i+1];
    for (int i = 74; i > 37; i--)
      leds[i] = leds[i-1];

    // scale audioIn for better visuals
    if (audioIn*2 > 255)
      audioIn = 255;
    else
      audioIn *= 2;

    // shifting hue by a factor of audioIn causes colors to shift faster with higher audioIn values
    if (audioIn > 200)
      dynamicHueShift(audioIn/15);
    else
      dynamicHueShift(audioIn/70);

    // brightness is directly related to audioIn strength
    dynamicHue.val = audioIn;

    // set center LED
    leds[37] = dynamicHue;

    // sudo "bass reactive" GPU LEDs through additive lighting
    // additive lighting usually reacts more to drawn-out noise (bass frequencies)
    for (int i = 75; i < 112; i++)
    {
      leds[i] += CRGB(audioIn/5, 0, 0);   // additive factor of audioIn
      leds[i] -= CRGB(8, 0, 0);           // reductive constant
    }
  }
  setGlobalBrightness();
  FastLED.show();
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// a relaxed visualizer utilizing only red channels
void mellowVisualizer()
{
  // make sure we're getting input from AudioInputProcessor.exe
  if (Serial.available())
  {
    // audioIn is typically a value from 0-127 (but can be as high as 255)
    audioIn = Serial.read();

    // shift LEDs away from the center LED (37)
    for (int i = 0; i < 37; i++)
      leds[i] = leds[i+1];
    for (int i = 74; i > 37; i--)
      leds[i] = leds[i-1];

    // scale audioIn for better visuals
    if (audioIn*2 > 255)
      audioIn = 255;
    else
      audioIn *= 2;

    // set center LED
    leds[37].red = audioIn;

    // sudo "bass reactive" GPU LEDs through additive lighting
    // additive lighting usually reacts more to drawn-out noise (bass frequencies)
    for (int i = 75; i < 112; i++)
    {
      leds[i] += CRGB(audioIn/5, 0, 0);   // additive factor of audioIn
      leds[i] -= CRGB(8, 0, 0);           // reductive constant
    }   
  }
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
    
    case 0xFF6897: ledMode = 0; ledStatus = 1; break; // 0 - Red
    case 0xFF30CF: ledMode = 1; ledStatus = 1; break; // 1
    case 0xFF18E7: ledMode = 2; ledStatus = 1; break; // 2
    case 0xFF7A85: ledMode = 3; ledStatus = 1; break; // 3
    case 0xFF10EF: ledMode = 4; ledStatus = 1; break; // 4
    case 0xFF38C7: ledMode = 5; ledStatus = 1; break; // 5
    case 0xFF5AA5: ledMode = 6; ledStatus = 1; break; // 6
    case 0xFF42BD: ledMode = 7; ledStatus = 1; break; // 7
    case 0xFF4AB5: ledMode = 8; ledStatus = 1; break; // 8 - MellowVisualizer
    case 0xFF52AD: ledMode = 9; ledStatus = 1; break; // 9 - DynamicVisualizer
    
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
    for (int i = 100; i < NUM_LEDS; i++)
      leds[i] = CRGB(BRIGHTNESS_0, BRIGHTNESS_0, BRIGHTNESS_0);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 1)
  {
    ledBrightness = BRIGHTNESS_1;
    for (int i = 75; i < NUM_LEDS; i++)
      leds[i] = CRGB(BRIGHTNESS_1, BRIGHTNESS_1, BRIGHTNESS_1);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 2)
  {
    ledBrightness = BRIGHTNESS_2;
    for (int i = 50; i < NUM_LEDS; i++)
      leds[i] = CRGB(BRIGHTNESS_2, BRIGHTNESS_2, BRIGHTNESS_2);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 3)
  {
    ledBrightness = BRIGHTNESS_3;
    for (int i = 50; i < NUM_LEDS; i++)
      leds[i] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    for (int i = 0; i < 25; i++)
      leds[i] = CRGB(BRIGHTNESS_3, BRIGHTNESS_3, BRIGHTNESS_3);
    FastLED.show();
    delay(200);
    off();
  }
  else if (ledBrightnessPreset == 4)
  {
    ledBrightness = BRIGHTNESS_4;
    for (int i = 0; i < NUM_LEDS; i++)
      leds[i] = CRGB(BRIGHTNESS_4, BRIGHTNESS_4, BRIGHTNESS_4);
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
  // adjust brightness for back LEDs
  for (int i = 0; i < 100; i++)
  {
    if (leds[i].red > ledBrightness) {leds[i].red = ledBrightness;}
    if (leds[i].green > ledBrightness) {leds[i].green = ledBrightness;}
    if (leds[i].blue > ledBrightness) {leds[i].blue = ledBrightness;}
  }

  // GPU LEDs should be brighter than back LEDs
  for (int i = 100; i < NUM_LEDS; i++)
  {
    if (leds[i].red > ledBrightness*GPU_SCALE_FACTOR) {leds[i].red = ledBrightness*GPU_SCALE_FACTOR;}
    if (leds[i].green > ledBrightness*GPU_SCALE_FACTOR) {leds[i].green = ledBrightness*GPU_SCALE_FACTOR;}
    if (leds[i].blue > ledBrightness*GPU_SCALE_FACTOR) {leds[i].blue = ledBrightness*GPU_SCALE_FACTOR;}
  }
}


// turn all LEDs off
void off()
{
  FastLED.clear();
  FastLED.show();
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

    leds[origin] += CHSV(dynamicHue.hue, 255, brightness);

    for (int i = 0; i < spread; i++)
    {
      upper++;
      if (upper > 111) {upper = 0;}
      leds[upper] += CHSV(dynamicHue.hue, 255, brightness);
      brightness /= 1.2;
    }

    brightness = audioIn;
    
    for (int i = 0; i < spread; i++)
    {
      if (lower == 0) {lower = 112;}
      lower--;
      leds[lower] += CHSV(dynamicHue.hue, 255, brightness);
      brightness /= 1.2;
    }

    for (int i = 0; i < NUM_LEDS; i++)
      leds[i] -= CHSV(0, 0, 2);
    
  }
  setGlobalBrightness();
  FastLED.show();
}
*/
