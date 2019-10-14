/*
 Copyright (c) 2019, Gabor Papp, All rights reserved.
 This code is intended for use with the Cinder C++ library: http://libcinder.org

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <sstream>

#include "cinder/Log.h"
#include "cinder/Utilities.h"
#include "cinder/app/App.h"

#include "FFmpegMovieWriter.h"

using namespace ci;

namespace mndl {

int32_t FFmpegMovieWriter::sPipeId = 0;

FFmpegMovieWriter::Format::Format()
{ }

FFmpegMovieWriter::Format::Format( const Format &format ) :
	mPathFFmpeg( format.mPathFFmpeg ),
	mCodecVideo( format.mCodecVideo ),
	mCodecAudio( format.mCodecAudio ),
	mBitRateVideo( format.mBitRateVideo ),
	mBitRateAudio( format.mBitRateAudio ),
	mFrameRate( format.mFrameRate ),
	mAudioSampleRate( format.mAudioSampleRate ),
	mNumAudioInputChannels( format.mNumAudioInputChannels ),
	mVideoChannelOrder( format.mVideoChannelOrder ),
	mRecordVideo( format.mRecordVideo ),
	mRecordAudio( format.mRecordAudio ),
	mVerbose( format.mVerbose )
{ }

const FFmpegMovieWriter::Format & FFmpegMovieWriter::Format::operator=( const Format &format )
{
	mPathFFmpeg = format.mPathFFmpeg;
	mCodecVideo = format.mCodecVideo;
	mCodecAudio = format.mCodecAudio;
	mBitRateVideo = format.mBitRateVideo;
	mBitRateAudio = format.mBitRateAudio;
	mFrameRate = format.mFrameRate;
	mAudioSampleRate = format.mAudioSampleRate;
	mNumAudioInputChannels = format.mNumAudioInputChannels;
	mVideoChannelOrder = format.mVideoChannelOrder;
	mRecordVideo = format.mRecordVideo;
	mRecordAudio = format.mRecordAudio;
	mVerbose = format.mVerbose;
	return *this;
}

FFmpegMovieWriter::FFmpegMovieWriter( const ci::fs::path &path,
		int32_t width, int32_t height, const Format &format ) :
	mFormat( format ),
	mPathMovie( path ),
	mMovieWidth( width ), mMovieHeight( height )
{
	setupFFmpeg();
}

FFmpegMovieWriter::~FFmpegMovieWriter()
{
	if ( mFormat.mRecordVideo )
	{
		cleanupVideoThread();
	}
	if ( mFormat.mRecordAudio )
	{
		cleanupAudioThread();
	}
	cleanupFFmpeg();
}

void FFmpegMovieWriter::setupFFmpeg()
{
	mThreadFFmpegInitialized = false;
	mNumAudioSamplesRecorded = 0;
	mNumVideoFramesRecorded = 0;

	mThreadFFmpeg = std::shared_ptr< std::thread >( new std::thread(
				std::bind( &FFmpegMovieWriter::ffmpegThreadFn, this ) ) );
}

void FFmpegMovieWriter::ffmpegThreadFn()
{
	std::stringstream outputSettings;
	if ( mFormat.mRecordVideo )
	{
		outputSettings << " -vcodec " << mFormat.mCodecVideo <<
			" -b:v " << mFormat.mBitRateVideo;
	}
	if ( mFormat.mRecordAudio )
	{
		outputSettings << " -c:a " << mFormat.mCodecAudio <<
			" -b:a " << mFormat.mBitRateAudio;
	}
	outputSettings << " \"" << mPathMovie.string() << "\"";

	if ( mFormat.mRecordVideo )
	{
		mPipeVideo = app::getAppPath() / ( "pipevideo" + std::to_string( sPipeId ) );
		if ( ! fs::exists( mPipeVideo ) )
		{
			mkfifo( mPipeVideo.string().c_str(), 0666 );
		}
	}
	if ( mFormat.mRecordAudio )
	{
		mPipeAudio = app::getAppPath() / ( "pipeaudio" + std::to_string( sPipeId ) );
		if ( ! fs::exists( mPipeAudio ) )
		{
			mkfifo( mPipeAudio.string().c_str(), 0666 );
		}
	}
	sPipeId++;

	std::stringstream cmd;
		cmd << "bash --login -c '" << mFormat.mPathFFmpeg <<
		( mFormat.mVerbose ? "" : " -loglevel quiet " ) << "-y";
	if ( mFormat.mRecordAudio )
	{
		cmd << " -c:a pcm_f32le -f f32le -ar " << mFormat.mAudioSampleRate <<
			" -ac " << mFormat.mNumAudioInputChannels << " -i \"" <<
			mPipeAudio.string() << "\"";
	}
	else
	{
		cmd << " -an";
	}

	if ( mFormat.mRecordVideo )
	{
		const std::vector< std::string > pixelFormats = { "rgba", "bgra", "argb", "abgr",
			"rgb0", "bgr0", "0rgb", "0bgr", "rgb24", "bgr24" };

		int code = mFormat.mVideoChannelOrder.getCode();
		std::string pixelFormat = "rgb24";
		if ( code < pixelFormats.size() )
		{
			pixelFormat = pixelFormats[ code ];
		}
		cmd << " -r "<< mFormat.mFrameRate <<
			" -s " << mMovieWidth << "x" << mMovieHeight <<
			" -f rawvideo -pix_fmt " << pixelFormat <<
			" -i \"" << mPipeVideo.string() << "\" -r " << mFormat.mFrameRate;
	}
	else
	{
		cmd << " -vn";
	}
	cmd << " " + outputSettings.str() + "' &";
	std::string command = cmd.str();


	int result = system( command.c_str() );
	int serrno = errno;
	if ( result == 0 )
	{
		CI_LOG_I( command << " command completed." );
		mThreadFFmpegInitialized = true;
	}
	else
	{
		CI_LOG_E( command << " command failed with result: " << result
							<< " - " << ::strerror( serrno ) << "." );
		// TODO: throw
	}

	if ( mFormat.mRecordAudio )
	{
		setupAudioThread();
	}
	if ( mFormat.mRecordVideo )
	{
		setupVideoThread();
	}
}

void FFmpegMovieWriter::cleanupFFmpeg()
{
	mThreadFFmpeg->join();
	mThreadFFmpeg.reset();

	if ( mFormat.mRecordVideo )
	{
		fs::remove( mPipeVideo );
	}
	if ( mFormat.mRecordAudio )
	{
		fs::remove( mPipeAudio );
	}
}

void FFmpegMovieWriter::setupVideoThread()
{
	mVideoThreadShouldQuit = false;
	mVideoFrames = new ConcurrentCircularBuffer< Surface8uRef >( 10 );
	mThreadVideo = std::shared_ptr< std::thread >( new std::thread(
				std::bind( &FFmpegMovieWriter::videoThreadFn, this ) ) );
}

void FFmpegMovieWriter::cleanupVideoThread()
{
	mVideoThreadShouldQuit = true;
	mVideoFrames->cancel();
	mThreadVideo->join();
	mThreadVideo.reset();
	delete mVideoFrames;
	mVideoFrames = nullptr;
}

void FFmpegMovieWriter::videoThreadFn()
{
	ThreadSetup threadSetup;

	int fd = ::open( mPipeVideo.string().c_str(), O_WRONLY );

	Surface8uRef frame;

	while ( ! mVideoThreadShouldQuit )
	{
		if ( mVideoFrames->isNotEmpty() )
		{
			mVideoFrames->popBack( &frame );
			if ( ! frame )
			{
				break;
			}

			int offset = 0;
			int remaining = frame->getWidth() * frame->getHeight() *
				frame->getPixelBytes();

			while ( remaining > 0 )
			{
				int written = ::write( fd, (char *)frame->getData() +
						offset, remaining );
				int serrno = errno;

				if ( written > 0 )
				{
					remaining -= written;
					offset += written;
				}
				else
				if ( written < 0 )
				{
					CI_LOG_E( "Write to pipe failed with error -> " << serrno
							<< " - " << ::strerror( serrno ) << "." );
					break;
				}

				if ( mVideoThreadShouldQuit )
				{
					break;
				}
			}

			frame.reset();
		}
		else
		{
			ci::sleep( 1 );
		}
	}

	::close( fd );
}

void FFmpegMovieWriter::addFrame( SurfaceRef surface )
{
	if ( ! mThreadFFmpegInitialized )
	{
		CI_LOG_W( "Dropping video frame" );
		return;
	}
	if ( ! mVideoFrames )
	{
		return;
	}

	size_t numFramesToAdd = 1;

	if ( mFormat.mRecordAudio )
	{
		double videoRecordedTime = mNumVideoFramesRecorded / mFormat.mFrameRate;
		double audioRecordedTime = mNumAudioSamplesRecorded /
			(double)mFormat.mAudioSampleRate;
		double syncDelta = audioRecordedTime - videoRecordedTime;
		const double frameTime = 1.0 / mFormat.mFrameRate;

		if ( syncDelta > frameTime )
		{
			// not enough video frames, we need to send extra video frames.
			while ( syncDelta > frameTime )
			{
				numFramesToAdd++;
				syncDelta -= frameTime;
			}
			CI_LOG_I( "recDelta = " << syncDelta << ". Not enough video frames for desired frame rate, copied this frame " << numFramesToAdd << " times." << audioRecordedTime << " v: " << videoRecordedTime );
		}
		else
		if ( syncDelta < -frameTime )
		{
			// more than one video frame is waiting, skip this frame
			numFramesToAdd = 0;
			CI_LOG_I( "recDelta = " << syncDelta << ". Too many video frames, skipping." );
		}
	}

	for ( size_t i = 0; i < numFramesToAdd; i++ )
	{
		mVideoFrames->pushFront( surface );
		mNumVideoFramesRecorded++;
	}
}

void FFmpegMovieWriter::setupAudioThread()
{
	mAudioThreadShouldQuit = false;
	mAudioFrames = new ConcurrentCircularBuffer< AudioFrame * >( 20 );
	mThreadAudio = std::shared_ptr< std::thread >( new std::thread(
				std::bind( &FFmpegMovieWriter::audioThreadFn, this ) ) );
}

void FFmpegMovieWriter::cleanupAudioThread()
{
	mAudioThreadShouldQuit = true;
	mAudioFrames->cancel();
	mThreadAudio->join();
	mThreadAudio.reset();
	delete mAudioFrames;
	mAudioFrames = nullptr;
}

void FFmpegMovieWriter::audioThreadFn()
{
	ThreadSetup threadSetup;

	int fd = ::open( mPipeAudio.string().c_str(), O_WRONLY );

	while ( ! mAudioThreadShouldQuit )
	{
		AudioFrame *frame = nullptr;

		if ( mAudioFrames->isNotEmpty() )
		{
			mAudioFrames->popBack( &frame );
			if ( frame == nullptr )
			{
				break;
			}

			int offset = 0;
			int remaining = frame->mSize * sizeof( float );

			while ( remaining > 0 )
			{
				int written = ::write( fd, (uint8_t *)frame->mData + offset,
						remaining );
				int serrno = errno;

				if ( written > 0 )
				{
					remaining -= written;
					offset += written;
				}
				else
				if ( written < 0 )
				{
					CI_LOG_E( "Write to pipe failed with error -> " << serrno
							<< " - " << strerror( serrno ) << "." );
					break;
				}

				if ( mAudioThreadShouldQuit )
				{
					break;
				}
			}

			delete [] frame->mData;
			delete frame;
			frame = nullptr;
		}
		else
		{
			ci::sleep( 1 );
		}
	}

	::close( fd );
}

void FFmpegMovieWriter::addAudioBuffer( const audio::Buffer *buffer )
{
	if ( ! mThreadFFmpegInitialized )
	{
		CI_LOG_W( "Dropping audio frame" );
		return;
	}
	if ( ! mAudioFrames )
	{
		return;
	}

	size_t size = buffer->getSize();
	AudioFrame *samples = new AudioFrame;
	samples->mData = new float[ size ];
	samples->mSize = size;
	const float *ch0 = buffer->getChannel( 0 );
	const float *ch1 = buffer->getChannel( 1 );
	size_t numFrames = buffer->getNumFrames();
	mNumAudioSamplesRecorded += numFrames;

	for ( size_t i = 0; i < numFrames; i++ )
	{
		samples->mData[ i * 2 ] = ch0[ i ];
		samples->mData[ i * 2 + 1 ] = ch1[ i ];
	}

	mAudioFrames->pushFront( samples );
}

}
