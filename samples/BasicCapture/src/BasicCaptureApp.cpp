#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"
#include "cinder/gl/gl.h"

#include "DecklinkDevice.h"
#include <map>

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

	DeckLinkDeviceDiscoveryRef				mDeviceDiscovery;

	std::map<size_t, DeckLinkDeviceRef>		mDevices;
	std::vector<SurfaceRef>					mSurfaces;
	DeckLinkDevice::Timecodes				mTimecodes;
};

BasicCaptureApp::BasicCaptureApp()
	: mDeviceDiscovery{ new DeckLinkDeviceDiscovery{ std::bind( &BasicCaptureApp::deviceArrived, this, _1, _2 ) } }
{
	getWindow()->getSignalClose().connect( [this] {
		for( auto& kv : mDevices ) {
			kv.second->cleanup();
		}
		mDevices.clear();
		mDeviceDiscovery.reset();
	} );

	gl::enableAlphaBlending();
}

void BasicCaptureApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	if( ! mDevices.empty() )
		return;

	try {
		auto device = make_shared<DeckLinkDevice>( mDeviceDiscovery.get(), decklink );
		device->start( BMDDisplayMode::bmdModeHD1080p30 );
		mDevices[index] = device;
		CI_LOG_I( "Starting sdi device." );
	}
	catch( DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

void BasicCaptureApp::update()
{
	if( mDevices.empty() )
		return;

	mSurfaces.clear();

	for( const auto& kv : mDevices ) {
		ci::Surface8uRef surface;
		if( kv.second->getSurface( surface, &mTimecodes ) ) {
			mSurfaces.push_back( surface );
		}
	}
}

void BasicCaptureApp::draw()
{
	gl::clear();

	int i = 0;
	for( const auto &surface : mSurfaces ) {
		auto windowBounds = Rectf{ app::getWindowBounds() };
		int width = windowBounds.getWidth() / mSurfaces.size();
		int height = windowBounds.getHeight();
		gl::draw( gl::Texture2d::create( *surface ), surface->getBounds(), Area{ width * i, 0, width * (i + 1), height } );

		gl::drawString( mTimecodes.rp188ltcTimecode, app::getWindowCenter() );
		++i;
	}
}

void prepareSettings( App::Settings* settings )
{
	//settings->setWindowSize( 1920, 1080 );
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
