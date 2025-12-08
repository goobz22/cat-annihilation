// Minimal OpenAL type stubs for compilation checking without OpenAL
// This file provides just enough types to make headers parse without linking
#ifndef OPENAL_STUBS_H
#define OPENAL_STUBS_H

#include <stdint.h>

// OpenAL types
typedef char ALboolean;
typedef char ALchar;
typedef signed char ALbyte;
typedef unsigned char ALubyte;
typedef short ALshort;
typedef unsigned short ALushort;
typedef int ALint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;
typedef float ALfloat;
typedef double ALdouble;
typedef void ALvoid;

// OpenAL constants
#define AL_NONE 0
#define AL_FALSE 0
#define AL_TRUE 1

// Error codes
#define AL_NO_ERROR 0
#define AL_INVALID_NAME 0xA001
#define AL_INVALID_ENUM 0xA002
#define AL_INVALID_VALUE 0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY 0xA005

// Source properties
#define AL_POSITION 0x1004
#define AL_VELOCITY 0x1006
#define AL_GAIN 0x100A
#define AL_PITCH 0x1003
#define AL_LOOPING 0x1007
#define AL_BUFFER 0x1009
#define AL_SOURCE_STATE 0x1010

// Source states
#define AL_INITIAL 0x1011
#define AL_PLAYING 0x1012
#define AL_PAUSED 0x1013
#define AL_STOPPED 0x1014

// Buffer properties
#define AL_FREQUENCY 0x2001
#define AL_BITS 0x2002
#define AL_CHANNELS 0x2003
#define AL_SIZE 0x2004

// Buffer formats
#define AL_FORMAT_MONO8 0x1100
#define AL_FORMAT_MONO16 0x1101
#define AL_FORMAT_STEREO8 0x1102
#define AL_FORMAT_STEREO16 0x1103

// Listener properties
#define AL_ORIENTATION 0x100F

// Distance models
#define AL_DISTANCE_MODEL 0xD000
#define AL_INVERSE_DISTANCE 0xD001
#define AL_INVERSE_DISTANCE_CLAMPED 0xD002
#define AL_LINEAR_DISTANCE 0xD003
#define AL_LINEAR_DISTANCE_CLAMPED 0xD004
#define AL_EXPONENT_DISTANCE 0xD005
#define AL_EXPONENT_DISTANCE_CLAMPED 0xD006

// Context types (ALC)
typedef struct ALCdevice ALCdevice;
typedef struct ALCcontext ALCcontext;
typedef char ALCboolean;
typedef char ALCchar;
typedef signed char ALCbyte;
typedef unsigned char ALCubyte;
typedef short ALCshort;
typedef unsigned short ALCushort;
typedef int ALCint;
typedef unsigned int ALCuint;
typedef int ALCsizei;
typedef int ALCenum;
typedef float ALCfloat;
typedef double ALCdouble;
typedef void ALCvoid;

// ALC constants
#define ALC_FALSE 0
#define ALC_TRUE 1
#define ALC_FREQUENCY 0x1007
#define ALC_REFRESH 0x1008
#define ALC_SYNC 0x1009

// Stub function declarations
#ifdef __cplusplus
extern "C" {
#endif

// AL functions
void alGenBuffers(ALsizei n, ALuint* buffers);
void alDeleteBuffers(ALsizei n, const ALuint* buffers);
ALboolean alIsBuffer(ALuint buffer);
void alBufferData(ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq);

void alGenSources(ALsizei n, ALuint* sources);
void alDeleteSources(ALsizei n, const ALuint* sources);
ALboolean alIsSource(ALuint source);

void alSourcef(ALuint source, ALenum param, ALfloat value);
void alSourcefv(ALuint source, ALenum param, const ALfloat* values);
void alSourcei(ALuint source, ALenum param, ALint value);
void alGetSourcei(ALuint source, ALenum param, ALint* value);

void alSourcePlay(ALuint source);
void alSourceStop(ALuint source);
void alSourcePause(ALuint source);
void alSourceRewind(ALuint source);

void alListenerf(ALenum param, ALfloat value);
void alListenerfv(ALenum param, const ALfloat* values);

ALenum alGetError(void);

// ALC functions
ALCdevice* alcOpenDevice(const ALCchar* devicename);
ALCboolean alcCloseDevice(ALCdevice* device);
ALCcontext* alcCreateContext(ALCdevice* device, const ALCint* attrlist);
ALCboolean alcMakeContextCurrent(ALCcontext* context);
void alcDestroyContext(ALCcontext* context);
ALCcontext* alcGetCurrentContext(void);
ALCdevice* alcGetContextsDevice(ALCcontext* context);

#ifdef __cplusplus
}
#endif

#endif // OPENAL_STUBS_H
