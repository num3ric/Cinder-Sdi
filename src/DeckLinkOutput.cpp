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

void DeckLinkOutput::setWindowSurface( const ci::Surface& surface )
{
	std::lock_guard<std::mutex> lock( mMutex );
	if( ! mWindowSurface || mWindowSurface->getSize() != surface.getSize() ) {
		mWindowSurface = ci::Surface8u::create( surface.getWidth(), surface.getHeight(), true, ci::SurfaceChannelOrder::BGRA );
	}

	mWindowSurface->copyFrom( surface, surface.getBounds() );
}

bool DeckLinkOutput::start()
{
	bool								bSuccess = false;
	IDeckLinkDisplayModeIterator*		pDLDisplayModeIterator;
	IDeckLinkDisplayMode*				pDLDisplayMode = NULL;

	if( mDeckLinkOutput->GetDisplayModeIterator( &pDLDisplayModeIterator ) == S_OK )
	{
		if( pDLDisplayModeIterator->Next( &pDLDisplayMode ) != S_OK )
		{
			MessageBox( NULL, _T( "Cannot find video mode." ), _T( "DeckLink error." ), MB_OK );
			goto bail;
		}
	}

	uiFrameWidth = pDLDisplayMode->GetWidth();
	uiFrameHeight = pDLDisplayMode->GetHeight();
	pDLDisplayMode->GetFrameRate( &frameDuration, &frameTimescale );

	uiFPS = ((frameTimescale + (frameDuration - 1)) / frameDuration);
	if( mDeckLinkOutput->EnableVideoOutput( pDLDisplayMode->GetDisplayMode(), bmdVideoOutputFlagDefault ) != S_OK )
	//if( mDeckLinkOutput->EnableVideoOutput( bmdModeHD1080p2398, bmdVideoOutputFlagDefault ) != S_OK )
		goto bail;

	uiTotalFrames = 0;

	setPreroll();

	mDeckLinkOutput->StartScheduledPlayback( 0, 100, 1.0 );

	bSuccess = true;
bail:
	if( pDLDisplayMode )
	{
		pDLDisplayMode->Release();
		pDLDisplayMode = NULL;
	}
	if( pDLDisplayModeIterator )
	{
		pDLDisplayModeIterator->Release();
		pDLDisplayModeIterator = NULL;
	}
	return bSuccess;
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
		if( mDeckLinkOutput->CreateVideoFrame( uiFrameWidth, uiFrameHeight, uiFrameWidth * 4, bmdFormat8BitBGRA, bmdFrameFlagDefault, &pDLVideoFrame ) != S_OK )
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

