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

#include <vector>
#include <atomic>

namespace media {

	class DeckLinkDevice;

	typedef std::shared_ptr<class DeckLinkOutput> DeckLinkOutputRef;
	class DeckLinkOutput : public IDeckLinkVideoOutputCallback
	{
	public:
		DeckLinkOutput( DeckLinkDevice * device );
		~DeckLinkOutput();

		void sendSurface( const ci::Surface& surface );
		void sendTexture( const ci::gl::Texture2dRef& texture );
		void sendWindowSurface();
		bool start( BMDDisplayMode videoMode );
		void stop();

	private:
		void setPreroll();

		// IDeckLinkVideoOutputCallback
		virtual HRESULT	STDMETHODCALLTYPE	ScheduledFrameCompleted( IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result ) override;
		virtual HRESULT	STDMETHODCALLTYPE	ScheduledPlaybackHasStopped() override;

		virtual HRESULT				QueryInterface( REFIID iid, LPVOID *ppv ) override;// { return E_NOINTERFACE; }
		virtual ULONG				AddRef() override;
		virtual ULONG				Release() override;

		DeckLinkDevice *	mDevice;

		IDeckLinkOutput*			mDeckLinkOutput;
		glm::ivec2					mResolution;
		BMDTimeValue				frameDuration;
		BMDTimeScale				frameTimescale;
		unsigned __int32			uiFPS;
		unsigned __int32			uiTotalFrames;

		ci::SurfaceRef				mWindowSurface;

		mutable std::mutex					mMutex;

		ULONG				m_refCount;
	};
}

