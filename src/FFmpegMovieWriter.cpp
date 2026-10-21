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

#if defined( _WIN32 )
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

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
	mExtraOutputArgs( format.mExtraOutputArgs ),
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
	mExtraOutputArgs = format.mExtraOutputArgs;
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
	// ffmpegThreadFn creates the video/audio threads, so it has to finish before
	// they are cleaned up; otherwise a writer destroyed right after creation could
	// have its threads spawned after the cleanup calls found them still null
	mThreadFFmpeg->join();
	mThreadFFmpeg.reset();

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
	mStartupDone = false;
	mFailed = false;
	mNumAudioSamplesRecorded = 0;
	mNumVideoFramesRecorded = 0;

	mThreadFFmpeg = std::shared_ptr< std::thread >( new std::thread(
				std::bind( &FFmpegMovieWriter::ffmpegThreadFn, this ) ) );
}

void FFmpegMovieWriter::waitForStartup()
{
	// Starting ffmpeg is quick (nothing in ffmpegThreadFn waits for ffmpeg itself);
	// waiting for it keeps the first frames from being dropped
	while ( ! mStartupDone )
	{
		std::this_thread::yield();
	}
}

void FFmpegMovieWriter::fail()
{
	mFailed = true;
	if ( mVideoFrames )
	{
		mVideoFrames->cancel();
	}
	if ( mAudioFrames )
	{
		mAudioFrames->cancel();
	}
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
	if ( ! mFormat.mExtraOutputArgs.empty() )
	{
		outputSettings << " " << mFormat.mExtraOutputArgs;
	}
	outputSettings << " \"" << mPathMovie.string() << "\"";

#if defined( _WIN32 )
	const std::string pipePrefix = "\\\\.\\pipe\\ffmpegmoviewriter_" +
		std::to_string( ::GetCurrentProcessId() ) + "_" + std::to_string( sPipeId );
	if ( mFormat.mRecordVideo )
	{
		mPipeVideo = pipePrefix + "_video";
		mPipeHandleVideo = createPipe( mPipeVideo );
	}
	if ( mFormat.mRecordAudio )
	{
		mPipeAudio = pipePrefix + "_audio";
		mPipeHandleAudio = createPipe( mPipeAudio );
	}
	if ( ( mFormat.mRecordVideo && ! mPipeHandleVideo ) ||
		 ( mFormat.mRecordAudio && ! mPipeHandleAudio ) )
	{
		closePipe( &mPipeHandleVideo );
		closePipe( &mPipeHandleAudio );
		fail();
		mStartupDone = true;
		return;
	}
#else
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
#endif
	sPipeId++;

	std::stringstream cmd;
	cmd << "\"" << mFormat.mPathFFmpeg.string() << "\"" <<
		( mFormat.mVerbose ? "" : " -loglevel quiet" ) << " -y";
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
		if ( code >= 0 && code < (int)pixelFormats.size() )
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
	cmd << " " + outputSettings.str();
	std::string command = cmd.str();

#if defined( _WIN32 )
	STARTUPINFOA startupInfo;
	ZeroMemory( &startupInfo, sizeof( startupInfo ) );
	startupInfo.cb = sizeof( startupInfo );
	PROCESS_INFORMATION processInfo;
	ZeroMemory( &processInfo, sizeof( processInfo ) );

	std::vector< char > commandLine( command.begin(), command.end() );
	commandLine.push_back( 0 );
	if ( ! ::CreateProcessA( nullptr, commandLine.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo ) )
	{
		CI_LOG_E( "could not start ffmpeg (error " << ::GetLastError() << "): " << command );
		closePipe( &mPipeHandleVideo );
		closePipe( &mPipeHandleAudio );
		fail();
		mStartupDone = true;
		return;
	}
	::CloseHandle( processInfo.hThread );
	mFFmpegProcess = processInfo.hProcess;
	mThreadFFmpegInitialized = true;
	CI_LOG_I( "ffmpeg started (pid " << processInfo.dwProcessId << "): " << command );
#else
	const char *scriptArg = command.c_str();	// the child must not allocate between fork and exec

	pid_t pid = ::fork();
	if ( pid < 0 )
	{
		int serrno = errno;
		CI_LOG_E( "fork failed for ffmpeg: " << ::strerror( serrno ) << "." );
		// Without ffmpeg reading the fifos the writer threads would block forever
		// in open(), so don't start them
		fail();
		mStartupDone = true;
		return;
	}
	else
	if ( pid == 0 )
	{
		::execlp( "bash", "bash", "--login", "-c", scriptArg, (char *)NULL );
		::_exit( 127 );
	}
	else
	{
		mFFmpegPid = pid;
		mThreadFFmpegInitialized = true;
		CI_LOG_I( "ffmpeg started (pid " << mFFmpegPid << "): " << command );
	}
#endif

	if ( mFormat.mRecordAudio )
	{
		setupAudioThread();
	}
	if ( mFormat.mRecordVideo )
	{
		setupVideoThread();
	}
	mStartupDone = true;
}

void FFmpegMovieWriter::cleanupFFmpeg()
{
	// The pipe write-ends were closed by cleanupVideoThread()/cleanupAudioThread()
	// in the destructor, so ffmpeg has received EOF; wait for it to finish writing
	// the moov atom and exit before the file is treated as complete.
#if defined( _WIN32 )
	closePipe( &mPipeHandleVideo );
	closePipe( &mPipeHandleAudio );
	if ( mFFmpegProcess )
	{
		::WaitForSingleObject( (HANDLE)mFFmpegProcess, INFINITE );
		DWORD exitCode = 0;
		if ( ::GetExitCodeProcess( (HANDLE)mFFmpegProcess, &exitCode ) && exitCode != 0 )
		{
			CI_LOG_E( "ffmpeg exited with status " << exitCode
					<< ", the recording is probably incomplete." );
		}
		::CloseHandle( (HANDLE)mFFmpegProcess );
		mFFmpegProcess = nullptr;
	}
#else
	if ( mFFmpegPid > 0 )
	{
		int status = 0;
		pid_t r;
		do
		{
			r = ::waitpid( mFFmpegPid, &status, 0 );
		}
		while ( r < 0 && errno == EINTR );
		if ( r == mFFmpegPid )
		{
			if ( WIFEXITED( status ) && ( WEXITSTATUS( status ) != 0 ) )
			{
				CI_LOG_E( "ffmpeg exited with status " << WEXITSTATUS( status )
									<< ", the recording is probably incomplete." );
			}
			else
			if ( WIFSIGNALED( status ) )
			{
				CI_LOG_E( "ffmpeg was killed by signal " << WTERMSIG( status )
									<< ", the recording is probably incomplete." );
			}
		}
		mFFmpegPid = -1;
	}

	if ( mFormat.mRecordVideo )
	{
		fs::remove( mPipeVideo );
	}
	if ( mFormat.mRecordAudio )
	{
		fs::remove( mPipeAudio );
	}
#endif
}

#if defined( _WIN32 )
void *FFmpegMovieWriter::createPipe( const fs::path &name )
{
	const DWORD kBufferSize = 4 * 1024 * 1024;
	HANDLE pipe = ::CreateNamedPipeA( name.string().c_str(),
			PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE | PIPE_WAIT, 1, kBufferSize, kBufferSize, 0, nullptr );
	if ( pipe == INVALID_HANDLE_VALUE )
	{
		CI_LOG_E( "could not create pipe " << name << " (error " << ::GetLastError() << ")" );
		return nullptr;
	}
	return pipe;
}

bool FFmpegMovieWriter::connectPipe( void *pipe )
{
	OVERLAPPED overlapped;
	ZeroMemory( &overlapped, sizeof( overlapped ) );
	overlapped.hEvent = ::CreateEventA( nullptr, TRUE, FALSE, nullptr );

	bool connected = false;
	if ( ::ConnectNamedPipe( (HANDLE)pipe, &overlapped ) )
	{
		connected = true;
	}
	else
	{
		const DWORD error = ::GetLastError();
		if ( error == ERROR_PIPE_CONNECTED )
		{
			connected = true;
		}
		else
		if ( error == ERROR_IO_PENDING )
		{
			// Wait for ffmpeg to open the pipe, or to exit without opening it
			HANDLE handles[ 2 ] = { overlapped.hEvent, (HANDLE)mFFmpegProcess };
			const DWORD result = ::WaitForMultipleObjects( 2, handles, FALSE, INFINITE );
			if ( result == WAIT_OBJECT_0 )
			{
				DWORD transferred = 0;
				connected = ::GetOverlappedResult( (HANDLE)pipe, &overlapped, &transferred, FALSE ) != 0;
			}
			else
			{
				::CancelIo( (HANDLE)pipe );
				CI_LOG_E( "ffmpeg exited before opening its input pipe" );
			}
		}
	}

	::CloseHandle( overlapped.hEvent );
	return connected;
}

bool FFmpegMovieWriter::writeToPipe( void *pipe, const uint8_t *data, size_t size )
{
	OVERLAPPED overlapped;
	ZeroMemory( &overlapped, sizeof( overlapped ) );
	overlapped.hEvent = ::CreateEventA( nullptr, TRUE, FALSE, nullptr );

	bool ok = true;
	while ( size > 0 )
	{
		const DWORD chunk = (DWORD)std::min< size_t >( size, 1 << 20 );
		DWORD written = 0;
		::ResetEvent( overlapped.hEvent );
		if ( ! ::WriteFile( (HANDLE)pipe, data, chunk, nullptr, &overlapped ) )
		{
			if ( ::GetLastError() != ERROR_IO_PENDING )
			{
				CI_LOG_E( "write to pipe failed (error " << ::GetLastError() << ")" );
				ok = false;
				break;
			}
			// The pipe buffer is full; wait until ffmpeg reads, or exits
			HANDLE handles[ 2 ] = { overlapped.hEvent, (HANDLE)mFFmpegProcess };
			if ( ::WaitForMultipleObjects( 2, handles, FALSE, INFINITE ) != WAIT_OBJECT_0 )
			{
				::CancelIo( (HANDLE)pipe );
				CI_LOG_E( "ffmpeg exited while writing to its input pipe" );
				ok = false;
				break;
			}
		}
		if ( ! ::GetOverlappedResult( (HANDLE)pipe, &overlapped, &written, TRUE ) )
		{
			CI_LOG_E( "write to pipe failed (error " << ::GetLastError() << ")" );
			ok = false;
			break;
		}
		data += written;
		size -= written;
	}

	::CloseHandle( overlapped.hEvent );
	return ok;
}

void FFmpegMovieWriter::closePipe( void **pipe )
{
	if ( *pipe )
	{
		// Waits until ffmpeg has read everything; closing then signals EOF
		::FlushFileBuffers( (HANDLE)*pipe );
		::CloseHandle( (HANDLE)*pipe );
		*pipe = nullptr;
	}
}
#endif

void FFmpegMovieWriter::setupVideoThread()
{
	mVideoThreadShouldQuit = false;
	mVideoFrames = new ConcurrentCircularBuffer< Surface8uRef >( 10 );
	mThreadVideo = std::shared_ptr< std::thread >( new std::thread(
				std::bind( &FFmpegMovieWriter::videoThreadFn, this ) ) );
}

void FFmpegMovieWriter::cleanupVideoThread()
{
	// The thread and its buffer are created at the end of ffmpegThreadFn, so both are
	// still null if the writer is destroyed right after being created
	if ( mThreadVideo )
	{
		// Queue a null frame as an end-of-stream marker: the video thread drains every
		// frame still buffered before it hits the marker and exits, so the tail of a
		// recording is not discarded
		if ( mVideoFrames )
		{
			mVideoFrames->pushFront( Surface8uRef() );
		}
		mThreadVideo->join();
		mThreadVideo.reset();
	}

	if ( mVideoFrames )
	{
		mVideoFrames->cancel();
		delete mVideoFrames;
		mVideoFrames = nullptr;
	}
}

void FFmpegMovieWriter::videoThreadFn()
{
	ThreadSetup threadSetup;

#if defined( _WIN32 )
	if ( ! connectPipe( mPipeHandleVideo ) )
	{
		fail();
		return;
	}
#else
	int fd = ::open( mPipeVideo.string().c_str(), O_WRONLY );
#endif

	Surface8uRef frame;

	while ( ! mVideoThreadShouldQuit && ! mFailed )
	{
		if ( mVideoFrames->isNotEmpty() )
		{
			mVideoFrames->popBack( &frame );
			if ( ! frame )
			{
				break;
			}

			const size_t size = (size_t)frame->getWidth() * frame->getHeight() *
				frame->getPixelBytes();
#if defined( _WIN32 )
			if ( ! writeToPipe( mPipeHandleVideo, frame->getData(), size ) )
			{
				fail();
			}
#else
			size_t offset = 0;
			size_t remaining = size;

			while ( remaining > 0 )
			{
				ssize_t written = ::write( fd, (char *)frame->getData() +
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
					fail();
					break;
				}

				if ( mVideoThreadShouldQuit )
				{
					break;
				}
			}
#endif

			frame.reset();
		}
		else
		{
			ci::sleep( 1 );
		}
	}

#if defined( _WIN32 )
	closePipe( &mPipeHandleVideo );
#else
	::close( fd );
#endif
}

void FFmpegMovieWriter::addFrame( SurfaceRef surface )
{
	waitForStartup();
	if ( mFailed || ! mThreadFFmpegInitialized || ! mVideoFrames )
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
	// The thread and its buffer are created at the end of ffmpegThreadFn, so both are
	// still null if the writer is destroyed right after being created
	if ( mThreadAudio )
	{
		// Queue a null frame as an end-of-stream marker: the audio thread drains every
		// frame still buffered before it hits the marker and exits, so the tail of a
		// recording is not discarded and its heap allocations are not leaked
		if ( mAudioFrames )
		{
			mAudioFrames->pushFront( nullptr );
		}
		mThreadAudio->join();
		mThreadAudio.reset();
	}

	if ( mAudioFrames )
	{
		// Free the frames left behind when the thread stopped early after a failure
		AudioFrame *frame = nullptr;
		while ( mAudioFrames->tryPopBack( &frame ) )
		{
			if ( frame )
			{
				delete [] frame->mData;
				delete frame;
			}
		}
		mAudioFrames->cancel();
		delete mAudioFrames;
		mAudioFrames = nullptr;
	}
}

void FFmpegMovieWriter::audioThreadFn()
{
	ThreadSetup threadSetup;

#if defined( _WIN32 )
	if ( ! connectPipe( mPipeHandleAudio ) )
	{
		fail();
		return;
	}
#else
	int fd = ::open( mPipeAudio.string().c_str(), O_WRONLY );
#endif

	while ( ! mAudioThreadShouldQuit && ! mFailed )
	{
		AudioFrame *frame = nullptr;

		if ( mAudioFrames->isNotEmpty() )
		{
			mAudioFrames->popBack( &frame );
			if ( frame == nullptr )
			{
				break;
			}

			const size_t size = frame->mSize * sizeof( float );
#if defined( _WIN32 )
			if ( ! writeToPipe( mPipeHandleAudio, (const uint8_t *)frame->mData, size ) )
			{
				fail();
			}
#else
			size_t offset = 0;
			size_t remaining = size;

			while ( remaining > 0 )
			{
				ssize_t written = ::write( fd, (uint8_t *)frame->mData + offset,
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
					fail();
					break;
				}

				if ( mAudioThreadShouldQuit )
				{
					break;
				}
			}
#endif

			delete [] frame->mData;
			delete frame;
			frame = nullptr;
		}
		else
		{
			ci::sleep( 1 );
		}
	}

#if defined( _WIN32 )
	closePipe( &mPipeHandleAudio );
#else
	::close( fd );
#endif
}

void FFmpegMovieWriter::addAudioBuffer( const audio::Buffer *buffer )
{
	waitForStartup();
	if ( mFailed || ! mThreadFFmpegInitialized || ! mAudioFrames )
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
