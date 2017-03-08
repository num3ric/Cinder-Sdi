#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"
#include "cinder/gl/gl.h"

#include "DeckLinkDevice.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace std::placeholders;
using namespace media;

class BasicCaptureApp : public App {
public:
	BasicCaptureApp();
	void update() override;
	void draw() override;
	void keyDown( KeyEvent event ) override;

	void deviceArrived( IDeckLink * decklink, size_t index );

	DeckLinkDeviceDiscoveryRef	mDeviceDiscovery;
	DeckLinkDeviceRef			mDevice;
	SurfaceRef					mSurface;
};

BasicCaptureApp::BasicCaptureApp()
	: mDeviceDiscovery{ new DeckLinkDeviceDiscovery{ std::bind( &BasicCaptureApp::deviceArrived, this, _1, _2 ) } }
{
	getWindow()->getSignalClose().connect( [this] {
		mDevice.reset();
		mDeviceDiscovery.reset();
	} );

	//gl::enableVerticalSync( false );
	gl::enableAlphaBlending();
}

void BasicCaptureApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	// For now, we only test the first device arrived.
	if( mDevice )
		return;

	try {
		mDevice = make_shared<DeckLinkDevice>( decklink );
		mDevice->getInput()->start( BMDDisplayMode::bmdModeHD1080p30 );
		CI_LOG_I( "Starting sdi device: " << index );
	}
	catch( DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

void BasicCaptureApp::update()
{
	if( mDevice )
		mDevice->getInput()->getSurface( mSurface );
}

void BasicCaptureApp::draw()
{
	gl::clear();

	if( mSurface ) {
		gl::draw( gl::Texture2d::create( *mSurface ), mSurface->getBounds(), app::getWindowBounds() );
	}
	gl::drawString( std::to_string( getAverageFps() ), app::getWindowCenter() );
}

void BasicCaptureApp::keyDown(KeyEvent event)
{
	auto code = event.getCode();
	if( code >= KeyEvent::KEY_1 && code <= KeyEvent::KEY_4 ) {
		auto index = static_cast<size_t>( code - KeyEvent::KEY_1 );
		mDevice.reset( new DeckLinkDevice{ mDeviceDiscovery->getDevice( index ) } );
		mDevice->getInput()->start( BMDDisplayMode::bmdModeHD1080p30 );
	}
}

void prepareSettings( App::Settings* settings )
{
	//settings->setWindowSize( 1920, 1080 );
	//settings->disableFrameRate();
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
