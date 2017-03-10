#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "cinder/ip/Resize.h"

#include "DeckLinkOutput.h"
#include "DeckLinkDevice.h"

using namespace media;

DeckLinkOutput::DeckLinkOutput( DeckLinkDevice * device )
	: mDevice{ device }
	, m_refCount{ 1 }
{
	if( mDevice->mDecklink->QueryInterface( IID_IDeckLinkOutput, (void**)&mDeckLinkOutput ) != S_OK ) {
		mDeckLinkOutput = NULL;
		throw DecklinkExc{ "This application was unable to obtain IDeckLinkOutput for the selected device." };
	}

	if( mDeckLinkOutput->SetScheduledFrameCompletionCallback( this ) != S_OK )
		throw DecklinkExc{ "Callback output error" };
}

DeckLinkOutput::~DeckLinkOutput()
{
	if( mDeckLinkOutput != NULL )
	{
		mDeckLinkOutput->Release();
		mDeckLinkOutput = NULL;
	}
}

void DeckLinkOutput::sendSurface( const ci::Surface & surface )
{
	std::lock_guard<std::mutex> lock( mMutex );
	if( ! mWindowSurface || mWindowSurface->getSize() != mResolution ) {
		mWindowSurface = ci::Surface8u::create( mResolution.x, mResolution.y, true, ci::SurfaceChannelOrder::BGRA );
	}

	if( surface.getSize() == mWindowSurface->getSize() && surface.getChannelOrder() == mWindowSurface->getChannelOrder() ) {
		mWindowSurface->copyFrom( surface, surface.getBounds() );
	}
	else {
		CI_LOG_E( "Incompatible surface." );
	}
}

void DeckLinkOutput::sendTexture( const ci::gl::Texture2dRef & texture )
{
	std::lock_guard<std::mutex> lock( mMutex );
	if( ! mWindowSurface || mWindowSurface->getSize() != mResolution ) {
		mWindowSurface = ci::Surface8u::create( mResolution.x, mResolution.y, true, ci::SurfaceChannelOrder::BGRA );
	}

	ci::gl::ScopedTextureBind tex0{ texture };
	glGetTexImage( GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mWindowSurface->getData() );
}

void DeckLinkOutput::sendWindowSurface()
{
	std::lock_guard<std::mutex> lock( mMutex );
	if( ! mWindowSurface || mWindowSurface->getSize() != mResolution ) {
		mWindowSurface = ci::Surface8u::create( mResolution.x, mResolution.y, true, ci::SurfaceChannelOrder::BGRA );
	}
	GLint oldPackAlignment;
	glFlush();
	glGetIntegerv( GL_PACK_ALIGNMENT, &oldPackAlignment );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, mResolution.x, mResolution.y, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mWindowSurface->getData() );
	glPixelStorei( GL_PACK_ALIGNMENT, oldPackAlignment );
}

bool DeckLinkOutput::start( BMDDisplayMode videoMode )
{
	bool								success = false;
	IDeckLinkDisplayModeIterator*		displayModeIterator;
	IDeckLinkDisplayMode*				displayMode = NULL;
	uiTotalFrames = 0;

	bool found = false;
	if( mDeckLinkOutput->GetDisplayModeIterator( &displayModeIterator ) == S_OK ) {
		while( displayModeIterator->Next( &displayMode ) == S_OK ) {
			if( displayMode->GetDisplayMode() == videoMode ) {
				found = true;
				break;
			}
		}
	}

	if( found ) {
		mResolution.x = displayMode->GetWidth();
		mResolution.y = displayMode->GetHeight();
		displayMode->GetFrameRate( &frameDuration, &frameTimescale );
		uiFPS = ( ( frameTimescale + ( frameDuration - 1 ) ) / frameDuration );
		if( mDeckLinkOutput->EnableVideoOutput( videoMode, bmdVideoOutputFlagDefault ) == S_OK ) {
			setPreroll();
			mDeckLinkOutput->StartScheduledPlayback( 0, 100, 1.0 );
			success = true;
		}
		else {
			CI_LOG_E( "Failed to enable video output." );
		}
	}
	else {
		CI_LOG_E( "Cannot find video mode." );
	}

	if( displayMode ) {
		displayMode->Release();
		displayMode = NULL;
	}
	if( displayModeIterator ) {
		displayModeIterator->Release();
		displayModeIterator = NULL;
	}
	return success;
}

void DeckLinkOutput::stop()
{
	mDeckLinkOutput->StopScheduledPlayback( 0, NULL, 0 );
	mDeckLinkOutput->DisableVideoOutput();
}

void DeckLinkOutput::setPreroll()
{
	IDeckLinkMutableVideoFrame* pDLVideoFrame;

	// Set 3 frame preroll
	for( unsigned i = 0; i < 3; i++ )
	{
		// Flip frame vertical, because OpenGL rendering starts from left bottom corner
		if( mDeckLinkOutput->CreateVideoFrame( mResolution.x, mResolution.y, mResolution.x * 4, bmdFormat8BitBGRA, bmdFrameFlagFlipVertical, &pDLVideoFrame ) != S_OK )
			goto bail;

		if( mDeckLinkOutput->ScheduleVideoFrame( pDLVideoFrame, (uiTotalFrames * frameDuration), frameDuration, frameTimescale ) != S_OK )
			goto bail;

		/* The local reference to the IDeckLinkVideoFrame is released here, as the ownership has now been passed to
		*  the DeckLinkAPI via ScheduleVideoFrame.
		*
		* After the API has finished with the frame, it is returned to the application via ScheduledFrameCompleted.
		* In ScheduledFrameCompleted, this application updates the video frame and passes it to ScheduleVideoFrame,
		* returning ownership to the DeckLink API.
		*/
		pDLVideoFrame->Release();
		pDLVideoFrame = NULL;

		uiTotalFrames++;
	}
	return;

bail:
	if( pDLVideoFrame )
	{
		pDLVideoFrame->Release();
		pDLVideoFrame = NULL;
	}
}

HRESULT DeckLinkOutput::ScheduledFrameCompleted( IDeckLinkVideoFrame * completedFrame, BMDOutputFrameCompletionResult result )
{
	std::lock_guard<std::mutex> lock( mMutex );

	if( ! mWindowSurface )
		return S_OK;

	void * data = NULL;
	completedFrame->GetBytes( (void**)&data );
	std::memcpy( data, mWindowSurface->getData(), mWindowSurface->getRowBytes() * mWindowSurface->getHeight() );

	if( mDeckLinkOutput->ScheduleVideoFrame( completedFrame, (uiTotalFrames * frameDuration), frameDuration, frameTimescale ) == S_OK )
	{
		uiTotalFrames++;
	}
	return S_OK;
}

HRESULT DeckLinkOutput::ScheduledPlaybackHasStopped()
{
	return S_OK;
}

HRESULT	STDMETHODCALLTYPE DeckLinkOutput::QueryInterface( REFIID iid, LPVOID *ppv )
{
	HRESULT			result = E_NOINTERFACE;

	if( ppv == NULL )
		return E_INVALIDARG;

	// Initialise the return result
	*ppv = NULL;

	// Obtain the IUnknown interface and compare it the provided REFIID
	if( iid == IID_IUnknown )
	{
		*ppv = this;
		AddRef();
		result = S_OK;
	}
	else if( iid == IID_IDeckLinkInputCallback )
	{
		*ppv = (IDeckLinkInputCallback*)this;
		AddRef();
		result = S_OK;
	}
	else if( iid == IID_IDeckLinkNotificationCallback )
	{
		*ppv = (IDeckLinkNotificationCallback*)this;
		AddRef();
		result = S_OK;
	}

	return result;
}

ULONG STDMETHODCALLTYPE DeckLinkOutput::AddRef( void )
{
	return InterlockedIncrement( (LONG*)&m_refCount );
}

ULONG STDMETHODCALLTYPE DeckLinkOutput::Release( void )
{
	int		newRefValue;

	newRefValue = InterlockedDecrement( (LONG*)&m_refCount );
	if( newRefValue == 0 )
	{
		delete this;
		return 0;
	}

	return newRefValue;
}

