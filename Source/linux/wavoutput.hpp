#ifndef _WAVOUTPUT_HPP_
#define _WAVOUTPUT_HPP_

#include "sound.hpp"
#include "Document.hpp"

class WavOutput : public SoundSink
{
public:
	WavOutput(IO *io, int channels, int sampleRate);
	void FlushBuffer(int16 *Buffer, uint32 Size);

	void finalize();
private:
	IO *m_io;
	Quantity m_size;
};

#endif