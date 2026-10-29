#pragma once

#include "AudioHardwareSA.h"

#include "miniaudio.h"

class CAEMiniaudioDecoder final : public CAEStreamingDecoder
{
private:
	ma_decoder		m_decoder;
	bool			m_bDecoderInitialised = false;
	uint32_t		m_sampleRate = 0;
	uint64_t		m_totalFrames = 0;
	uint64_t		m_playedFrames = 0;

private:
	static ma_result	onRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead);
	static ma_result	onSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin);

public:
	CAEMiniaudioDecoder(CAEDataStream* stream)
		: CAEStreamingDecoder(stream)
	{}

	virtual					~CAEMiniaudioDecoder() override;
	virtual bool			Initialise() override;
	virtual uint32_t		FillBuffer(void* pBuf, uint32_t nLen) override;
	virtual uint32_t		GetStreamLengthMs() const override
	{ return m_sampleRate != 0 ? uint32_t((m_totalFrames * 1000) / m_sampleRate) : 0; }
	virtual uint32_t		GetStreamPlayTimeMs() const override
	{ return m_sampleRate != 0 ? uint32_t((m_playedFrames * 1000) / m_sampleRate) : 0; }
	virtual void			SetCursor(uint32_t nTime) override;
	virtual uint32_t		GetSampleRate() const override
	{ return m_sampleRate; }
	virtual uint32_t		GetStreamID() const override
	{ return GetStream()->GetID(); }
};
