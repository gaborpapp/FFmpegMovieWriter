/*
 Press Space to record from capture device with audio.

 Currently https://github.com/cinder/Cinder/pull/2061 is required on Linux
*/

#include <atomic>

#include "cinder/Capture.h"
#include "cinder/Log.h"
#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/audio/audio.h"
#include "cinder/gl/gl.h"

#include "FFmpegMovieWriter.h"

using namespace ci;
using namespace ci::app;
using namespace std;

typedef std::shared_ptr< class FFmpegSampleRecorderNode > FFmpegSampleRecorderNodeRef;

class FFmpegSampleRecorderNode : public audio::SampleRecorderNode
{
 public:
	FFmpegSampleRecorderNode( std::function< void( audio::Buffer *buffer ) > const &fn ) :
		mFn( fn )
	{ }

 protected:
	void process( audio::Buffer *buffer ) override { mFn( buffer ); }

	std::function< void( audio::Buffer *buffer ) > mFn;
};

class FFmpegMovieWriterApp : public App
{
 public:
	void setup() override;
	void update() override;
	void draw() override;
	void keyDown( KeyEvent event ) override;
	void cleanup() override;

 private:
	const int kDefaultCaptureWidth = 1280;
	const int kDefaultCaptureHeight = 720;
	int mCaptureWidth = kDefaultCaptureWidth;
	int mCaptureHeight = kDefaultCaptureHeight;

	CaptureRef mCapture;
	void setupCapture();

	gl::Texture2dRef mTexCapture;

	void updateCapture();

	void setupAudioCapture();
	void cleanupAudioCapture();

	audio::InputDeviceNodeRef mInputDeviceNode;
	FFmpegSampleRecorderNodeRef mFFmpegSampleRecorderNode;

	size_t mSampleRate;
	size_t mNumInputChannels;
	SurfaceChannelOrder mCaptureChannelOrder;

	mndl::FFmpegMovieWriterRef mMovieWriter;

	std::atomic< bool > mRecording;
	bool mLastRecording;
};

void FFmpegMovieWriterApp::setup()
{
	mRecording = false;
	mLastRecording = false;

	setupAudioCapture();
	setupCapture();
}

void FFmpegMovieWriterApp::setupAudioCapture()
{
	auto ctx = audio::Context::master();
	app::console() << audio::Device::printDevicesToString() << std::endl;

	const auto &device = audio::Device::getDefaultInput();
	mInputDeviceNode = ctx->createInputDeviceNode( device );

	mSampleRate = device->getSampleRate();
	mNumInputChannels = device->getNumInputChannels();

	auto fn =
		[ this ]( audio::Buffer *buffer )
		{
			if ( mRecording && mMovieWriter )
			{
				mMovieWriter->addAudioBuffer( buffer );
			}
		};

	mFFmpegSampleRecorderNode = ctx->makeNode( new FFmpegSampleRecorderNode( fn ) );
	mInputDeviceNode >> mFFmpegSampleRecorderNode;

	mInputDeviceNode->enable();
	//mFFmpegSampleRecorderNode->enable();
	ctx->enable();
}

void FFmpegMovieWriterApp::cleanupAudioCapture()
{
	mInputDeviceNode->disable();
	auto ctx = audio::Context::master();
	ctx->disable();
}

void FFmpegMovieWriterApp::setupCapture()
{
	auto devices = Capture::getDevices();
	for ( const auto &device : devices )
	{
		try
		{
			mCapture = Capture::create( kDefaultCaptureWidth, kDefaultCaptureHeight, device );
			mCapture->start();
			mCaptureWidth = mCapture->getWidth();
			mCaptureHeight = mCapture->getHeight();
			CI_LOG_I( "Capture " << mCaptureWidth << "x" << mCaptureHeight );
			break;
		}
		catch ( const CaptureExc &exc )
		{
			CI_LOG_W( "Failed to initialize the Capture: " << exc.what() << " " <<
					device->getName() << " " << device->getUniqueId() );
		}
	}
	if ( ! mCapture )
	{
		CI_LOG_E( "Failed to initialize the Capture." );
		quit();
	}

	if ( mCapture )
	{
		mTexCapture = gl::Texture2d::create( mCapture->getWidth(), mCapture->getHeight(),
				gl::Texture2d::Format().loadTopDown() );
	}

}

void FFmpegMovieWriterApp::update()
{
	float now = getElapsedSeconds();

	if ( mRecording != mLastRecording )
	{
		if ( ! mRecording )
		{
			mMovieWriter.reset();
		}
		else
		{
			static int32_t id = 0;
			auto format = mndl::FFmpegMovieWriter::Format();
			format.setAudioSampleRate( mSampleRate );
			format.setNumAudioInputChannels( mNumInputChannels );
			format.setRecordAudio();
			format.setVideoChannelOrder( mCaptureChannelOrder );
			mMovieWriter = mndl::FFmpegMovieWriter::create(
				getAppPath() /
				( "recording-" + std::to_string( id++ ) + ".mp4" ),
				mCaptureWidth, mCaptureHeight, format );
		}
		mLastRecording = mRecording;
	}

	if ( mCapture && mCapture->checkNewFrame() )
	{
		auto surf = mCapture->getSurface();

		if ( mCaptureChannelOrder.getCode() == SurfaceChannelOrder::UNSPECIFIED )
		{
			mCaptureChannelOrder = surf->getChannelOrder();
		}

		mTexCapture->update( *surf );
		if ( mRecording )
		{
			mMovieWriter->addFrame( surf );
		}
	}
}

void FFmpegMovieWriterApp::draw()
{
	gl::ScopedViewport scpViewport( getWindowSize() );
	gl::setMatricesWindow( getWindowSize() );

	gl::clear();
	if ( mTexCapture )
	{
		Rectf srcRect( mTexCapture->getBounds() );
		Rectf dstRect = srcRect.getCenteredFit( getWindowBounds(), true );
		gl::draw( mTexCapture, dstRect );
	}

	if ( mRecording )
	{
		gl::drawString( "Recording", vec2( 40 ) );
	}
}

void FFmpegMovieWriterApp::keyDown( KeyEvent event )
{
	switch ( event.getCode() )
	{
		case KeyEvent::KEY_SPACE:
			mRecording = ! mRecording;
			break;

		case KeyEvent::KEY_ESCAPE:
			quit();
			break;
	}
}

void FFmpegMovieWriterApp::cleanup()
{
	cleanupAudioCapture();
}

CINDER_APP( FFmpegMovieWriterApp, RendererGl,
		[]( app::App::Settings *settings )
		{
			settings->setWindowSize( ivec2( 1280, 720 ) );
		} )
