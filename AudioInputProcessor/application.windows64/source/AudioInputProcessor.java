import processing.core.*; 
import processing.data.*; 
import processing.event.*; 
import processing.opengl.*; 

import ddf.minim.*; 
import processing.serial.*; 

import java.util.HashMap; 
import java.util.ArrayList; 
import java.io.File; 
import java.io.BufferedReader; 
import java.io.PrintWriter; 
import java.io.InputStream; 
import java.io.OutputStream; 
import java.io.IOException; 

public class AudioInputProcessor extends PApplet {

/*
*   Written by Patrick Hirsh, August 2018
*    
*   LED Visualizer Audio Input Processor
*    
*   This script takes audio signal from the default windows recording device
*   (ideally Stereo Mix), normalizes it, and converts it into a byte value. This
*   value is then written to the specified port for use in an LED Visualizer. 
*   For best results, tweak the powVal and scaleVal globals such that sendVal 
*   values *just* reach 255 at the loudest point of your audio.
*
*   Thanks to Abhik Pal for the Arduino serial port communication template
*/

// Import the Minim and Serial Libraries



// create audio processing and serial port communication objects
Minim minim;
AudioInput in;
Serial port;

int sendVal = 0;            // initialize the value to send
int sampleCount = 200;      // number of adjacent bytes to sample (more = smoother)
float scaleVal = .8f;        // scale all samples by this amount
float powVal = 2.2f;         // raise final output brightness to this power. Higher powVal = greater distribution


public void setup()
{
  // "COM4" should be set to the port your arduino is connected to
  minim = new Minim(this);
  in = minim.getLineIn();
  port = new Serial(this, "COM4", 9600);
}


public void draw()
{
  // read from the LineIn buffer
  for (int i = 0; i < (in.bufferSize()/1000)*width - 1; i++)
  {   
    // obtain samples from stereo source and average them. Scale these raw values
    // abs() is necessary to flip the negative samples (since sound wave voltage oscillates about 0V)
    float rightAvg = 0;
    for (int j = 0; j < sampleCount; j++)
      rightAvg += abs(in.right.get(i+j)*scaleVal);
    rightAvg /= sampleCount;
    
    float leftAvg = 0;
    for (int j = 0; j < sampleCount; j++)
      leftAvg += abs(in.left.get(i+j)*scaleVal);
    leftAvg /= sampleCount;
    
    // average both sides since we can only work with one-byte writes
    float avg = (rightAvg + leftAvg)/2; 
    
    // voltage is between -1 and 1, so we multiply by 255 to normalize
    sendVal = (int)(pow(avg*255, powVal));
    
    // powVal can (if not tuned correctly) generate values over 255. Ensure we don't do that
    if (sendVal > 255)
      sendVal = 255;
  }
  
  //println(sendVal);
  port.write(sendVal);
}
  static public void main(String[] passedArgs) {
    String[] appletArgs = new String[] { "AudioInputProcessor" };
    if (passedArgs != null) {
      PApplet.main(concat(appletArgs, passedArgs));
    } else {
      PApplet.main(appletArgs);
    }
  }
}
