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

#include "DeckLinkDeviceDiscovery.h"
#include "cinder/Surface.h"

#include <mutex>
#include <vector>
#include <atomic>

namespace media {

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

	class DeckLinkDevice;

	typedef std::shared_ptr<class DeckLinkInput> DeckLinkInputRef;
	class DeckLinkInput : public IDeckLinkInputCallback
	{
	public:
		DeckLinkInput( DeckLinkDevice * device );
		~DeckLinkInput();

		bool						start( BMDDisplayMode videoMode );
		void						stop();
		bool						isCapturing();

		std::vector<std::string>	getDisplayModeNames();

		Timecodes					getTimecode() const;
		bool						getTexture( ci::gl::Texture2dRef& texture, Timecodes * timecodes = nullptr );
		bool						getSurface( ci::SurfaceRef& surface, Timecodes * timecodes = nullptr );
	private:
		IDeckLinkInput *					mDecklinkInput;
		std::vector<IDeckLinkDisplayMode*>	mModesList;

		void						getAncillaryDataFromFrame( IDeckLinkVideoInputFrame* frame, BMDTimecodeFormat format, std::string& timecodeString, std::string& userBitsString );

		virtual HRESULT				VideoInputFormatChanged( BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags ) override;
		virtual HRESULT				VideoInputFrameArrived( IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audioPacket ) override;

		virtual HRESULT				QueryInterface( REFIID iid, LPVOID *ppv ) override;// { return E_NOINTERFACE; }
		virtual ULONG				AddRef() override;
		virtual ULONG				Release() override;


		mutable std::mutex					mMutex;
		std::atomic_bool					mCurrentlyCapturing;
		std::atomic_bool					mNewFrame;
		std::atomic_bool					mReadSurface;

		glm::ivec2							mSize;
		ci::SurfaceRef						mSurface;
		std::vector<uint16_t>				mBuffer;

		Timecodes							mTimecode;

		DeckLinkDevice *					mDevice;

		ULONG								m_refCount;
	};
}

