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

	gl::FboRef					mFbo;

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
	mCamera.setAspectRatio( app::getWindowAspectRatio() );

	mFbo = gl::Fbo::create( app::getWindowWidth(),
							app::getWindowHeight(),
							gl::Fbo::Format()
								.colorTexture( gl::Texture2d::Format().internalFormat( GL_RGBA ).dataType( GL_UNSIGNED_INT_8_8_8_8_REV ) ) );


	gl::enableVerticalSync( false );
	gl::enableDepth();
}

void OutputSampleApp::deviceArrived( IDeckLink * decklink, size_t index )
{
	try {
		mDevice = make_shared<DeckLinkDevice>( decklink );
		mDevice->getOutput()->start( BMDDisplayMode::bmdModeHD720p60 );
		CI_LOG_I( "Starting output device." );
	}
	catch( DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

void OutputSampleApp::update()
{
	gl::ScopedMatrices push;
	gl::ScopedFramebuffer bind{ mFbo };
	gl::clear();
	gl::setMatrices( mCamera );
	gl::drawColorCube( vec3( 0 ), vec3( 3 ) );
}

void OutputSampleApp::draw()
{
	gl::clear();
	gl::draw( mFbo->getColorTexture() );

	if( mDevice ) {
		mDevice->getOutput()->sendTexture( mFbo->getColorTexture() );
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
	settings->setWindowSize( 1280, 720 );
	settings->setResizable( false );
}

CINDER_APP( OutputSampleApp, RendererGl, prepareSettings )
