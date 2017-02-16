#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Log.h"
#include "cinder/Arcball.h"
#include "cinder/CameraUi.h"

#include "DeckLinkDevice.h"

using namespace ci;
using namespace ci::app;
using namespace std;
using namespace std::placeholders;
using namespace media;

class OutputSampleApp : public App {
  public:
	OutputSampleApp();
	void update() override;
	void draw() override;

	void mouseDown( MouseEvent event );
	void mouseDrag( MouseEvent event );

	void deviceArrived( IDeckLink * decklink, size_t index );

	DeckLinkDeviceDiscoveryRef	mDeviceDiscovery;
	DeckLinkDeviceRef			mDevice;

	Arcball				mArcball;

	CameraUi			mCamUi;
	CameraPersp			mCamera;
};

OutputSampleApp::OutputSampleApp()
: mDeviceDiscovery{ new DeckLinkDeviceDiscovery{ std::bind( &OutputSampleApp::deviceArrived, this, _1, _2 ) } }
{
	getWindow()->getSignalClose().connect( [this] {
		mDevice.reset();
		mDeviceDiscovery.reset();
	} );

	mCamUi = CameraUi( &mCamera );
	mCamera.setPivotDistance( 10.0f );
	mCamera.setEyePoint( vec3( 0, 0, 10.f ) );
	mCamera.lookAt( vec3( 0 ) );
	gl::enableDepth();
	//mCamera.setPerspective( calcSideFov( 18.747f, 13.365f ), app::getWindowAspectRatio(), 0.1f, 1000.0f );
}

void OutputSampleApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	// For now, we only test the first device arrived.
	if( index == 0 ) {
		try {
			mDevice = make_shared<DeckLinkDevice>( decklink );
			mDevice->getOutput()->start();
		}
		catch( DecklinkExc& exc ) {
			CI_LOG_EXCEPTION( "", exc );
		}
	}
}

void OutputSampleApp::update()
{
}

void OutputSampleApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) );
	gl::setMatrices( mCamera );
	gl::drawColorCube( vec3( 0 ), vec3( 3 ) );

	if( mDevice ) {
		mDevice->getOutput()->setWindowSurface( app::copyWindowSurface() );
	}
}

void OutputSampleApp::mouseDown( MouseEvent event )
{
	mCamUi.mouseDown( event );
}

void OutputSampleApp::mouseDrag( MouseEvent event )
{
	mCamUi.mouseDrag( event );
}

void prepareSettings( App::Settings* settings )
{
	settings->setWindowSize( 720, 486 );
	//settings->disableFrameRate();
}

CINDER_APP( OutputSampleApp, RendererGl, prepareSettings )
