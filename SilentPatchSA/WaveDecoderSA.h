#pragma once

#include "AudioHardwareSA.h"

class CAEWaveDecoder final : public CAEStreamingDecoder
{
private:
	uint32_t	m_dataSize;
	uint32_t	m_offsetToData;
	size_t		m_maxBlockSize = 0;
	uint8_t*	m_buffer = nullptr;

	struct FormatChunk
	{
		uint16_t		audioFormat;
		uint16_t		numChannels;
		uint32_t		sampleRate;
		uint32_t		byteRate;
		uint16_t		blockAlign;
		uint16_t		bitsPerSample;
	}				m_formatChunk;

public:
	CAEWaveDecoder(CAEDataStream* stream)
		: CAEStreamingDecoder(stream)
	{}

	virtual ~CAEWaveDecoder() override
	{
		delete[] m_buffer;
	}

	virtual bool			Initialise() override;
	virtual uint32_t		FillBuffer(void* pBuf, uint32_t nLen) override;

	virtual uint32_t		GetStreamLengthMs() const override
	{ return (static_cast<uint64_t>(m_dataSize) * 1000) / m_formatChunk.blockAlign; }
	virtual uint32_t		GetStreamPlayTimeMs() const override
	{ return (static_cast<uint64_t>(GetStream()->GetCurrentPosition() - m_offsetToData) * 1000) / m_formatChunk.blockAlign; }

	virtual void			SetCursor(uint32_t nTime) override
	{
		uint64_t sampleNum = (static_cast<uint64_t>(nTime) * m_formatChunk.sampleRate) / 1000;
		GetStream()->Seek( m_offsetToData + (static_cast<uint32_t>(sampleNum) * m_formatChunk.blockAlign), FILE_BEGIN );
	}

	virtual uint32_t		GetSampleRate() const override
	{ return m_formatChunk.sampleRate; }

	virtual uint32_t		GetStreamID() const override
	{ return GetStream()->GetID(); }
	
private:
	size_t					CalcBufferSize( uint32_t outputBuf );
};