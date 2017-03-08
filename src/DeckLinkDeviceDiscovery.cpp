#include "cinder/app/App.h"
#include "cinder/Log.h"

#include "DeckLinkDeviceDiscovery.h"

using namespace media;

IDeckLinkVideoConversion* DeckLinkDeviceDiscovery::sVideoConverter = NULL;

DeckLinkDeviceDiscovery::DeckLinkDeviceDiscovery( std::function<void( IDeckLink*, size_t )> deviceCallback )
	: mDeviceArrivedCallback{ deviceCallback }, m_deckLinkDiscovery( NULL ), m_refCount( 1 )
{
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

