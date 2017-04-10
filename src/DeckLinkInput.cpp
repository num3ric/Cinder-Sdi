#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "DeckLinkDevice.h"

using namespace media;


DeckLinkInput::DeckLinkInput( DeckLinkDevice * device )
: mDevice{ device }
, mDecklinkInput( NULL )
, m_refCount{ 1 }
, mCurrentlyCapturing{ false }
, mUseYUVTexture{ false }
, mResolution{}
{

	IDeckLinkAttributes* deckLinkAttributes = NULL;
	IDeckLinkDisplayModeIterator* displayModeIterator = NULL;
	IDeckLinkDisplayMode* displayMode = NULL;
	bool result = false;

	while( mModesList.size() > 0 ) {
		mModesList.back()->Release();
		mModesList.pop_back();
	}

	// Get the IDeckLinkInput for the selected device
	if( mDevice->mDecklink->QueryInterface( IID_IDeckLinkInput, (void**)&mDecklinkInput ) != S_OK ) {
		mDecklinkInput = NULL;
		throw DecklinkExc{ "This application was unable to obtain IDeckLinkInput for the selected device." };
	}

	// Retrieve and cache mode list
	if( mDecklinkInput->GetDisplayModeIterator( &displayModeIterator ) == S_OK ) {
		while( displayModeIterator->Next( &displayMode ) == S_OK )
			mModesList.push_back( displayMode );

		displayModeIterator->Release();
	}
}

DeckLinkInput::~DeckLinkInput()
{
	stop();

	if( mDecklinkInput != NULL ) {
		mDecklinkInput->Release();
		mDecklinkInput = NULL;
	}
}

bool DeckLinkInput::start( BMDDisplayMode videoMode, bool useYUVTexture )
{
	if( mCurrentlyCapturing ) {
		CI_LOG_W( "Already capturing, aborting start." );
		return false;
	}

	BMDVideoInputFlags videoInputFlags = bmdVideoInputFlagDefault;
	if( mDevice->isFormatDetectionSupported() )
		videoInputFlags |= bmdVideoInputEnableFormatDetection;

	// Set the video input mode
	if( mDecklinkInput->EnableVideoInput( videoMode, bmdFormat8BitYUV, videoInputFlags ) != S_OK ) {
		CI_LOG_E( "This application was unable to select the chosen video mode. Perhaps, the selected device is currently in-use." );
		return false;
	}

	// Start the capture
	if( mDecklinkInput->StartStreams() != S_OK ) {
		CI_LOG_E( "This application was unable to start the capture. Perhaps, the selected device is currently in-use." );
		return false;
	}

	mResolution = mDevice->getDisplayModeResolution( videoMode );

	// Set capture callback
	mDecklinkInput->SetCallback( this );
	mCurrentlyCapturing = true;
	mUseYUVTexture = useYUVTexture;
	return true;
}

void DeckLinkInput::stop()
{
	if( ! mCurrentlyCapturing )
		return;
		
	if( mDecklinkInput != NULL ) {
		mDecklinkInput->StopStreams();
		mDecklinkInput->SetCallback( NULL );
	}

	mCurrentlyCapturing = false;
}

bool DeckLinkInput::isCapturing()
{
	return mCurrentlyCapturing;
}

HRESULT DeckLinkInput::VideoInputFormatChanged(/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags ) {

	unsigned int	modeIndex = 0;
	BMDPixelFormat	pixelFormat = bmdFormat8BitYUV;

	// Restart capture with the new video mode if told to
	if( mDevice->isFormatDetectionSupported() ) {
		if( detectedSignalFlags & bmdDetectedVideoInputRGB444 )
			pixelFormat = bmdFormat10BitRGB;

		// Stop the capture
		mDecklinkInput->StopStreams();

		// Set the video input mode
		if( mDecklinkInput->EnableVideoInput( newMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection ) != S_OK )
		{
			// Let the UI know we couldnt restart the capture with the detected input mode
			CI_LOG_E( "This application was unable to select the new video mode." );
			return S_OK;
		}

		// Start the capture
		if( mDecklinkInput->StartStreams() != S_OK )
		{
			// Let the UI know we couldnt restart the capture with the detected input mode
			CI_LOG_E( "This application was unable to start the capture on the selected device." );
			return S_OK;
		}
	}

	mResolution = mDevice->getDisplayModeResolution( newMode->GetDisplayMode() );

	return S_OK;
}

HRESULT DeckLinkInput::VideoInputFrameArrived( IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audioPacket )
{
	if( frame == NULL )
		return S_OK;

	std::lock_guard<std::mutex> lock( mFrameMutex );

	if( (frame->GetFlags() & bmdFrameHasNoInputSource) == 0 ) {

		if( mUseYUVTexture ) {
			FrameEvent frameEvent{ frame };
			mSignalFrame.emit( frameEvent );
		}
		else {
			FrameEvent frameEvent{ frame->GetWidth(), frame->GetHeight() };
			DeckLinkDeviceDiscovery::sVideoConverter->ConvertFrame( frame, &frameEvent.surfaceData );
			mSignalFrame.emit( frameEvent );
		}

		return S_OK;
	}
	return S_FALSE;
}

void DeckLinkInput::getAncillaryDataFromFrame( IDeckLinkVideoInputFrame* videoFrame, BMDTimecodeFormat timecodeFormat, std::string& timecodeString, std::string& userBitsString ) {
	IDeckLinkTimecode* timecode = NULL;
	BSTR timecodeCFString;
	BMDTimecodeUserBits userBits = 0;

	if( (videoFrame != NULL)
		&& (videoFrame->GetTimecode( timecodeFormat, &timecode ) == S_OK) ) {
		if( timecode->GetString( &timecodeCFString ) == S_OK ) {
			assert( timecodeCFString != NULL );
			timecodeString = ws2s( std::wstring( timecodeCFString, SysStringLen( timecodeCFString ) ) );
		}
		else {
			timecodeString = "";
		}

		timecode->GetTimecodeUserBits( &userBits );
		userBitsString = "0x" + userBits;
		timecode->Release();
	}
	else {
		timecodeString = "";
		userBitsString = "";
	}
}

HRESULT	STDMETHODCALLTYPE DeckLinkInput::QueryInterface( REFIID iid, LPVOID *ppv )
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

ULONG STDMETHODCALLTYPE DeckLinkInput::AddRef( void )
{
	return InterlockedIncrement( (LONG*)&m_refCount );
}

ULONG STDMETHODCALLTYPE DeckLinkInput::Release( void )
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

void VideoFrameBGRA::getSurface( ci::SurfaceRef & surface )
{
	if( surface == nullptr || surface->getSize() != GetSize() ) {
		surface = ci::Surface8u::create( GetWidth(), GetHeight(), true, ci::SurfaceChannelOrder::BGRA );
	}
	std::memcpy( surface->getData(), data(), GetRowBytes() * GetHeight() );
}
