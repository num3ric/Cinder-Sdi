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
	void frameArrived( media::VideoFrameBGRA * frame );

	DeckLinkDeviceDiscoveryRef	mDeviceDiscovery;
	DeckLinkDeviceRef			mDevice;

	std::mutex					mFrameLock;
	SurfaceRef					mSurface;
};

BasicCaptureApp::BasicCaptureApp()
	: mDeviceDiscovery{ new DeckLinkDeviceDiscovery{ std::bind( &BasicCaptureApp::deviceArrived, this, _1, _2 ) } }
{
	getWindow()->getSignalClose().connect( [this] {
		mDevice.reset();
		mDeviceDiscovery.reset();
	} );

	gl::enableAlphaBlending();
}

void BasicCaptureApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	// For now, we only test the first device arrived.
	if( mDevice )
		return;

	try {
		mDevice = make_shared<DeckLinkDevice>( decklink );
		auto callback = std::bind( &BasicCaptureApp::frameArrived, this, _1 );
		mDevice->getInput()->start( BMDDisplayMode::bmdModeHD1080p30, callback );
		CI_LOG_I( "Starting sdi device: " << index );
	}
	catch( DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

void BasicCaptureApp::frameArrived( media::VideoFrameBGRA * frame )
{
	/* Note that both device and frame callbacks are triggered from DeckLink worker threads, hence the mutex here. */
	std::lock_guard<std::mutex> lock{ mFrameLock };
	frame->getSurface( mSurface );
}

void BasicCaptureApp::update()
{
}

void BasicCaptureApp::draw()
{
	gl::clear();

	std::lock_guard<std::mutex> lock{ mFrameLock };
	if( mSurface ) {
		gl::draw( gl::Texture2d::create( *mSurface ), mSurface->getBounds(), app::getWindowBounds() );
	}
}

void BasicCaptureApp::keyDown(KeyEvent event)
{
	auto code = event.getCode();
	if( code >= KeyEvent::KEY_1 && code <= KeyEvent::KEY_4 ) {
		auto index = static_cast<size_t>( code - KeyEvent::KEY_1 );
		mDevice.reset( new DeckLinkDevice{ mDeviceDiscovery->getDevice( index ) } );
		auto callback = std::bind( &BasicCaptureApp::frameArrived, this, _1 );
		mDevice->getInput()->start( BMDDisplayMode::bmdModeHD1080p30, callback );
	}
}

void prepareSettings( App::Settings* settings )
{
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
