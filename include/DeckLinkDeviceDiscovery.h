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

#include <afxwin.h>

#include "cinder/gl/gl.h"
#include "cinder/Noncopyable.h"

#include "DeckLinkAPI_h.h"

#include <vector>
#include <atomic>

namespace media {

	inline std::string ws2s( const std::wstring& wstr )
	{
		using convert_typeX = std::codecvt_utf8<wchar_t>;
		std::wstring_convert<convert_typeX, wchar_t> converterX;

		return converterX.to_bytes( wstr );
	}

	typedef std::shared_ptr<class DeckLinkDeviceDiscovery> DeckLinkDeviceDiscoveryRef;

	class DeckLinkDeviceDiscovery : public IDeckLinkDeviceNotificationCallback, public ci::Noncopyable
	{
	public:
		DeckLinkDeviceDiscovery( std::function<void( IDeckLink*, size_t )> deviceCallback );
		virtual ~DeckLinkDeviceDiscovery();

		std::string										getDeviceName( IDeckLink* device );
		ci::gl::GlslProgRef								getYUV2RGBShader() { return mGlslYUV2RGB; }

		// IDeckLinkDeviceNotificationCallback interface
		virtual HRESULT	STDMETHODCALLTYPE	DeckLinkDeviceArrived(/* in */ IDeckLink* deckLink );
		virtual HRESULT	STDMETHODCALLTYPE	DeckLinkDeviceRemoved(/* in */ IDeckLink* deckLink );

		// IUnknown needs only a dummy implementation
		virtual HRESULT	STDMETHODCALLTYPE	QueryInterface( REFIID iid, LPVOID *ppv );
		virtual ULONG	STDMETHODCALLTYPE	AddRef();
		virtual ULONG	STDMETHODCALLTYPE	Release();

		static IDeckLinkVideoConversion*	sVideoConverter;
	private:
		IDeckLinkDiscovery*					m_deckLinkDiscovery;
		ULONG								m_refCount;

		// The captured video is YCbCr 4:2:2 packed into a UYVY macropixel.  OpenGL has no YCbCr format
		// so treat it as RGBA 4:4:4:4 by halving the width and using GL_RGBA internal format.
		ci::gl::GlslProgRef			mGlslYUV2RGB;

		std::function<void( IDeckLink*, size_t )> mDeviceArrivedCallback;
	};

	class DecklinkExc : public ci::Exception {
		using ci::Exception::Exception;
	};

} //end namespace media

