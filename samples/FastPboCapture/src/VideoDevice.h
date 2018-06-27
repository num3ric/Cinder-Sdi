#pragma once

#include "DeckLinkDevice.h"

typedef std::shared_ptr<class VideoDevice> VideoDeviceRef;
typedef std::unique_ptr<class VideoDevice> VideoDevicePtr;
class VideoDevice final {
public:
	VideoDevice( IDeckLink * decklink, BMDDisplayMode videoMode, size_t index );
	~VideoDevice();

	void update();

	bool getTexture( ci::gl::Texture2dRef& videoTexture ) const;
	size_t getDeviceIndex() const { return mDeviceIndex; }
private:
	static const int NUM_BUFFERS;

	void frameArrived( media::FrameEvent& frameEvent );

	std::atomic<bool>		mHasNewFrame;
	media::VideoFrameBGRA	mLastFrame;

	ci::gl::TextureRef		mTexs[2];
	ci::gl::PboRef			mPbos[2];
	int						mCurrentTex, mCurrentPbo;
	size_t					mDeviceIndex;

	std::mutex							mFrameLock;
	media::DeckLinkDeviceRef			mDevice;
	ci::signals::Connection				mConnectionFrame;
};
