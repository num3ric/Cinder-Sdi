//FIXME: Remove the necessity to include this first!
#include "stdafx.h"

#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/Log.h"
#include "cinder/gl/gl.h"

#include "DecklinkDevice.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class BasicCaptureApp : public App {
public:
	BasicCaptureApp();
	void keyDown( KeyEvent event ) override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
	void cleanup() override;

	std::vector<DeckLinkDeviceRef>	mDevices;

	std::vector<SurfaceRef>			mSurfaces;
	DeckLinkDevice::Timecodes		mTimecodes;
};

BasicCaptureApp::BasicCaptureApp()
{
	//for( size_t i = 0; i < 2; ++i ) {
		try {
			auto device = std::make_shared<DeckLinkDevice>( DeckLinkManager::getDevice( 0 ) );
			device->start( BMDDisplayMode::bmdModeHD1080p30 );
			mDevices.push_back( device );
		}
		catch( DecklinkExc& exc ) {
			CI_LOG_EXCEPTION( "", exc );
		}
	//}

	gl::enableAlphaBlending();

	//FIXME: debugging deallocation. This seems to work, but not on cleanup?
	getWindow()->getSignalClose().connect( [this] {
		for( auto& device : mDevices ) {
			device.reset();
		}
		DeckLinkManager::cleanup();
	} );
}

void BasicCaptureApp::update()
{
	if( mDevices.empty() )
		return;

	mSurfaces.clear();

	for( const auto& device : mDevices ) {
		ci::Surface8uRef surface;
		if( device->getSurface( surface, &mTimecodes ) ) {
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

void BasicCaptureApp::cleanup()
{
}


void BasicCaptureApp::keyDown( KeyEvent event )
{

}

void BasicCaptureApp::mouseDown( MouseEvent event )
{
}

void prepareSettings( App::Settings* settings )
{
	//settings->setWindowSize( 1920, 1080 );
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
