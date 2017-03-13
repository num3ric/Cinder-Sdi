#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "DeckLinkInput.h"
#include "DeckLinkDevice.h"

using namespace media;

DeckLinkDevice::DeckLinkDevice( IDeckLink * decklink )
: mDecklink( decklink )
, mSupportsFormatDetection( 0 )
, mInput{ nullptr }
, mOutput{ nullptr }
{
	mDecklink->AddRef();

	//
	// Check if input mode detection format is supported.
	IDeckLinkAttributes* deckLinkAttributes = NULL;
	mSupportsFormatDetection = false; // assume unsupported until told otherwise
	if( mDecklink->QueryInterface( IID_IDeckLinkAttributes, (void**)&deckLinkAttributes ) == S_OK ) {
		BOOL support = 0;
		if( deckLinkAttributes->GetFlag( BMDDeckLinkSupportsInputFormatDetection, &support ) == S_OK )
			mSupportsFormatDetection = support;

		//LONGLONG index;
		//if( deckLinkAttributes->GetInt( BMDDeckLinkSubDeviceIndex, &index ) == S_OK ) {
		//	CI_LOG_I( index );
		//}
		deckLinkAttributes->Release();
	}

	mInput = std::make_shared<DeckLinkInput>( this );
	mOutput = std::make_shared<DeckLinkOutput>( this );
}

DeckLinkDevice::~DeckLinkDevice()
{
	mInput->stop();
	mOutput->stop();

	if( mDecklink != NULL ) {
		mDecklink->Release();
		mDecklink = NULL;
	}
}

std::vector<std::string> DeckLinkInput::getDisplayModeNames()
{
	std::vector<std::string> modeNames;
	int modeIndex;
	BSTR modeName;

	for( modeIndex = 0; modeIndex < mModesList.size(); modeIndex++ ) {
		if( mModesList[modeIndex]->GetName( &modeName ) == S_OK ) {
			assert( modeName != NULL );
			std::wstring ws( modeName, SysStringLen( modeName ) );
			modeNames.push_back( ws2s( ws ) );
		}
		else {
			modeNames.push_back( "Unknown mode" );
		}
	}

	return modeNames;
}

bool DeckLinkDevice::isFormatDetectionSupported()
{
	return mSupportsFormatDetection;
}

inline std::string getDisplayModeString( BMDDisplayMode mode )
{
	switch( mode ) {
	case bmdModeNTSC:			return "Mode NTSC";
	case bmdModeNTSC2398:		return "Mode NTSC2398";
	case bmdModePAL:			return "Mode PAL";
	case bmdModeNTSCp:			return "Mode NTSCp";
	case bmdModePALp:			return "Mode PALp";
	case bmdModeHD1080p2398:	return "Mode HD1080p2398";
	case bmdModeHD1080p24:		return "Mode HD1080p24";
	case bmdModeHD1080p25:		return "Mode HD1080p25";
	case bmdModeHD1080p2997:	return "Mode HD1080p2997";
	case bmdModeHD1080p30:		return "Mode HD1080p30";
	case bmdModeHD1080i50:		return "Mode HD1080i50";
	case bmdModeHD1080i5994:	return "Mode HD1080i5994";
	case bmdModeHD1080i6000:	return "Mode HD1080i6000";
	case bmdModeHD1080p50:		return "Mode HD1080p50";
	case bmdModeHD1080p5994:	return "Mode HD1080p5994";
	case bmdModeHD1080p6000:	return "Mode HD1080p6000";
	case bmdModeHD720p50:		return "Mode HD720p50";
	case bmdModeHD720p5994:		return "Mode HD720p5994";
	case bmdModeHD720p60:		return "Mode HD720p60";
	case bmdMode2k2398:			return "Mode 2k2398";
	case bmdMode2k24:			return "Mode 2k24";
	case bmdMode2k25:			return "Mode 2k25";
	case bmdMode2kDCI2398:		return "Mode 2kDCI2398";
	case bmdMode2kDCI24:		return "Mode 2kDCI24";
	case bmdMode2kDCI25:		return "Mode 2kDCI25";
	case bmdMode4K2160p2398:	return "Mode 4K2160p2398";
	case bmdMode4K2160p24:		return "Mode 4K2160p24";
	case bmdMode4K2160p25:		return "Mode 4K2160p25";
	case bmdMode4K2160p2997:	return "Mode 4K2160p2997";
	case bmdMode4K2160p30:		return "Mode 4K2160p30";
	case bmdMode4K2160p50:		return "Mode 4K2160p50";
	case bmdMode4K2160p5994:	return "Mode 4K2160p5994";
	case bmdMode4K2160p60:		return "Mode 4K2160p60";
	case bmdMode4kDCI2398:		return "Mode 4kDCI2398";
	case bmdMode4kDCI24:		return "Mode 4kDCI24";
	case bmdMode4kDCI25:		return "Mode 4kDCI25";
	case bmdModeUnknown:		return "Mode Unknown";
	default:					return "";
	}
}

glm::ivec2 DeckLinkDevice::getDisplayModeResolution( BMDDisplayMode mode ) {

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
