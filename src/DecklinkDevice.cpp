
#include "stdafx.h"
#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "DecklinkDevice.h"

std::string ws2s( const std::wstring& wstr )
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes( wstr );
}

std::unique_ptr<DeckLinkManager> DeckLinkManager::mInstance = nullptr;
std::once_flag DeckLinkManager::mOnceFlag;

DeckLinkManager& DeckLinkManager::instance()
{
	std::call_once( mOnceFlag,
		[] {
		mInstance.reset( new DeckLinkManager{} );
	} );

	return *mInstance;
}

DeckLinkManager::DeckLinkManager()
	: mVideoConverter{ nullptr }
{
	IDeckLinkIterator* deckLinkIterator = nullptr;
	IDeckLink* deckLink = nullptr;

	if( CoCreateInstance( CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator ) != S_OK ) {
		deckLinkIterator = nullptr;
		throw DecklinkExc{ "Please install the Blackmagic Desktop Video drivers to use the features of this application." };
	}

	if( CoCreateInstance( CLSID_CDeckLinkVideoConversion, NULL, CLSCTX_ALL, IID_IDeckLinkVideoConversion, (void**)&mVideoConverter ) != S_OK ) {
		throw DecklinkExc{ "Failed to create the decklink video converter." };
		mVideoConverter = nullptr;
	}

	while( deckLinkIterator->Next( &deckLink ) == S_OK ) {
		deckLink->AddRef();
		mDevices.push_back( deckLink );
	}

	deckLinkIterator->Release();

	try {
		auto vert = "#version 410 \n"
			"in vec4 ciPosition; \n"
			"in vec2 ciTexCoord0; \n"
			"uniform mat4 ciModelViewProjection; \n"
			"out vec2 vTexCoord0; \n"
			"void main() { \n"
			"	gl_Position = ciModelViewProjection * ciPosition; \n"
			"	vTexCoord0 = ciTexCoord0; \n"
			"} \n";
		auto frag = "#version 410 \n"
			"uniform sampler2D UYVYtex; \n"
			"in vec2	vTexCoord0; \n"
			"out vec4 fragColor; \n"
			"void main( void ) { \n"
			"	float tx, ty, Y, Cb, Cr, r, g, b; \n"
			"	tx = vTexCoord0.x; \n"
			"	ty = 1.0 - vTexCoord0.y; \n"
			"	if( tx > 0.5 ) \n"
			"		Y = texture( UYVYtex, vec2( tx, ty ) ).a; \n"
			"	else \n"
			"		Y = texture( UYVYtex, vec2( tx, ty ) ).g; \n"
			"	Cb = texture( UYVYtex, vec2( tx, ty ) ).b; \n"
			"	Cr = texture( UYVYtex, vec2( tx, ty ) ).r; \n"
			"	Y = (Y * 256.0 - 16.0) / 219.0; \n"
			"	Cb = (Cb * 256.0 - 16.0) / 224.0 - 0.5; \n"
			"	Cr = (Cr * 256.0 - 16.0) / 224.0 - 0.5; \n"
			"	r = Y + 1.5748 * Cr; \n"
			"	g = Y - 0.1873 * Cb - 0.4681 * Cr; \n"
			"	b = Y + 1.8556 * Cb; \n"
			"	fragColor = vec4( r, g, b, 0.7 ); \n"
			"} \n";
		mGlslYUV2RGB = ci::gl::GlslProg::create( vert, frag );
	}
	catch( ci::gl::GlslProgCompileExc & exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}

	ci::app::getWindow()->getSignalClose().connect( [this]() {
		for( auto& device : instance().mDevices ) {
			device->Release();
		}
	} );
}

IDeckLink* DeckLinkManager::getDevice( size_t index )
{
	return instance().mDevices.at( index );
}

size_t DeckLinkManager::getDeviceCount()
{
	return instance().mDevices.size();
}

std::vector<std::string> DeckLinkManager::getDeviceNames()
{
	std::vector<std::string> names;
	for( auto& device : instance().mDevices ) {
		BSTR cfStrName;
		// Get the name of this device
		if( device->GetDisplayName( &cfStrName ) == S_OK ) {
			assert( cfStrName != nullptr );
			std::wstring ws( cfStrName, SysStringLen( cfStrName ) );
			names.push_back( ws2s( ws ) );
		}
		else {
			names.push_back( "DeckLink" );
		}
	}
	return names;
}

DeckLinkDevice::DeckLinkDevice( IDeckLink * device )
: mDecklink( device )
, mDecklinkInput( nullptr )
, mSupportsFormatDetection( 0 )
, mCurrentlyCapturing( false )
, mSize()
, mNewFrame{ false }
, mReadSurface{ false }
, mSurface{ nullptr }
, mLock{ mMutex, std::defer_lock }
{
	mDecklink->AddRef();

	IDeckLinkAttributes* deckLinkAttributes = nullptr;
	IDeckLinkDisplayModeIterator* displayModeIterator = nullptr;
	IDeckLinkDisplayMode* displayMode = nullptr;
	bool result = false;

	// A new device has been selected.
	// Release the previous selected device and mode list
	if( mDecklinkInput != nullptr ) {
		mDecklinkInput->Release();
	}

	while( mModesList.size() > 0 ) {
		mModesList.back()->Release();
		mModesList.pop_back();
	}


	// Get the IDeckLinkInput for the selected device
	if( mDecklink->QueryInterface( IID_IDeckLinkInput, (void**)&mDecklinkInput ) != S_OK ) {
		mDecklinkInput = nullptr;
		throw DecklinkExc{ "This application was unable to obtain IDeckLinkInput for the selected device." };
	}

	// Retrieve and cache mode list
	if( mDecklinkInput->GetDisplayModeIterator( &displayModeIterator ) == S_OK ) {
		while( displayModeIterator->Next( &displayMode ) == S_OK )
			mModesList.push_back( displayMode );

		displayModeIterator->Release();
	}

	//
	// Check if input mode detection format is supported.

	mSupportsFormatDetection = false; // assume unsupported until told otherwise
	if( device->QueryInterface( IID_IDeckLinkAttributes, (void**)&deckLinkAttributes ) == S_OK ) {
		BOOL support = 0;
		if( deckLinkAttributes->GetFlag( BMDDeckLinkSupportsInputFormatDetection, &support ) == S_OK )
			mSupportsFormatDetection = support;

		deckLinkAttributes->Release();
	}

	ci::app::getWindow()->getSignalClose().connect( [this]() {
		stop();
		cleanup();
	} );
}

DeckLinkDevice::~DeckLinkDevice()
{
	cleanup();
}

void DeckLinkDevice::cleanup()
{
	if( mDecklinkInput != nullptr ) {
		mDecklinkInput->Release();
		mDecklinkInput = nullptr;
	}

	if( mDecklink != nullptr ) {
		mDecklink->Release();
		mDecklink = nullptr;
	}
}

std::vector<std::string> DeckLinkDevice::getDisplayModeNames() {
	std::vector<std::string> modeNames;
	int modeIndex;
	BSTR modeName;

	for( modeIndex = 0; modeIndex < mModesList.size(); modeIndex++ ) {
		if( mModesList[modeIndex]->GetName( &modeName ) == S_OK ) {
			assert( modeName != nullptr );
			std::wstring ws( modeName, SysStringLen( modeName ) );
			modeNames.push_back( ws2s( ws ) );
		}
		else {
			modeNames.push_back( "Unknown mode" );
		}
	}

	return modeNames;
}

bool DeckLinkDevice::isFormatDetectionEnabled()
{
	return mSupportsFormatDetection;
}

bool DeckLinkDevice::isCapturing()
{
	return mCurrentlyCapturing;
}

glm::ivec2 DeckLinkDevice::getDisplayModeBufferSize( BMDDisplayMode mode ) {

	if( mode == bmdModeNTSC2398
		|| mode == bmdModeNTSC
		|| mode == bmdModeNTSCp ) {
		return glm::ivec2( 720, 486 );
	}
	else if( mode == bmdModePAL
		|| mode == bmdModePALp ) {
		return glm::ivec2( 720, 576 );
	}
	else if( mode == bmdModeHD720p50
		|| mode == bmdModeHD720p5994
		|| mode == bmdModeHD720p60 ) {
		return glm::ivec2( 1280, 720 );
	}
	else if( mode == bmdModeHD1080p2398
		|| mode == bmdModeHD1080p24
		|| mode == bmdModeHD1080p25
		|| mode == bmdModeHD1080p2997
		|| mode == bmdModeHD1080p30
		|| mode == bmdModeHD1080i50
		|| mode == bmdModeHD1080i5994
		|| mode == bmdModeHD1080i6000
		|| mode == bmdModeHD1080p50
		|| mode == bmdModeHD1080p5994
		|| mode == bmdModeHD1080p6000 ) {
		return glm::ivec2( 1920, 1080 );
	}
	else if( mode == bmdMode2k2398
		|| mode == bmdMode2k24
		|| mode == bmdMode2k25 ) {
		return glm::ivec2( 2048, 1556 );
	}
	else if( mode == bmdMode2kDCI2398
		|| mode == bmdMode2kDCI24
		|| mode == bmdMode2kDCI25 ) {
		return glm::ivec2( 2048, 1080 );
	}
	else if( mode == bmdMode4K2160p2398
		|| mode == bmdMode4K2160p24
		|| mode == bmdMode4K2160p25
		|| mode == bmdMode4K2160p2997
		|| mode == bmdMode4K2160p30 ) {
		return glm::ivec2( 3840, 2160 );
	}
	else if( mode == bmdMode4kDCI2398
		|| mode == bmdMode4kDCI24
|| mode == bmdMode4kDCI25 ) {
return glm::ivec2( 4096, 2160 );
	}

	return glm::ivec2();
}

bool DeckLinkDevice::start( int videoModeIndex ) {
	// Get the IDeckLinkDisplayMode from the given index
	if( (videoModeIndex < 0) || (videoModeIndex >= mModesList.size()) ) {
		CI_LOG_E( "An invalid display mode was selected." );
		return false;
	}

	return start( mModesList[videoModeIndex]->GetDisplayMode() );
}

bool DeckLinkDevice::start( BMDDisplayMode videoMode )
{
	BMDVideoInputFlags videoInputFlags = bmdVideoInputFlagDefault;
	if( mSupportsFormatDetection )
		videoInputFlags |= bmdVideoInputEnableFormatDetection;

	// Set capture callback
	mDecklinkInput->SetCallback( this );

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

	mCurrentlyCapturing = true;
	return true;
}

void DeckLinkDevice::stop()
{
	if( ! mCurrentlyCapturing )
		return;

	if( mDecklinkInput != nullptr ) {
		mDecklinkInput->StopStreams();
		mDecklinkInput->SetScreenPreviewCallback( NULL );
		mDecklinkInput->SetCallback( NULL );
	}

	mCurrentlyCapturing = false;
}

HRESULT DeckLinkDevice::VideoInputFormatChanged(/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags ) {

	// Stop the capture
	mDecklinkInput->StopStreams();

	// Set the video input mode
	if( mDecklinkInput->EnableVideoInput( newMode->GetDisplayMode(), bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection ) != S_OK ) {
		CI_LOG_E( "This application was unable to select the new video mode." );
		return S_FALSE;
	}

	// Start the capture
	if( mDecklinkInput->StartStreams() != S_OK ) {
		CI_LOG_E( "This application was unable to start the capture on the selected device." );
		return S_FALSE;
	}

	return S_OK;
}

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
} AncillaryDataStruct;

HRESULT DeckLinkDevice::VideoInputFrameArrived( IDeckLinkVideoInputFrame* frame, IDeckLinkAudioInputPacket* audioPacket )
{
	if( ( frame->GetFlags() & bmdFrameHasNoInputSource ) == 0 ) {
		// Get the various timecodes and userbits for this frame
		AncillaryDataStruct ancillaryData;
		getAncillaryDataFromFrame( frame, bmdTimecodeVITC, ancillaryData.vitcF1Timecode, ancillaryData.vitcF1UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeVITCField2, ancillaryData.vitcF2Timecode, ancillaryData.vitcF2UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188VITC1, ancillaryData.rp188vitc1Timecode, ancillaryData.rp188vitc1UserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188LTC, ancillaryData.rp188ltcTimecode, ancillaryData.rp188ltcUserBits );
		getAncillaryDataFromFrame( frame, bmdTimecodeRP188VITC2, ancillaryData.rp188vitc2Timecode, ancillaryData.rp188vitc2UserBits );

		auto x = static_cast<int32_t>(frame->GetWidth());
		auto y = static_cast<int32_t>(frame->GetHeight());

		std::lock_guard<std::mutex> lock( mMutex );

		mSize = glm::ivec2( x, y );
		mBuffer.resize( frame->GetRowBytes() * frame->GetHeight() );
		void * data;
		frame->GetBytes( &data );
		std::memcpy( mBuffer.data(), data, mBuffer.size() );

		if( mReadSurface ) {
			VideoFrame videoFrame{ frame->GetWidth(), frame->GetHeight() };
			DeckLinkManager::getConverter()->ConvertFrame( frame, &videoFrame );
			if( mSurface == nullptr || mSurface->getWidth() != frame->GetWidth() || mSurface->getHeight() != frame->GetHeight() ) {
				mSurface = ci::Surface8u::create( frame->GetWidth(), frame->GetHeight(), true, ci::SurfaceChannelOrder::ARGB );
			}
			std::memcpy( mSurface->getData(), videoFrame.data(), videoFrame.GetRowBytes() * videoFrame.GetHeight() );
		}

		mNewFrame = true;
		return S_OK;
	}
	return S_FALSE;
}

void DeckLinkDevice::getAncillaryDataFromFrame( IDeckLinkVideoInputFrame* videoFrame, BMDTimecodeFormat timecodeFormat, std::string& timecodeString, std::string& userBitsString ) {
	IDeckLinkTimecode* timecode = nullptr;
	BSTR timecodeCFString;
	BMDTimecodeUserBits userBits = 0;

	if( (videoFrame != nullptr)
		&& (videoFrame->GetTimecode( timecodeFormat, &timecode ) == S_OK) ) {
		if( timecode->GetString( &timecodeCFString ) == S_OK ) {
			assert( timecodeCFString != nullptr );
			timecodeString = ws2s( std::wstring( timecodeCFString, SysStringLen( timecodeCFString ) ) );
		}
		else {
			timecodeString = "";
		}

		timecode->GetTimecodeUserBits( &userBits );
		//userBitsString = "0x" +  userBits ;
		timecode->Release();
	}
	else {
		timecodeString = "";
		userBitsString = "";
	}
}

bool DeckLinkDevice::getTexture( ci::gl::Texture2dRef& texture )
{
	if( ! mNewFrame )
		return false;

	mNewFrame = false;

	std::lock_guard<std::mutex> lock( mMutex );
	texture = ci::gl::Texture2d::create( mBuffer.data(), GL_BGRA, mSize.x/2, mSize.y, ci::gl::Texture::Format().internalFormat( GL_RGBA ).dataType( GL_UNSIGNED_INT_8_8_8_8_REV ) );
	return true;
}

bool DeckLinkDevice::acquireSurface( ci::SurfaceRef& surface )
{
	mReadSurface = true;
	if( ! mNewFrame )
		return false;

	mNewFrame = false;
	mLock.lock();
	surface = mSurface;
	return true;
}

void DeckLinkDevice::releaseSurface()
{
	if( mLock.owns_lock() ) {
		mLock.unlock();
	}
}
