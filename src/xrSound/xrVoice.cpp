#include "stdafx.h"
#include "xrVoice.h"

#include "openal/alc.h"
#include "openal/al.h"
  
ALCdevice* device_capture;
const int SRATE = 44100;
const int SSIZE = 1024;

ALbyte buffer[22050];
ALint sample;

xrVoice::xrVoice()
{
	 
	device_capture = alcCaptureOpenDevice(NULL, SRATE, AL_FORMAT_STEREO16, SSIZE);


	
	
}

xrVoice::~xrVoice()
{

}

void xrVoice::InitCapture()
{
}

void xrVoice::capture()
{
	
}
