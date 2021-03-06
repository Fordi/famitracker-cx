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

//
// The NES APU + Expansion chips (2A03/2A07) emulation core
//
// Written by Jonathan Liss 2002 - 2003
//
// Briefly about how the audio stream is handled
//
//  To set up sound, call AllocateBuffer with desired sample rate, stereo and speed (NTSC/PAL).
//  At every end of frame, call EndFrame. It will then call the parent to flush the buffer with about one frame
//  worth of sound.
//  The sound is rendered in 32 bit signed format.
//  Speed may be changed during playback. No buffers needs to be reallocated.
//
// Mail: zxy965r@tninet.se
//
// TODO:
//

#include <vector>
#include "../types.hpp"
#include <cstdio>
#include <memory>
#include <cmath>
#include "APU.h"
#include "core/soundsink.hpp"

#include "Square.h"
#include "Triangle.h"
#include "Noise.h"
#include "DPCM.h"

#include "VRC6.h"
#include "MMC5.h"
#include "FDS.h"
#include "N106.h"
#include "VRC7.h"
//#include "S5B.h"

using std::min;
using std::max;

const int	 CAPU::SEQUENCER_PERIOD		= 7458;
//const int	 CAPU::SEQUENCER_PERIOD_PAL	= 7458;			// ????
const uint32 CAPU::BASE_FREQ_NTSC		= 1789773;		// 72.667
const uint32 CAPU::BASE_FREQ_PAL		= 1662607;
const uint8	 CAPU::FRAME_RATE_NTSC		= 60;
const uint8	 CAPU::FRAME_RATE_PAL		= 50;

const uint8 CAPU::LENGTH_TABLE[] = {
	0x0A, 0xFE, 0x14, 0x02, 0x28, 0x04, 0x50, 0x06,
	0xA0, 0x08, 0x3C, 0x0A, 0x0E, 0x0C, 0x1A, 0x0E,
	0x0C, 0x10, 0x18, 0x12, 0x30, 0x14, 0x60, 0x16,
	0xC0, 0x18, 0x48, 0x1A, 0x10, 0x1C, 0x20, 0x1E
};

static std::vector<CExternal*> ExChips;

CAPU::CAPU(CSampleMem *pSampleMem) :
	m_pParent(NULL),
	m_iFrameCycles(0),
	m_pSoundBuffer(NULL),
	m_pMixer(new CMixer()),
	m_iExternalSoundChip(0),
	m_iCyclesToRun(0)
{
	m_pSquare1 = new CSquare(m_pMixer, CHANID_SQUARE1, SNDCHIP_NONE);
	m_pSquare2 = new CSquare(m_pMixer, CHANID_SQUARE2, SNDCHIP_NONE);
	m_pTriangle = new CTriangle(m_pMixer, CHANID_TRIANGLE);
	m_pNoise = new CNoise(m_pMixer, CHANID_NOISE);
	m_pDPCM = new CDPCM(m_pMixer, pSampleMem, CHANID_DPCM);

	m_pMMC5 = new CMMC5(m_pMixer);
	m_pVRC6 = new CVRC6(m_pMixer);
	m_pVRC7 = new CVRC7(m_pMixer);
	m_pFDS = new CFDS(m_pMixer);
	m_pN106 = new CN106(m_pMixer);
//	m_pS5B = new CS5B(m_pMixer);

	m_fLevelVRC7 = 1.0f;
	m_fLevelS5B = 1.0f;

#ifdef LOGGING
	m_pLog = new CFile("apu_log.txt", CFile::modeCreate | CFile::modeWrite);
#endif
}

CAPU::~CAPU()
{
	SAFE_RELEASE(m_pSquare1);
	SAFE_RELEASE(m_pSquare2);
	SAFE_RELEASE(m_pTriangle);
	SAFE_RELEASE(m_pNoise);
	SAFE_RELEASE(m_pDPCM);

	SAFE_RELEASE(m_pMMC5);
	SAFE_RELEASE(m_pVRC6);
	SAFE_RELEASE(m_pVRC7);
	SAFE_RELEASE(m_pFDS);
	SAFE_RELEASE(m_pN106);
//	SAFE_RELEASE(m_pS5B);

	SAFE_RELEASE(m_pMixer);

	SAFE_RELEASE_ARRAY(m_pSoundBuffer);

#ifdef LOGGING
	m_pLog->Close();
	delete m_pLog;
#endif
}

inline void CAPU::Clock_240Hz()
{
	// 240Hz Frame counter (1/4 frame)
	//

	m_pSquare1->EnvelopeUpdate();
	m_pSquare2->EnvelopeUpdate();
	m_pNoise->EnvelopeUpdate();
	m_pTriangle->LinearCounterUpdate();
}

inline void CAPU::Clock_120Hz()
{
	// 120Hz Frame counter (1/2 frame)
	//

	m_pSquare1->SweepUpdate(1);
	m_pSquare2->SweepUpdate(0);

	m_pSquare1->LengthCounterUpdate();
	m_pSquare2->LengthCounterUpdate();
	m_pTriangle->LengthCounterUpdate();
	m_pNoise->LengthCounterUpdate();
}

inline void CAPU::Clock_60Hz()
{
	// 60Hz Frame counter (1/1 frame)
	//

	// No IRQs are generated for NSFs
}

inline void CAPU::ClockSequence()
{
	// The frame sequencer
	//

	m_iSequencerClock += SEQUENCER_PERIOD;

	if (m_iFrameMode == 0)
	{
		m_iFrameSequence = (m_iFrameSequence + 1) % 4;
		switch (m_iFrameSequence)
		{
			case 0: Clock_240Hz(); break;
			case 1: Clock_240Hz(); Clock_120Hz(); break;
			case 2: Clock_240Hz(); break;
			case 3: Clock_240Hz(); Clock_120Hz(); Clock_60Hz(); break;
		}
	}
	else {
		m_iFrameSequence = (m_iFrameSequence + 1) % 5;
		switch (m_iFrameSequence)
		{
			case 0: Clock_240Hz(); Clock_120Hz(); break;
			case 1: Clock_240Hz(); break;
			case 2: Clock_240Hz(); Clock_120Hz(); break;
			case 3: Clock_240Hz(); break;
			case 4: break;
		}
	}
}

void CAPU::Process()
{
	// The main APU emulation
	//
	// The amount of cycles that will be emulated is added by CAPU::AddCycles
	//
	
	static uint32 Time, i;

	while (m_iCyclesToRun > 0)
	{
		Time = m_iCyclesToRun;

		if (Time > m_iSequencerClock)
			Time = m_iSequencerClock;
		if (Time > m_iFrameClock)
			Time = m_iFrameClock;
		
		// Fixes the problem with distortion due to volume modulation
		i = Time;
		while (i > 0)
		{
			uint32 Period = min(m_pSquare1->GetPeriod(), m_pSquare2->GetPeriod());
			Period = min(max<uint32>(Period, 7), i);
			m_pSquare1->Process(Period);
			m_pSquare2->Process(Period);
			i -= Period;
		}

		i = Time;
		while (i > 0)
		{
			uint32 Period = min(m_pTriangle->GetPeriod(), m_pNoise->GetPeriod());
			Period = min<uint32>(Period, m_pDPCM->GetPeriod());
			Period = min(max<uint32>(Period, 7), i);
			m_pTriangle->Process(Period);
			m_pNoise->Process(Period);
			m_pDPCM->Process(Period);
			i -= Period;
		}

		for (std::vector<CExternal*>::iterator iter = ExChips.begin(); iter != ExChips.end(); ++iter)
		{
			(*iter)->Process(Time);
		}

		m_iFrameCycles		+= Time;
		m_iSequencerClock	-= Time;
		m_iFrameClock		-= Time;
		m_iCyclesToRun		-= Time;

		if (m_iSequencerClock == 0)
			ClockSequence();

		if (m_iFrameClock == 0)
			EndFrame();
	}
}

// End of audio frame, flush the buffer if enough samples has been produced, and start a new frame
void CAPU::EndFrame()
{
	// The APU will always output audio in 32 bit signed format
	
	m_pSquare1->EndFrame();
	m_pSquare2->EndFrame();
	m_pTriangle->EndFrame();
	m_pNoise->EndFrame();
	m_pDPCM->EndFrame();

	for (std::vector<CExternal*>::iterator iter = ExChips.begin(); iter != ExChips.end(); ++iter)
	{
		(*iter)->EndFrame();
	}

	int SamplesAvail = m_pMixer->FinishBuffer(m_iFrameCycles);
	int ReadSamples	= m_pMixer->ReadBuffer(SamplesAvail, m_pSoundBuffer, m_bStereoEnabled);
	(*m_pParent)(m_pSoundBuffer, ReadSamples, m_pParentData);
	
	m_iFrameClock /*+*/= m_iFrameCycleCount;
	m_iFrameCycles = 0;

#ifdef LOGGING
	m_iFrame++;
#endif
}

void CAPU::Reset()
{
	// Reset APU
	//
	
	m_iCyclesToRun		= 0;
	m_iFrameCycles		= 0;
	m_iSequencerClock	= SEQUENCER_PERIOD;
	m_iFrameSequence	= 0;
	m_iFrameMode		= 0;
	m_iFrameClock		= m_iFrameCycleCount;
	
	m_pMixer->ClearBuffer();

	m_pSquare1->Reset();
	m_pSquare2->Reset();
	m_pTriangle->Reset();
	m_pNoise->Reset();
	m_pDPCM->Reset();

	for (std::vector<CExternal*>::iterator iter = ExChips.begin(); iter != ExChips.end(); ++iter)
	{
		(*iter)->Reset();
	}

#ifdef LOGGING
	m_iFrame = 0;
#endif
}

void CAPU::SetupMixer(int LowCut, int HighCut, int HighDamp, int Volume) const
{
	// New settings
	m_pMixer->UpdateSettings(LowCut, HighCut, HighDamp, Volume);
	m_pVRC7->SetVolume((float(Volume) / 100.0f) * m_fLevelVRC7);
}

void CAPU::SetExternalSound(uint8 Chip)
{
	// Set expansion chip
	m_iExternalSoundChip = Chip;
	m_pMixer->ExternalSound(Chip);

	ExChips.clear();

	if (Chip & SNDCHIP_VRC6)
		ExChips.push_back(m_pVRC6);
	if (Chip & SNDCHIP_VRC7)
		ExChips.push_back(m_pVRC7);
	if (Chip & SNDCHIP_FDS)
		ExChips.push_back(m_pFDS);
	if (Chip & SNDCHIP_MMC5)
		ExChips.push_back(m_pMMC5);
	if (Chip & SNDCHIP_N106)
		ExChips.push_back(m_pN106);
//	if (Chip & SNDCHIP_S5B)
//		ExChips.push_back(m_pS5B);

	Reset();
}

void CAPU::ChangeMachine(int Machine)
{
	// Allow to change speed on the fly
	//

	switch (Machine)
	{
		case MACHINE_NTSC:
			m_pNoise->PERIOD_TABLE = CNoise::NOISE_PERIODS_NTSC;
			m_pDPCM->PERIOD_TABLE = CDPCM::DMC_PERIODS_NTSC;			
			m_pMixer->SetClockRate(BASE_FREQ_NTSC);
			break;
		case MACHINE_PAL:
			m_pNoise->PERIOD_TABLE = CNoise::NOISE_PERIODS_PAL;
			m_pDPCM->PERIOD_TABLE = CDPCM::DMC_PERIODS_PAL;			
			m_pMixer->SetClockRate(BASE_FREQ_PAL);
			break;
	}
}

bool CAPU::SetupSound(int SampleRate, int NrChannels, int Machine)
{
	// Allocate a sound buffer
	//
	// Returns false if a buffer couldn't be allocated
	//
	
	uint32 BaseFreq = (Machine == MACHINE_NTSC) ? BASE_FREQ_NTSC : BASE_FREQ_PAL;
	uint8 FrameRate = (Machine == MACHINE_NTSC) ? FRAME_RATE_NTSC : FRAME_RATE_PAL;

	m_iSoundBufferSamples = uint32(SampleRate / FRAME_RATE_PAL);	// Samples / frame. Allocate for PAL, since it's more
	m_bStereoEnabled	  = (NrChannels == 2);	
	m_iSoundBufferSize	  = m_iSoundBufferSamples * NrChannels;		// Total amount of samples to allocate
	m_iSampleSizeShift	  = (NrChannels == 2) ? 1 : 0;
	m_iBufferPointer	  = 0;

	if (!m_pMixer->AllocateBuffer(m_iSoundBufferSamples, SampleRate, NrChannels))
		return false;

	m_pMixer->SetClockRate(BaseFreq);

	SAFE_RELEASE_ARRAY(m_pSoundBuffer);

	m_pSoundBuffer = new int16[m_iSoundBufferSize << 1];

	if (m_pSoundBuffer == NULL)
		return false;

	ChangeMachine(Machine);

	// VRC7 generates samples on it's own
	m_pVRC7->SetSampleSpeed(SampleRate, BaseFreq, FrameRate);

	// Same for sunsoft
//	m_pS5B->SetSampleSpeed(SampleRate, BaseFreq, FrameRate);

	// Numbers of cycles/audio frame
	m_iFrameCycleCount = BaseFreq / FrameRate;

	return true;
}

void CAPU::AddTime(int32 Cycles)
{
	if (Cycles < 0)
		return;
	m_iCyclesToRun += Cycles;
}

void CAPU::Write(uint16 Address, uint8 Value)
{
	// Data was written to an APU register
	//

	Process();

	if (Address == 0x4015)
	{
		Write4015(Value);
		return;
	}
	else if (Address == 0x4017)
	{
		Write4017(Value);
		return;
	}

	switch (Address & 0x1C)
	{
		case 0x00: m_pSquare1->Write(Address & 0x03, Value); break;
		case 0x04: m_pSquare2->Write(Address & 0x03, Value); break;
		case 0x08: m_pTriangle->Write(Address & 0x03, Value); break;
		case 0x0C: m_pNoise->Write(Address & 0x03, Value); break;
		case 0x10: m_pDPCM->Write(Address & 0x03, Value); break;
	}

	m_iRegs[Address & 0x1F] = Value;

#ifdef LOGGING
	m_iRegs[Address & 0x1F] = Value;
#endif
}

void CAPU::Write4017(uint8 Value)
{
	// The $4017 Control port
	//

	Process();

	// Reset counter
	m_iFrameSequence = 0;

	// Mode 1
	if (Value & 0x80)
	{
		m_iFrameMode = 1;
		// Immediately run all units		
		Clock_240Hz();
		Clock_120Hz();
		Clock_60Hz();
	}
	// Mode 0
	else
		m_iFrameMode = 0;

	// IRQs are not generated when playing NSFs
}

void CAPU::Write4015(uint8 Value)
{
	//  Sound Control ($4015)
	//

	Process();

	m_pSquare1->WriteControl(Value);
	m_pSquare2->WriteControl(Value >> 1);
	m_pTriangle->WriteControl(Value >> 2);
	m_pNoise->WriteControl(Value >> 3);
	m_pDPCM->WriteControl(Value >> 4);
}

uint8 CAPU::Read4015()
{
	// Sound Control ($4015)
	//

	uint8 RetVal;

	Process();

	RetVal = m_pSquare1->ReadControl();
	RetVal |= m_pSquare2->ReadControl() << 1;
	RetVal |= m_pTriangle->ReadControl() << 2;
	RetVal |= m_pNoise->ReadControl() << 3;
	RetVal |= m_pDPCM->ReadControl() << 4;
	RetVal |= m_pDPCM->DidIRQ() << 7;
	
	return RetVal;
}

void CAPU::ExternalWrite(uint16 Address, uint8 Value)
{
	// Data was written to an external sound chip 
	// (this doesn't really belong in the APU but are here for convenience)
	//

	Process();

	for (std::vector<CExternal*>::iterator iter = ExChips.begin(); iter != ExChips.end(); ++iter)
	{
		(*iter)->Write(Address, Value);
	}

	LogExternalWrite(Address, Value);
}

uint8 CAPU::ExternalRead(uint16 Address)
{
	// Data read from an external chip
	//

	uint8 Value(0);
	bool Mapped(false);

	Process();

	for (std::vector<CExternal*>::iterator iter = ExChips.begin(); iter != ExChips.end(); ++iter)
	{
		if (!Mapped)
			Value = (*iter)->Read(Address, Mapped);
	}

	if (!Mapped)
		Value = Address >> 8;	// open bus

	return Value;
}

// Expansion for famitracker

int32 CAPU::GetVol(uint8 Chan) const	
{
	return m_pMixer->GetChanOutput(Chan);
}

uint8 CAPU::GetSamplePos() const
{
	return m_pDPCM->GetSamplePos();
}

uint8 CAPU::GetDeltaCounter() const
{
	return m_pDPCM->GetDeltaCounter();
}

bool CAPU::DPCMPlaying() const
{
	return m_pDPCM->IsPlaying();
}

#ifdef LOGGING
void CAPU::Log()
{
	CString str;
	str.Format("Frame %08i: ", m_iFrame);
	for (int i = 0; i < 0x14; i++)
		str.AppendFormat("%02X ", m_iRegs[i]);
	str.Append("\n");
	m_pLog->Write(str, str.GetLength());
}
#endif

void CAPU::SetChipLevel(int Chip, int Level)
{
	float fLevel = expf(float(Level) / 20.0f);	// dB -> gain

	switch (Chip)
	{
		case SNDCHIP_VRC7:
			m_fLevelVRC7 = fLevel;
			break;
	/*	case SNDCHIP_S5B:
			m_fLevelS5B = fLevel;
			break;*/
		default:
			m_pMixer->SetChipLevel(Chip, fLevel);
	}
}

void CAPU::LogExternalWrite(uint16 Address, uint8 Value)
{
	if (Address >= 0x9000 && Address <= 0x9003)
		m_iRegsVRC6[Address - 0x9000] = Value;
	else if (Address >= 0xA000 && Address <= 0xA003)
		m_iRegsVRC6[Address - 0xA000 + 3] = Value;
	else if (Address >= 0xB000 && Address <= 0xB003)
		m_iRegsVRC6[Address - 0xB000 + 6] = Value;
	else if (Address >= 0x4080 && Address <= 0x408F)
		m_iRegsFDS[Address - 0x4080] = Value;
}

uint8 CAPU::GetReg(int Chip, int Reg) const
{
	switch (Chip)
	{
	case SNDCHIP_NONE:
		return m_iRegs[Reg & 0x1F];
	case SNDCHIP_VRC6:
		return m_iRegsVRC6[Reg & 0x1F];
//	case SNDCHIP_N163:
//		return m_pN163->ReadMem(Reg);
	case SNDCHIP_FDS:
		return m_iRegsFDS[Reg & 0x1F];
	default:
		return 0;
	}
}
