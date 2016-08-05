#include "stdafx.h"
#include <stdint.h>

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
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
	void cleanup() override;

	std::unique_ptr<DeckLinkDevice>	mDecklink;
	SurfaceRef			mSurface;
	gl::TextureRef		mTexture;
};

void BasicCaptureApp::setup()
{
	try {
		mDecklink = std::unique_ptr<DeckLinkDevice>( new DeckLinkDevice{ DeckLinkManager::getDevice( 0 ) } );
		std::stringstream ss;
		for( auto& mode : mDecklink->getDisplayModeNames() ) {
			ss << mode << ", ";
		}
		app::console() << ss.str() << std::endl;

		mDecklink->start( BMDDisplayMode::bmdModeHD1080p30 );
	}
	catch( DecklinkExc& exc ) {
		CI_LOG_EXCEPTION( "", exc );
	}

	gl::enableAlphaBlending();
}

void BasicCaptureApp::mouseDown( MouseEvent event )
{
}

void BasicCaptureApp::update()
{
	if( !mDecklink )
		return;

	//mDecklink->getSurface( mSurface );
	mDecklink->getTexture( mTexture );
}

void BasicCaptureApp::draw()
{
	gl::clear();

	if( mTexture ) {
		gl::ScopedMatrices push;
		auto glsl = DeckLinkManager::getYUV2RGBShader();
		gl::ScopedGlslProg bind( glsl );
		gl::ScopedTextureBind tex0( mTexture, 0 );

		gl::setMatricesWindow( app::getWindowSize() );
		gl::drawSolidRect( app::getWindowBounds() );
	}

	if( mSurface ) {
		gl::draw( gl::Texture2d::create( *mSurface ) );
	}
}

void BasicCaptureApp::cleanup()
{

}

void prepareSettings( App::Settings* settings )
{
	//settings->setWindowSize( 1920, 1080 );
	settings->setWindowSize( 0.5 * 1920, 0.5 * 1080 );
}

CINDER_APP( BasicCaptureApp, RendererGl, prepareSettings )
