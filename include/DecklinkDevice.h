/*
* Copyright (c) 2016, The Mill
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or
* without modification, are permitted provided that the following
* conditions are met:
*
* Redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in
* the documentation and/or other materials provided with the
* distribution.
*
* Neither the name of the Eric Renaud-Houde nor the names of its
* contributors may be used to endorse or promote products
* derived from this software without specific prior written
* permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#pragma once

#include "DeckLinkAPI_h.h"

#include "cinder/gl/gl.h"
#include "cinder/Surface.h"
#include "cinder/Noncopyable.h"

#include <vector>
#include <atomic>

class DeckLinkManager : public ci::Noncopyable {
public:
	static DeckLinkManager& instance();
	static void cleanup();

	static IDeckLink*					getDevice( size_t index );
	static std::vector<IDeckLink*>		getDevices() { return instance().mDevices; }
	static size_t						getDeviceCount();
	static std::vector<std::string>		getDeviceNames();
	static ci::gl::GlslProgRef			getYUV2RGBShader() { return instance().mGlslYUV2RGB; }
private:
	DeckLinkManager();
	static IDeckLinkVideoConversion *	getConverter() { return instance().mVideoConverter; }

	std::vector<IDeckLink*>					mDevices;
	IDeckLinkVideoConversion *				mVideoConverter;
	// The captured video is YCbCr 4:2:2 packed into a UYVY macropixel.  OpenGL has no YCbCr format
	// so treat it as RGBA 4:4:4:4 by halving the width and using GL_RGBA internal format.
	ci::gl::GlslProgRef						mGlslYUV2RGB;

	static std::unique_ptr<DeckLinkManager>	mInstance;
	static std::once_flag					mOnceFlag;

	friend class DeckLinkDevice;
};

//typedef std::shared_ptr<class DeckLinkDevice> DeckLinkDeviceRef;

class DeckLinkDevice : private IDeckLinkInputCallback, public ci::Noncopyable {
public:
	typedef struct {
		// VITC timecodes and user bits for field 1 & 2
		std::string vitcF1Timecode;
		std::string vitcF1UserBits;
		std::string vitcF2Timecode;
		std::string vitcF2UserBits;

		// RP188 timecodes and user bits (VITC1, VITC2 and LTC)
		std::string rp188vitc1Timecode;
		std::string rp188vitc1UserBits;
		std::string rp188vitc2Timecode;
		std::string rp188vitc2UserBits;
		std::string rp188ltcTimecode;
		std::string rp188ltcUserBits;
	} Timecodes;

	DeckLinkDevice( IDeckLink * device );
	virtual ~DeckLinkDevice();

	std::vector<std::string>	getDisplayModeNames();
	glm::ivec2					getDisplayModeBufferSize( BMDDisplayMode mode );
	bool						isFormatDetectionEnabled();
	bool						isCapturing();

	bool						start( BMDDisplayMode videoMode );
	bool						start( int videoModeIndex );
	void						stop();
	void						cleanup();

	Timecodes					getTimecode() const;
	bool						getTexture( ci::gl::Texture2dRef& texture );
	bool						getSurface( ci::SurfaceRef& surface );
private:
	class VideoFrame : public IDeckLinkVideoFrame {
	public:
		VideoFrame( long width, long height )
			: mWidth{ width }, mHeight{ height }
		{
			mData.resize( height * width * 4 );
		}

		uint8_t * data() const { return (uint8_t*)mData.data(); }

		//override these methods for virtual
		virtual long			GetWidth( void ) { return mWidth; }
		virtual long			GetHeight( void ) { return mHeight; }
		virtual long			GetRowBytes( void ) { return mWidth * 4; }
		virtual BMDPixelFormat	GetPixelFormat( void ) { return bmdFormat8BitARGB; }
		virtual BMDFrameFlags	GetFlags( void ) { return 0; }
		virtual HRESULT			GetBytes( void **buffer )
		{
			*buffer = (void*)mData.data();
			return S_OK;
		}

		virtual HRESULT			GetTimecode( BMDTimecodeFormat format, IDeckLinkTimecode **timecode ) { return E_NOINTERFACE; };
		virtual HRESULT			GetAncillaryData( IDeckLinkVideoFrameAncillary **ancillary ) { return E_NOINTERFACE; };
		virtual HRESULT			QueryInterface( REFIID iid, LPVOID *ppv ) { return E_NOINTERFACE; }
		virtual ULONG			AddRef() { return 1; }
		virtual ULONG			Release() { return 1; }
	private:
		long mWidth, mHeight;
		std::vector<uint8_t> mData;
	};

	void						getAncillaryDataFromFrame( IDeckLinkVideoInputFrame* frame, BMDTimecodeFormat format, std::string& timecodeString, std::string& userBitsString );

	virtual HRESULT				VideoInputFormatChanged( BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags ) override;
	virtual HRESULT				VideoInputFrameArrived( IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audioPacket ) override;

	virtual HRESULT				QueryInterface( REFIID iid, LPVOID *ppv ) override { return E_NOINTERFACE; }
	virtual ULONG				AddRef() override { return 1; }
	virtual ULONG				Release() override { return 1; }

	IDeckLink *							mDecklink;
	IDeckLinkInput *					mDecklinkInput;
	std::vector<IDeckLinkDisplayMode*>	mModesList;

	mutable std::mutex					mMutex;
	std::atomic_bool					mNewFrame;
	std::atomic_bool					mReadSurface;

	std::atomic_bool					mCurrentlyCapturing;
	bool								mSupportsFormatDetection;
	
	glm::ivec2							mSize;
	ci::SurfaceRef						mSurface;
	std::vector<uint16_t>				mBuffer;

	Timecodes							mTimecode;
};


class DecklinkExc : public ci::Exception {
	using ci::Exception::Exception;
};
