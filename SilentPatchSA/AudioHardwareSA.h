#ifndef __AUDIOHARDWARE
#define __AUDIOHARDWARE

#include "ExternalBindings.hpp"

// IStream
#include <Objidl.h>

enum eDecoderType
{
	DECODER_NULL,
	DECODER_VORBIS,
	DECODER_WAVE,
	DECODER_WINDOWSMEDIA,
	DECODER_QUICKTIME,

	// Custom
	DECODER_FLAC
};

extern ExternalFunc<void (void*)> GTAdelete;

class NOVMT CAEDataStream final : IStream
{
private:
	HANDLE			hHandle;
	char*			pFilename;
	bool			bOpened;
	DWORD			dwCurrentPosition, dwStartPosition;
	DWORD			dwLength;
	DWORD			dwID;
	bool			bEncrypted;
	LONG			nRefCount;

public:
	void			operator delete(void* data)
	{
		// Call SA operator delete
		GTAdelete.Call(data);
	}

	CAEDataStream() = delete;

	// This is handled by GTA so we can leave it that way
	static ExternalMethod<CAEDataStream, bool()> Initialise;

	static bool HasGameBindings() { return EnsureBindings(Initialise, GTAdelete); }

	~CAEDataStream()
	{
		if ( bOpened )
		{
			CloseHandle(hHandle);
			bOpened = false;
		}
		if ( pFilename != nullptr )
		{
			GTAdelete.Call(pFilename);
			pFilename = nullptr;
		}
	}

	inline DWORD	GetID() const
		{ return dwID; }
	inline HANDLE	GetFile() const
		{ return hHandle; }

public:
	// Custom methods
	DWORD		Seek(LONG nToSeek, DWORD nPoint);
	DWORD		FillBuffer(void* pBuf, DWORD nLen);
	DWORD		GetCurrentPosition() const
	{
		LARGE_INTEGER filePointer;
		filePointer.QuadPart = 0;
		SetFilePointerEx( hHandle, filePointer, &filePointer, FILE_CURRENT );
		return DWORD(filePointer.QuadPart - dwStartPosition);
	}
};

class CAEStreamingDecoder
{
private:
	CAEDataStream*		pStream;

public:
	CAEStreamingDecoder(CAEDataStream* stream)
		: pStream(stream)
	{
		if ( stream != nullptr )
			stream->Initialise.Call(stream);
	}

	inline CAEDataStream*	GetStream()
		{ return pStream; }
	inline const CAEDataStream*	GetStream() const
	{ return pStream; }

	virtual bool			Initialise()=0;
	virtual uint32_t		FillBuffer(void* pBuf,uint32_t nLen)=0;

	virtual uint32_t		GetStreamLengthMs() const =0;
	virtual uint32_t		GetStreamPlayTimeMs() const =0;
	virtual void			SetCursor(uint32_t nTime)=0;
	virtual uint32_t		GetSampleRate() const =0;

	virtual					~CAEStreamingDecoder();

	virtual uint32_t		GetStreamID() const =0;
};

static_assert(sizeof(CAEDataStream) == 0x28, "Wrong size: CAEDataStream");

#endif
