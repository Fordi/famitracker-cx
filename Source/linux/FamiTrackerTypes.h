/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2010  Jonathan Liss
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/


#pragma once

#include <stdlib.h>

#define MIDI_NOTE(octave, note)		((octave) * 12 + (note) - 1)
#define GET_OCTAVE(midi_note)		((midi_note) / 12)
#define GET_NOTE(midi_note)			((midi_note) % 12 + 1)

/*
 * Here are the constants that defines the limits in the tracker
 * change if needed (some might cause side effects)
 *
 */

// Maximum number of instruments to use
const int MAX_INSTRUMENTS = 64;

// Maximum number of sequence lists
const int MAX_SEQUENCES	= 128;

// Maximum number of items in each sequence
const int MAX_SEQUENCE_ITEMS = /*128*/ 253;		// TODO: need to check if this exports correctly

// Maximum number of patterns per channel
const int MAX_PATTERN = 128;

// Maximum number of frames
const int MAX_FRAMES = 128;

// Maximum length of patterns (in rows)
const int MAX_PATTERN_LENGTH = 256;

// Maximum number of dmc samples (this would be more limited by NES memory)
const int MAX_DSAMPLES = 64;

// Sample space available (from $C000-$FFFF) Will be increased when bankswitching is supported
const int MAX_SAMPLE_SPACE = 0x4000;

// Number of effect columns allowed
const int MAX_EFFECT_COLUMNS = 4;

// Maximum numbers of tracks allowed (NSF limit is 256, but dunno if the bankswitcher can handle that)
const unsigned int MAX_TRACKS = 64;

// Max tempo
const int MAX_TEMPO	= 255;

// Min tempo
const int MIN_TEMPO	= 32;

// Max speed
const int MAX_SPEED = 31;

// Min speed
const int MIN_SPEED = 1;


// Number of avaliable channels (max) TODO: should not be used anymore!
// instead, check the channelsavailable variable and allocate dynamically
const int MAX_CHANNELS	 = 5 + 3 + 2 + 6 + 1 + 8 + 3;		

const int CHANNELS_DEFAULT = 5;
const int CHANNELS_VRC6	   = 3;
const int CHANNELS_VRC7	   = 6;

const int OCTAVE_RANGE = 8;
const int NOTE_RANGE   = 12;

// Sequence types (shared with VRC6)

enum {
	SEQ_VOLUME,
	SEQ_ARPEGGIO,
	SEQ_PITCH,
	SEQ_HIPITCH,		// TODO: remove this eventually
	SEQ_DUTYCYCLE,

	SEQ_COUNT
};

// New sequence types
/*
enum {
	SEQ_VOLUME,
	SEQ_ARPEGGIO,
	SEQ_PITCH,
	SEQ_HIPITCH,		// TODO: remove this eventually
	SEQ_DUTYCYCLE,
	SEQ_SUNSOFT_NOISE,

	SEQ_COUNT
};
*/
//const int SEQ_SUNSOFT_NOISE = SEQ_DUTYCYCLE + 1;

// Channel effects
enum {
	EF_NONE = 0,
	EF_SPEED,
	EF_JUMP,
	EF_SKIP,
	EF_HALT,
	EF_VOLUME,
	EF_PORTAMENTO,
	EF_PORTAOFF,		// unused!!
	EF_SWEEPUP,
	EF_SWEEPDOWN,
	EF_ARPEGGIO,
	EF_VIBRATO,
	EF_TREMOLO,
	EF_PITCH,
	EF_DELAY,
	EF_DAC,
	EF_PORTA_UP,
	EF_PORTA_DOWN,
	EF_DUTY_CYCLE,
	EF_SAMPLE_OFFSET,
	EF_SLIDE_UP,
	EF_SLIDE_DOWN,
	EF_VOLUME_SLIDE,
	EF_NOTE_CUT,
	EF_RETRIGGER,

	EF_DELAYED_VOLUME,	

	EF_FDS_MOD_DEPTH,
	EF_FDS_MOD_SPEED_HI,
	EF_FDS_MOD_SPEED_LO,

	EF_DPCM_PITCH,

	EF_COUNT
};

// DPCM  effects
//const int EF_DPCM_PITCH = EF_SWEEPUP;		// DPCM pitch, 'H'

//const int EF_VRC7_PATCH = EF_DUTY_CYCLE;	// VRC7 patch setting, 'V'

// FDS effects
//const int EF_FDS_MOD_DEPTH = EF_SWEEPUP;	// FDS modulation depth, 'H'

//const int EF_RETRIGGER = EF_SWEEPDOWN;

// Channel effect letters
const char EFF_CHAR[] = {'F',	// Speed
						 'B',	// Jump 
						 'D',	// Skip 
						 'C',	// Halt
						 'E',	// Volume
						 '3',	// Porta on
						 ' ',	// Porta off		// unused
						 'H',	// Sweep up
						 'I',	// Sweep down
						 '0',	// Arpeggio
						 '4',	// Vibrato
						 '7',	// Tremolo
						 'P',	// Pitch
						 'G',	// Note delay
						 'Z',	// DAC setting
						 '1',	// Portamento up
						 '2',	// Portamento down
						 'V',	// Duty cycle
						 'Y',	// Sample offset
						 'Q',	// Slide up
						 'R',	// Slide down
						 'A',	// Volume slide
						 'S',	// Note cut
						 'X',	// DPCM retrigger						 
						 ' ',	// (TODO, delayed volume)
						 'H',	// FDS modulation depth
						 'I',	// FDS modulation speed hi
						 'J',	// FDS modulation speed lo
						 'W'	// DPCM Pitch
						 
};


enum {
	NONE = 0,	// No note
	C, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B,
	RELEASE,	// Release, begin note release sequence
	HALT		// Halt, stops note
};

enum {
	NTSC,
	PAL
};

enum {
	VIBRATO_OLD = 0,
	VIBRATO_NEW
};


// DPCM sample
class CDSample {
public:
	// Empty constructor
	CDSample();

	// Unnamed sample constructor
	CDSample(int Size, char *pData = NULL);

	// Copy constructor
	CDSample(CDSample &sample);

	// Destructor
	~CDSample();

	// Copy from existing sample
	void Copy(const CDSample *pDSample);
	
	// Allocate memory
	void Allocate(int iSize, char *pData = NULL);

	// Sample data & name
	// TODO: make these private
	unsigned int SampleSize;
	char *SampleData;
	char Name[256];

	// Max size of samples supported by NES, in bytes
	static const int MAX_SIZE = 0x0FF1;
};