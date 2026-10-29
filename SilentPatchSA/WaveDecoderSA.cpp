#include "StdAfxSA.h"
#include "WaveDecoderSA.h"

bool CAEWaveDecoder::Initialise()
{
	CAEDataStream*		pTheStream = GetStream();
	struct {
		char			sectionID[4];
		uint32_t		sectionSize;
	}				chunkHeader;

	// Find fmt section
	pTheStream->Seek(12, FILE_BEGIN);

	pTheStream->FillBuffer(&chunkHeader, sizeof(chunkHeader));
	m_offsetToData = 12 + sizeof(chunkHeader);

	while ( chunkHeader.sectionID[0] != 'f' || chunkHeader.sectionID[1] != 'm' || chunkHeader.sectionID[2] != 't' || chunkHeader.sectionID[3] != ' ' )
	{
		m_offsetToData += sizeof(chunkHeader) + chunkHeader.sectionSize;

		pTheStream->Seek(chunkHeader.sectionSize, FILE_CURRENT);
		pTheStream->FillBuffer(&chunkHeader, sizeof(chunkHeader));

	}

	// Read fmt header
	pTheStream->FillBuffer(&m_formatChunk, sizeof(m_formatChunk));
	m_offsetToData += sizeof(m_formatChunk);

	// Now skip through the rest of a chunk
	if ( chunkHeader.sectionSize - sizeof(m_formatChunk) > 0 )
	{
		m_offsetToData += chunkHeader.sectionSize - sizeof(m_formatChunk);
		pTheStream->Seek(chunkHeader.sectionSize - sizeof(m_formatChunk), FILE_CURRENT);
	}

	// Find data chunk
	m_offsetToData += sizeof(chunkHeader);
	pTheStream->FillBuffer(&chunkHeader, sizeof(chunkHeader));

	while ( chunkHeader.sectionID[0] != 'd' || chunkHeader.sectionID[1] != 'a' || chunkHeader.sectionID[2] != 't' || chunkHeader.sectionID[3] != 'a' )
	{
		m_offsetToData += sizeof(chunkHeader) + chunkHeader.sectionSize;

		pTheStream->Seek(chunkHeader.sectionSize, FILE_CURRENT);
		pTheStream->FillBuffer(&chunkHeader, sizeof(chunkHeader));
	}

	m_dataSize = chunkHeader.sectionSize;

	return m_formatChunk.sampleRate <= 48000 && m_formatChunk.numChannels <= 2 && (m_formatChunk.bitsPerSample == 8 || m_formatChunk.bitsPerSample == 16 || m_formatChunk.bitsPerSample == 24);
}

uint32_t CAEWaveDecoder::FillBuffer(void* pBuf, uint32_t nLen)
{
	size_t curBlockSize = CalcBufferSize( nLen );
	if ( curBlockSize > m_maxBlockSize )
	{
		delete[] m_buffer;
		m_buffer = new uint8_t[curBlockSize];
		m_maxBlockSize = curBlockSize;
	}
	else if ( curBlockSize == 0 )
	{
		return GetStream()->FillBuffer( pBuf, nLen );
	}

	uint32_t bytesRead = GetStream()->FillBuffer( m_buffer, curBlockSize );
	size_t samplesRead = bytesRead / m_formatChunk.blockAlign;

	if ( m_formatChunk.bitsPerSample == 16 )
	{
		assert( m_formatChunk.numChannels == 1 );
		if ( m_formatChunk.numChannels == 1 )
		{
			int16_t* inputBuf = (int16_t*)m_buffer;
			int16_t* outputBuf = (int16_t*)pBuf;
			for ( size_t i = 0; i < samplesRead; ++i )
			{
				*outputBuf = *(outputBuf+1) = *inputBuf++;
				outputBuf += 2;
			}
			
		}
	}
	else if ( m_formatChunk.bitsPerSample == 8 )
	{
		if ( m_formatChunk.numChannels == 2 )
		{
			uint8_t* inputBuf = (uint8_t*)m_buffer;
			int16_t* outputBuf = (int16_t*)pBuf;
			for ( size_t i = 0; i < samplesRead; ++i )
			{
				*outputBuf++ = (static_cast<signed char>(*inputBuf++) - 128) << 8;
				*outputBuf++ = (static_cast<signed char>(*inputBuf++) - 128) << 8;
			}
		}
		else
		{
			uint8_t* inputBuf = (uint8_t*)m_buffer;
			int16_t* outputBuf = (int16_t*)pBuf;
			for ( size_t i = 0; i < samplesRead; ++i )
			{
				*outputBuf = *(outputBuf+1) = (static_cast<signed char>(*inputBuf++) - 128) << 8;
				outputBuf += 2;
			}
		}
	}
	else if ( m_formatChunk.bitsPerSample == 24 )
	{
		if ( m_formatChunk.numChannels == 2 )
		{
			uint8_t* inputBuf = (uint8_t*)m_buffer;
			int16_t* outputBuf = (int16_t*)pBuf;
			for ( size_t i = 0; i < samplesRead; ++i )
			{
				*outputBuf++ = *(inputBuf+1) | (*(inputBuf+2) << 8); inputBuf += 3;
				*outputBuf++ = *(inputBuf+1) | (*(inputBuf+2) << 8); inputBuf += 3;
			}
		}
		else
		{
			uint8_t* inputBuf = (uint8_t*)m_buffer;
			int16_t* outputBuf = (int16_t*)pBuf;
			for ( size_t i = 0; i < samplesRead; ++i )
			{
				*outputBuf = *(outputBuf+1) = *(inputBuf+1) | (*(inputBuf+2) << 8); inputBuf += 3;
				outputBuf += 2;
			}
		}
	}

	return samplesRead * (2*sizeof(int16_t));
}

size_t CAEWaveDecoder::CalcBufferSize( uint32_t outputBuf )
{
	uint32_t requestedSamples = outputBuf / (2*sizeof(int16_t));
	size_t requiredSize = m_formatChunk.blockAlign * requestedSamples;
	return requiredSize == outputBuf ? 0 : requiredSize;
}