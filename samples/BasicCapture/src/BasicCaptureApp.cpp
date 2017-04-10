#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"
#include "cinder/gl/gl.h"

#include "DeckLinkDevice.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace std::placeholders;

class BasicCaptureApp : public App {
public:
	BasicCaptureApp();
	void update() override;
	void draw() override;

	void deviceArrived( IDeckLink * decklink, size_t index );
	void frameArrived( media::FrameEvent& frameEvent );

	media::DeckLinkDeviceDiscoveryRef	mDeviceDiscovery;
	media::DeckLinkDeviceRef			mDevice;
	ci::signals::Connection				mConnectionFrame;

	std::mutex					mFrameLock;
	SurfaceRef					mSurface;
};

BasicCaptureApp::BasicCaptureApp()
	: mDeviceDiscovery{ new media::DeckLinkDeviceDiscovery{ std::bind( &BasicCaptureApp::deviceArrived, this, _1, _2 ) } }
{
	getWindow()->getSignalClose().connect( [this] {
		mConnectionFrame.disconnect();
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
		mDevice = make_shared<media::DeckLinkDevice>( decklink );
		mDevice->getInput()->start( BMDDisplayMode::bmdModeHD1080p30, false );
		mConnectionFrame = mDevice->getInput()->getFrameSignal().connect( std::bind( &BasicCaptureApp::frameArrived, this, _1 ) );
		CI_LOG_I( "Starting sdi device: " << index );
	}
	catch( media::DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

void BasicCaptureApp::frameArrived( media::FrameEvent& frameEvent )
{
	/* Note that both device and frame callbacks are triggered from DeckLink worker threads, hence the mutex here. */
	std::lock_guard<std::mutex> lock{ mFrameLock };
	frameEvent.surfaceData.getSurface( mSurface );
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

void prepareSettings( App::Settings* settings )
{
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
