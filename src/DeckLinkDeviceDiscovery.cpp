#include "cinder/app/App.h"
#include "cinder/Log.h"
#include "cinder/FileWatcher.h"	

#include "DeckLinkDeviceDiscovery.h"

#include <vector>

using namespace ci;
using namespace media;

IDeckLinkVideoConversion* DeckLinkDeviceDiscovery::sVideoConverter = NULL;

DeckLinkDeviceDiscovery::DeckLinkDeviceDiscovery( std::function<void( IDeckLink*, size_t )> deviceCallback )
	: mDeviceArrivedCallback{ deviceCallback }, m_deckLinkDiscovery( NULL ), m_refCount( 1 )
{
	auto vert = "#version 150 \n"
	"uniform mat4	ciModelViewProjection; \n"
	"in vec4			ciPosition; \n"
	"in vec2			ciTexCoord0; \n"
	"out vec2		vTexCoord0; \n"
	"void main() { \n"
	"	vTexCoord0 = ciTexCoord0; \n"
	"	gl_Position = ciModelViewProjection * ciPosition; \n"
	"} \n";
	auto frag = "#version 150 \n"
	"uniform ivec2		ciWindowSize; \n"
	"uniform sampler2D	UYVYtex; \n"
	"in vec2				vTexCoord0; \n"
	"out vec4 			fragColor; \n"
	"void main() \n"
	"{ \n"
	"	vec2 coord = vec2( vTexCoord0.x, 1.0 - vTexCoord0.y ); \n"
	"	ivec2 size = textureSize( UYVYtex, 0 ); \n"
	"	vec4 uyvyColor = texture( UYVYtex, coord ); \n"
	"	float Y, Cb, Cr; \n"
	"	vec3 color; \n"
	"	Cb = uyvyColor.x; \n"
	"	Cr = uyvyColor.z; \n"
	"	Y = mix( uyvyColor.y, uyvyColor.w, fract( 2.0 * coord.x * size.x ) ); \n"
	"	float max = 255; \n"
	"	float half = 0.5 * max; \n"
	"	Y = 1.164 * ( clamp( Y * max, 0, 255 ) - 16 ); \n"
	"	Cb = clamp( Cb * max, 0, 255 ) - half; \n"
	"	Cr = clamp( Cr * max, 0, 255 ) - half; \n"
	"	color.r = Y + 1.793 * Cr; \n"
	"	color.g = Y - 0.534 * Cr - 0.213 * Cb; \n"
	"	color.b = Y + 2.115 * Cb; \n"
	"	color = color / vec3( max ); \n"
	"	fragColor = vec4( color, 1.0 ); \n"
	"} \n";
	mGlslYUV2RGB = gl::GlslProg::create( vert, frag );
	mGlslYUV2RGB->uniform( "UYVYtex", 0 );

	if( CoCreateInstance( CLSID_CDeckLinkDiscovery, NULL, CLSCTX_ALL, IID_IDeckLinkDiscovery, (void**)&m_deckLinkDiscovery ) != S_OK ) {
		m_deckLinkDiscovery = NULL;
		CI_LOG_E( "Failed to create decklink discovery instance." );
		return;
	}

	if( ! sVideoConverter ) {
		if( CoCreateInstance( CLSID_CDeckLinkVideoConversion, NULL, CLSCTX_ALL, IID_IDeckLinkVideoConversion, (void**)&sVideoConverter ) != S_OK ) {
			throw DecklinkExc{ "Failed to create the decklink video converter." };
			sVideoConverter = NULL;
		}
	}

	m_deckLinkDiscovery->InstallDeviceNotifications( this );
}


DeckLinkDeviceDiscovery::~DeckLinkDeviceDiscovery()
{
	if( m_deckLinkDiscovery != NULL )
	{
		// Uninstall device arrival notifications and release discovery object
		m_deckLinkDiscovery->UninstallDeviceNotifications();
		m_deckLinkDiscovery->Release();
		m_deckLinkDiscovery = NULL;
	}

	for( auto& kv : mDevices ) {
		kv.second->Release();
	}
}

IDeckLink * DeckLinkDeviceDiscovery::getDevice( size_t index ) const
{
	return mDevices.at( index );
}

std::string DeckLinkDeviceDiscovery::getDeviceName( IDeckLink * device )
{
	if( device == nullptr ) {
		CI_LOG_E( "No device." );
		return "";
	}
	
	BSTR cfStrName;
	// Get the name of this device
	if( device->GetDisplayName( &cfStrName ) == S_OK ) {
		assert( cfStrName != NULL );
		std::wstring ws( cfStrName, SysStringLen( cfStrName ) );
		return ws2s( ws );
	}

	CI_LOG_I( "No device name found." );
	return "DeckLink";
}

HRESULT     DeckLinkDeviceDiscovery::DeckLinkDeviceArrived( IDeckLink* decklink )
{
	IDeckLinkAttributes* deckLinkAttributes = NULL;
	LONGLONG index = 0;
	if( decklink->QueryInterface( IID_IDeckLinkAttributes, (void**)&deckLinkAttributes ) == S_OK ) {
		if( deckLinkAttributes->GetInt( BMDDeckLinkSubDeviceIndex, &index ) != S_OK ) {
			CI_LOG_E( "Cannot read device index." );
		}
		deckLinkAttributes->Release();
	}

	CI_LOG_I( "Device " << getDeviceName( decklink ) << " with index " << index << " arrived." );

	mDevices[index] = decklink;
	decklink->AddRef();

	mDeviceArrivedCallback( decklink, static_cast<size_t>( index ) );
	return S_OK;
}

HRESULT     DeckLinkDeviceDiscovery::DeckLinkDeviceRemoved(/* in */ IDeckLink* decklink )
{
	CI_LOG_I( "Device " << getDeviceName( decklink ) << " removed" );

	return S_OK;
}

HRESULT	STDMETHODCALLTYPE DeckLinkDeviceDiscovery::QueryInterface( REFIID iid, LPVOID *ppv )
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
	else if( iid == IID_IDeckLinkDeviceNotificationCallback )
	{
		*ppv = (IDeckLinkDeviceNotificationCallback*)this;
		AddRef();
		result = S_OK;
	}

	return result;
}

ULONG STDMETHODCALLTYPE DeckLinkDeviceDiscovery::AddRef( void )
{
	return InterlockedIncrement( (LONG*)&m_refCount );
}

ULONG STDMETHODCALLTYPE DeckLinkDeviceDiscovery::Release( void )
{
	ULONG		newRefValue;

	newRefValue = InterlockedDecrement( (LONG*)&m_refCount );
	if( newRefValue == 0 )
	{
		delete this;
		return 0;
	}

	return newRefValue;
}

