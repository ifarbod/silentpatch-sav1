#include "StdAfxSA.h"
#include "AudioHardwareSA.h"

ExternalMethod<CAEDataStream, bool()> CAEDataStream::Initialise(AddressByVersion<bool (__thiscall*)(CAEDataStream*)>(0x4DC2B0));

DWORD CAEDataStream::Seek(LONG nToSeek, DWORD nPoint)
{
	LARGE_INTEGER filePosition;
	switch ( nPoint )
	{
	case FILE_BEGIN:
		filePosition.QuadPart = nToSeek + dwStartPosition;
		break;
	case FILE_END:
		nPoint = FILE_BEGIN;
		filePosition.QuadPart = dwStartPosition + dwLength - nToSeek;
		break;
	default:
		filePosition.QuadPart = nToSeek;
		break;
	}

	SetFilePointerEx(hHandle, filePosition, &filePosition, nPoint);
	dwCurrentPosition = filePosition.LowPart;

	return dwCurrentPosition - dwStartPosition;
}

DWORD CAEDataStream::FillBuffer(void* pBuf, DWORD nLen)
{
	ReadFile(hHandle, pBuf, nLen, &nLen, nullptr);
	dwCurrentPosition += nLen;
	return nLen;
}

CAEStreamingDecoder::~CAEStreamingDecoder()
{
	delete pStream;
}
