#include "VideoDevice.h"

#include "cinder/app/AppBase.h"
#include "cinder/Log.h"

using namespace ci;

const int VideoDevice::NUM_BUFFERS = 2;

VideoDevice::VideoDevice( IDeckLink * decklink, BMDDisplayMode videoMode, size_t index )
	: mHasNewFrame{ false }
	, mDeviceIndex{ index }
	, mCurrentTex{ 0 }
	, mCurrentPbo{ 0 }
	, mTexs{ nullptr, nullptr }
	, mPbos{ nullptr, nullptr }
{
	try {
		mDevice = std::make_shared<media::DeckLinkDevice>( decklink );
		mDevice->getInput()->start( videoMode, false );
		mConnectionFrame = mDevice->getInput()->getFrameSignal().connect( std::bind( &VideoDevice::frameArrived, this, std::placeholders::_1 ) );
		CI_LOG_I( "Starting sdi device: " << index );
	}
	catch (media::DecklinkExc& exc) {
		CI_LOG_EXCEPTION( "", exc );
	}
}

VideoDevice::~VideoDevice()
{
	mConnectionFrame.disconnect();
	mDevice.reset();
}

void VideoDevice::update()
{
	if (mHasNewFrame) {
		{
			std::lock_guard<std::mutex> lock{ mFrameLock };
			if (mTexs[0] == nullptr) {
				for (int b = 0; b < NUM_BUFFERS; ++b) {
					mTexs[b] = gl::Texture::create( mLastFrame.GetWidth(), mLastFrame.GetHeight(), gl::Texture::Format().internalFormat( GL_RGBA ) );
					mPbos[b] = gl::Pbo::create( GL_PIXEL_UNPACK_BUFFER, mLastFrame.GetRowBytes() * mLastFrame.GetHeight(), nullptr, GL_STREAM_DRAW );
				}
			}

			gl::ScopedBuffer bscp( mPbos[mCurrentPbo] );
			void *pboData = mPbos[mCurrentPbo]->map( GL_WRITE_ONLY );
			memcpy( (uint8_t*)pboData, mLastFrame.data(), mLastFrame.GetRowBytes() * mLastFrame.GetHeight() );
			mPbos[mCurrentPbo]->unmap();
		}

		mTexs[mCurrentTex]->update( mPbos[mCurrentPbo], GL_RGBA, GL_UNSIGNED_BYTE );

		mCurrentPbo = (mCurrentPbo + 1) % NUM_BUFFERS;
		mCurrentTex = (mCurrentTex + 1) % NUM_BUFFERS;
		mHasNewFrame = false;
	}
}

bool VideoDevice::getTexture( gl::Texture2dRef& videoTexture ) const
{
	if (mTexs[mCurrentTex] != nullptr) {
		videoTexture = mTexs[mCurrentTex];
		return true;
	}
	return false;
}

void VideoDevice::frameArrived( media::FrameEvent & frameEvent )
{
	std::lock_guard<std::mutex> lock{ mFrameLock };
	mHasNewFrame = true;
	mLastFrame = frameEvent.surfaceData;
}