#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Log.h"

#include "VideoDevice.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace std::placeholders;

class FastPboCaptureApp : public App {
  public:
	void setup() override;
	void update() override;
	void draw() override;
private:
	void deviceArrived( IDeckLink * decklink, size_t index );

	std::vector<VideoDeviceRef>			mDevices;
	media::DeckLinkDeviceDiscoveryRef	mDeviceDiscovery;
	gl::Texture2dRef					mVideoTexture;
};

void FastPboCaptureApp::setup()
{
	mDeviceDiscovery = std::make_shared<media::DeckLinkDeviceDiscovery>( std::bind( &FastPboCaptureApp::deviceArrived, this, _1, _2 ) );

	app::getWindow()->getSignalClose().connect( [this] {
		mDevices.clear();
		mDeviceDiscovery.reset();
	} );
}

void FastPboCaptureApp::update()
{
	for (const auto& device : mDevices) {
		device->update();
	}
}

void FastPboCaptureApp::draw()
{
	gl::clear(); 

	if( ! mDevices.empty() ) {
		if( mDevices.at( 0 )->getTexture( mVideoTexture ) ) {
			mVideoTexture->setTopDown( true );
			gl::draw( mVideoTexture, app::getWindowBounds() );
		}
	}
}

void FastPboCaptureApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	mDevices.push_back( std::make_shared<VideoDevice>( decklink, BMDDisplayMode::bmdModeHD1080p30, index ) );
}

CINDER_APP( FastPboCaptureApp, RendererGl, []( App::Settings * settings ) {
	settings->setWindowSize( 1280, 720 );
} )
