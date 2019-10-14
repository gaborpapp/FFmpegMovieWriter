#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"

#include "FFmpegMovieWriter.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class MovieWriterApp : public App
{
 public:
	static void prepareSettings( Settings *settings );

	void setup() override;
	void update() override;
	void draw() override;
	void cleanup() override { mMovieExporter.reset(); }

 private:
	mndl::FFmpegMovieWriterRef mMovieExporter;
};

void MovieWriterApp::prepareSettings( App::Settings *settings )
{
	settings->setWindowSize( ivec2( 1280, 720 ) );
	settings->setHighDensityDisplayEnabled( false );
}

void MovieWriterApp::setup()
{
	auto format = mndl::FFmpegMovieWriter::Format();
	format.setFrameRate( 60.0f );
	format.setVideoChannelOrder( SurfaceChannelOrder::RGB );
	format.setBitRateVideo( "8000k" );

	mMovieExporter = mndl::FFmpegMovieWriter::create(
			getAppPath() / "test.mp4", getWindowWidth(), getWindowHeight(), format );
}

void MovieWriterApp::update()
{
	const int maxFrames = 600;
	if ( mMovieExporter && getElapsedFrames() > 1 && getElapsedFrames() < maxFrames )
	{
		mMovieExporter->addFrame( Surface8u::create( copyWindowSurface() ) );
	}
	else
	if ( mMovieExporter && getElapsedFrames() >= maxFrames )
	{
		mMovieExporter.reset();
	}
}

void MovieWriterApp::draw()
{
	gl::clear();
	gl::color( Color( CM_HSV, fmod( getElapsedFrames() / 30.0f, 1.0f ), 1, 1 ) );
	gl::draw( geom::Circle().center( getWindowCenter() ).radius( getElapsedFrames() ).subdivisions( 100 ) );
}

CINDER_APP( MovieWriterApp, RendererGl, MovieWriterApp::prepareSettings )
