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

// control globals
CRGB leds[NUM_LEDS];        // 0-99: back LEDs (bottom right moving CC); 100-111: GPU LEDs (left to right)
byte audioIn = 0;           // processed audio signal normalized to range 0-255
byte ledMode = 10;          // determines which LED sequence to run (if ledStatus = 1) -> 10 = BootSequence
byte ledStatus = 1;         // 1 = on; 0 = off

// IR remote globals
IRrecv irrecv(IR_PIN);
decode_results results;

// dynamic LED sequence globals
CHSV dynamicHue = CHSV(0, 255, 255);


// -------------------- STARTUP -------------------- //
// runs once on arduino startup
void setup() 
{
  // clear LEDs
  Off();
  
	// tell FastLED about the LED strip configuration
	FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);

  // initialize the serial communication:
  Serial.begin(9600);

  // start the IR reciever
  irrecv.enableIRIn();
}




// -------------------- MAIN LOOP -------------------- //
// runs constantly
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
    if (ledMode == 10)     {BootSequence();}
    else if (ledMode == 0) {Red();}
    else if (ledMode == 1) {RedDimmed();}
    else if (ledMode == 8) {MellowVisualizer();}
    else if (ledMode == 9) {DynamicVisualizer();}
       
    // non-implemented mode selected. Turn LEDs off
    else {ledStatus = 0;}       
  }
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
 * 
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
    case 0xFF906F: break;    // Arrow Up
    case 0xFFE01F: break;    // Arrow Down
    case 0xFF629D: break;    // Vol Up
    case 0xFFA857: break;    // Vol Down
    
    case 0xFF6897: ledMode = 0; ledStatus = 1; break; // 0 - Red
    case 0xFF30CF: ledMode = 1; ledStatus = 1; break; // 1 - RedDimmed
    case 0xFF18E7: ledMode = 2; ledStatus = 1; break; // 2
    case 0xFF7A85: ledMode = 3; ledStatus = 1; break; // 3
    case 0xFF10EF: ledMode = 4; ledStatus = 1; break; // 4
    case 0xFF38C7: ledMode = 5; ledStatus = 1; break; // 5
    case 0xFF5AA5: ledMode = 6; ledStatus = 1; break; // 6
    case 0xFF42BD: ledMode = 7; ledStatus = 1; break; // 7
    case 0xFF4AB5: ledMode = 8; ledStatus = 1; break; // 8 - MellowVisualizer
    case 0xFF52AD: ledMode = 9; ledStatus = 1; break; // 9 - DynamicVisualizer
    
    default:
      Off();
      ledStatus = 0;   
  }
  // prevent repeated signals
  delay(100); 
}


// turn all LEDs off
void Off()
{
  FastLED.clear();
  FastLED.show();
}




// -------------------- LED SEQUENCES -------------------- //


// sequence that runs on arduino startup
void BootSequence()
{
  Off();
  delay(200);
  
  // slowly increase red brightness of back LEDs
  for (int i = 0; i < 255; i++)
  {
    for (int j = 0; j < 99; j++)
    {
      if (leds[j].red + 1 > 255)
        leds[j].red = 255;
      else
        leds[j].red += 1;
    }

    // begin increasing red brightness of GPU LEDs after 10 loops
    if (i > 10)
    {
      for (int j = 100; j < 112; j++)
      {
        if (leds[j].red + 1 > 255)
          leds[j].red = 255;
        else
          leds[j].red += 1;
      }
    }
    delay(10);
    FastLED.show();
  }

  //finish GPU animation
  for (int i = 0; i < 10; i++)
  {
    for (int j = 100; j < 112; j++)
    {
      if (leds[j].red + 1 > 255)
        leds[j].red = 255;
      else
        leds[j].red += 1;
    }
    FastLED.show();
  }

  // transition to Red() LED mode
  ledMode = 0;
}


// simple all-red preset
void Red()
{
  // only update LEDs once
  if (leds[0].red != 255)
	  for (int i = 0; i < NUM_LEDS; i++)
		  leds[i] = CRGB(255, 0, 0);
  
	FastLED.show();
}

// simple all-red preset (dimmed)
void RedDimmed()
{
  // only update LEDs once
  if (leds[0].red != 80)
  {
    for (int i = 0; i < 100; i++)
      leds[i] = CRGB(40, 0, 0);
    for (int i = 100; i < 112; i++)
      leds[i] = CRGB(255, 0, 0);
  }
  
  FastLED.show();
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// a more dynamic visualizer utilizing the full color range
void DynamicVisualizer()
{
  if (Serial.available())
  {
    // audioIn is typically a value from 0-127 (but can be as high as 255)
    audioIn = Serial.read();

    // shift LEDs away from the center LED (37)
    for (int i = 0; i < 37; i++)
      leds[i] = leds[i+1];
    for (int i = 74; i > 37; i--)
      leds[i] = leds[i-1];

    // normalize audioIn to get closer to 255
    if (audioIn*2 > 255)
      audioIn = 255;
    else
      audioIn *= 2;


    // shift the dynamic hue by a factor of the audioIn strength
    if (audioIn > 240)
    {
      for (int i = audioIn/15; i > 0; i--)
      {
        if (dynamicHue.hue == 255)
          dynamicHue.hue = 0;
        else
          dynamicHue.hue++;
      }
    }

    else
    {
      for (int i = audioIn/100; i > 0; i--)
      {
        if (dynamicHue.hue == 255)
          dynamicHue.hue = 0;
        else
          dynamicHue.hue++;
      }
    }

    // apply brightness adjustment
    dynamicHue.val = audioIn;

    // set center LED
    leds[37] = dynamicHue;
    
    if (leds[100].red + audioIn/5 <= 255)
    {
      for (int i = 75; i < 112; i++)        
        leds[i].red += audioIn/5;
    }
    
    else
    {
      for (int i = 75; i < 112; i++)        
        leds[i].red = 255;
    }
       
    FastLED.show();

    if (leds[100].red < 8 )
      for (int i = 75; i < 112; i++)
        leds[i] = CRGB(0, 0, 0);

    if (leds[100].red > 0)
      for (int i = 75; i < 112; i++)
        leds[i].red -= 8;
  }
}


// NOTE: Arduino should be plugged into a computer running AudioInputProcessor.exe for this mode.
// a relaxed visualizer utilizing only red channels
void MellowVisualizer()
{
  if (Serial.available())
  {
    audioIn = Serial.read();
    int centerLED = 37;

    for (int i = 0; i < centerLED; i++)
      leds[i].red = leds[i+1].red;
    for (int i = 74; i > centerLED; i--)
      leds[i].red = leds[i-1].red;

    if (audioIn*2 > 255)
      audioIn = 255;
    else
      audioIn *= 2;

    leds[centerLED].red = audioIn;

    if (leds[100].red + audioIn/5 <= 255)
    {
      for (int i = 100; i < 112; i++)        
        leds[i].red += audioIn/5;
    }
    
    else
    {
      for (int i = 100; i < 112; i++)        
        leds[i].red = 255;
    }
       
    FastLED.show();

    if (leds[100].red < 8 )
      for (int i = 100; i < 112; i++)
        leds[i] = CRGB(0, 0, 0);

    if (leds[100].red > 0)
      for (int i = 100; i < 112; i++)
        leds[i].red -= 8;
  }
}
