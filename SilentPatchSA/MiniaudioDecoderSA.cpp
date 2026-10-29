#include "StdAfxSA.h"
#include "MiniaudioDecoderSA.h"

ma_result CAEMiniaudioDecoder::onRead(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead)
{
	CAEMiniaudioDecoder*	pClientData = static_cast<CAEMiniaudioDecoder*>(pDecoder->pUserData);
	DWORD					bytesRead = 0;

	if ( pBytesRead != nullptr )
		*pBytesRead = 0;

	if ( ReadFile(pClientData->GetStream()->GetFile(), pBufferOut, DWORD(bytesToRead), &bytesRead, nullptr) == FALSE )
		return MA_IO_ERROR;

	if ( pBytesRead != nullptr )
		*pBytesRead = bytesRead;

	// No more data available, let miniaudio know we hit the end of the file
	return bytesRead == 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result CAEMiniaudioDecoder::onSeek(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin)
{
	CAEMiniaudioDecoder*	pClientData = static_cast<CAEMiniaudioDecoder*>(pDecoder->pUserData);
	LARGE_INTEGER			filePosition;

	DWORD moveMethod = FILE_CURRENT;
	switch ( origin )
	{
	case ma_seek_origin_start:
		moveMethod = FILE_BEGIN;
		break;
	case ma_seek_origin_end:
		moveMethod = FILE_END;
		break;
	default:
		break;
	}

	filePosition.QuadPart = byteOffset;
	if ( SetFilePointerEx(pClientData->GetStream()->GetFile(), filePosition, nullptr, moveMethod) == FALSE )
		return MA_IO_ERROR;

	return MA_SUCCESS;
}

bool CAEMiniaudioDecoder::Initialise()
{
	ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 2, 0);
	config.encodingFormat = ma_encoding_format_flac;

	if ( ma_decoder_init(onRead, onSeek, this, &config, &m_decoder) != MA_SUCCESS )
		return false;

	m_bDecoderInitialised = true;

	m_sampleRate = m_decoder.outputSampleRate;
	if ( m_sampleRate == 0 || m_sampleRate > 48000 )
		return false;

	// FLAC stores the total amount of samples in its STREAMINFO block,
	// so the length should always be available. Only used for GetStreamLengthMs.
	ma_uint64 totalFrames = 0;
	if ( ma_decoder_get_length_in_pcm_frames(&m_decoder, &totalFrames) == MA_SUCCESS )
		m_totalFrames = totalFrames;

	m_playedFrames = 0;
	return true;
}

uint32_t CAEMiniaudioDecoder::FillBuffer(void* pBuf, uint32_t nLen)
{
	if ( !m_bDecoderInitialised )
		return 0;

	const ma_uint64	framesToRead = nLen / (2 * sizeof(int16_t));
	ma_uint64		framesRead = 0;

	const ma_result result = ma_decoder_read_pcm_frames(&m_decoder, pBuf, framesToRead, &framesRead);
	if ( result != MA_SUCCESS && result != MA_AT_END )
		return 0;

	m_playedFrames += framesRead;
	return uint32_t(framesRead * 2 * sizeof(int16_t));
}

void CAEMiniaudioDecoder::SetCursor(uint32_t nTime)
{
	if ( !m_bDecoderInitialised || m_sampleRate == 0 )
		return;

	ma_uint64 frameIndex = (ma_uint64(nTime) * m_sampleRate) / 1000;
	if ( m_totalFrames != 0 && frameIndex > m_totalFrames )
		frameIndex = m_totalFrames;

	if ( ma_decoder_seek_to_pcm_frame(&m_decoder, frameIndex) == MA_SUCCESS )
		m_playedFrames = frameIndex;
}

CAEMiniaudioDecoder::~CAEMiniaudioDecoder()
{
	if ( m_bDecoderInitialised )
	{
		ma_decoder_uninit(&m_decoder);
		m_bDecoderInitialised = false;
	}
}
