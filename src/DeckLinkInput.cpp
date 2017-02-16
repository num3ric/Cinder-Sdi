#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "DeckLinkDevice.h"

using namespace media;

class VideoFrameBGRA : public IDeckLinkVideoFrame {
public:
	VideoFrameBGRA( long width, long height )
		: mWidth{ width }, mHeight{ height }
	{
		mData.resize( height * width * 4 );
	}

	uint8_t * data() const { return (uint8_t*)mData.data(); }

	//override these methods for virtual
	virtual long			GetWidth( void ) { return mWidth; }
	virtual long			GetHeight( void ) { return mHeight; }
	virtual long			GetRowBytes( void ) { return mWidth * 4; }
	virtual BMDPixelFormat	GetPixelFormat( void ) { return bmdFormat8BitBGRA; }
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


DeckLinkInput::DeckLinkInput( DeckLinkDevice * device )
: mDevice{ device }
, mDecklinkInput( NULL )
, mSize()
, mNewFrame{ false }
, mReadSurface{ false }
, mSurface{ nullptr }
, m_refCount{ 1 }
, mCurrentlyCapturing{ false }
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
	if( mDecklinkInput != NULL ) {
		mDecklinkInput->Release();
		mDecklinkInput = NULL;
	}
}

bool DeckLinkInput::start( BMDDisplayMode videoMode )
{
	if( mCurrentlyCapturing )
		return false;

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

	// Set capture callback
	mDecklinkInput->SetCallback( this );

	mCurrentlyCapturing = true;
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
	BMDPixelFormat	pixelFormat = bmdFormat10BitYUV;

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
	return S_OK;
}

HRESULT DeckLinkInput::VideoInputFrameArrived( IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audioPacket )
{
	if( frame == NULL )
		return S_OK;

	if( (frame->GetFlags() & bmdFrameHasNoInputSource) == 0 ) {

		std::lock_guard<std::mutex> lock( mMutex );

		// Get the various timecodes and userbits for this frame
		getAncillaryDataFromFrame( frame, bmdTimecodeVITC, mTimecode.vitcF1Timecode, mTimecode.vitcF1UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeVITCField2, mTimecode.vitcF2Timecode, mTimecode.vitcF2UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188VITC1, mTimecode.rp188vitc1Timecode, mTimecode.rp188vitc1UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188LTC, mTimecode.rp188ltcTimecode, mTimecode.rp188ltcUserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188VITC2, mTimecode.rp188vitc2Timecode, mTimecode.rp188vitc2UserBits );

		mBuffer.resize( frame->GetRowBytes() * frame->GetHeight() );
		void * data;
		frame->GetBytes( &data );
		std::memcpy( mBuffer.data(), data, mBuffer.size() );

		if( mReadSurface ) {
			// // YUV 4:2:2 channel
			//ci::Channel16u yuvChannel{ frame->GetWidth() / 2, frame->GetHeight(), static_cast<ptrdiff_t>( frame->GetRowBytes() ), 2, static_cast<uint16_t*>( data ) };
			//std::memcpy( yuvChannel.getData(), data, frame->GetRowBytes() * frame->GetHeight() );
			//mSurface = ci::Surface8u::create( yuvChannel );

			VideoFrameBGRA videoFrame{ frame->GetWidth(), frame->GetHeight() };
			DeckLinkDeviceDiscovery::sVideoConverter->ConvertFrame( frame, &videoFrame );
			if( mSurface == nullptr ) {
				mSurface = ci::Surface8u::create( frame->GetWidth(), frame->GetHeight(), true, ci::SurfaceChannelOrder::BGRA );
			}
			std::memcpy( mSurface->getData(), videoFrame.data(), videoFrame.GetRowBytes() * videoFrame.GetHeight() );
		}

		mNewFrame = true;
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

Timecodes DeckLinkInput::getTimecode() const
{
	std::lock_guard<std::mutex> lock( mMutex );
	return mTimecode;
}

bool DeckLinkInput::getTexture( ci::gl::Texture2dRef& texture, Timecodes * timecodes )
{
	if( !mNewFrame )
		return false;

	mNewFrame = false;

	std::lock_guard<std::mutex> lock( mMutex );
	texture = ci::gl::Texture2d::create( mBuffer.data(), GL_BGRA, mSize.x / 2, mSize.y, ci::gl::Texture::Format().internalFormat( GL_RGBA ).dataType( GL_UNSIGNED_INT_8_8_8_8_REV ) );

	if( timecodes )
		*timecodes = mTimecode;

	return true;
}

bool DeckLinkInput::getSurface( ci::SurfaceRef& surface, Timecodes * timecodes )
{
	mReadSurface = true;
	if( !mNewFrame || !mSurface )
		return false;

	mNewFrame = false;

	std::lock_guard<std::mutex> lock( mMutex );
	if( surface && surface->getSize() == mSurface->getSize() ) {
		surface->copyFrom( *mSurface, mSurface->getBounds() );
	}
	else {
		surface = ci::Surface8u::create( *mSurface );
	}

	if( timecodes )
		*timecodes = mTimecode;

	return true;
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

