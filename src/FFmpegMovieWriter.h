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

#pragma once

#if ! defined( _WIN32 )
#include <unistd.h>
#endif

#include <atomic>
#include <memory>
#include <string>

#include "cinder/ConcurrentCircularBuffer.h"
#include "cinder/Filesystem.h"
#include "cinder/Surface.h"
#include "cinder/Thread.h"
#include "cinder/audio/Buffer.h"

namespace mndl {

typedef std::shared_ptr< class FFmpegMovieWriter > FFmpegMovieWriterRef;

class FFmpegMovieWriter
{
 public:
	class Format
	{
	 public:
		Format();
		Format( const Format &format );

		const Format & operator=( const Format &format );

		Format & recordVideo( bool recordVideo = true ) { mRecordVideo = recordVideo; return *this; }
		bool getRecordVideo() const { return mRecordVideo; }
		void setRecordVideo( bool recordVideo = true ) { mRecordVideo = recordVideo; }

		Format & recordAudio( bool recordAudio = true ) { mRecordAudio = recordAudio; return *this; }
		bool getRecordAudio() const { return mRecordAudio; }
		void setRecordAudio( bool recordAudio = true ) { mRecordAudio = recordAudio; }

		Format & frameRate( float frameRate ) { mFrameRate = frameRate; return *this; }
		float getFrameRate() const { return mFrameRate; }
		void setFrameRate( float frameRate ) { mFrameRate = frameRate; }

		Format & bitRateVideo( const std::string &bitRate ) { mBitRateVideo = bitRate; return *this; }
		std::string getBitRateVideo() const { return mBitRateVideo; }
		void setBitRateVideo( const std::string &bitRate ) { mBitRateVideo = bitRate; }

		Format & bitRateAudio( const std::string &bitRate ) { mBitRateAudio = bitRate; return *this; }
		std::string getBitRateAudio() const { return mBitRateAudio; }
		void setBitRateAudio( const std::string &bitRate ) { mBitRateAudio = bitRate; }

		Format & audioSampleRate( size_t sampleRate ) { mAudioSampleRate = sampleRate; return *this; }
		size_t getAudioSampleRate() const { return mAudioSampleRate; }
		void setAudioSampleRate( size_t audioSampleRate ) { mAudioSampleRate = audioSampleRate; }

		Format & numAudioInputChannels( size_t numInputChannels ) { mNumAudioInputChannels = numInputChannels; return *this; }
		size_t getNumAudioInputChannels() const { return mNumAudioInputChannels; }
		void setNumAudioInputChannels( size_t numInputChannels ) { mNumAudioInputChannels = numInputChannels; }

		Format & videoChannelOrder( const ci::SurfaceChannelOrder &channelOrder ) { mVideoChannelOrder = channelOrder; return *this; }
		ci::SurfaceChannelOrder getVideoChannelOrder() const { return mVideoChannelOrder; }
		void setVideoChannelOrder( const ci::SurfaceChannelOrder &channelOrder ) { mVideoChannelOrder = channelOrder; }

		Format & codecVideo( const std::string &codec ) { mCodecVideo = codec; return *this; }
		std::string getCodecVideo() const { return mCodecVideo; }
		void setCodecVideo( const std::string &codec ) { mCodecVideo = codec; }

		Format & extraOutputArgs( const std::string &args ) { mExtraOutputArgs = args; return *this; }
		std::string getExtraOutputArgs() const { return mExtraOutputArgs; }

		//! Path of the ffmpeg executable. Default: "ffmpeg", searched on the PATH.
		Format & pathFFmpeg( const ci::fs::path &path ) { mPathFFmpeg = path; return *this; }
		ci::fs::path getPathFFmpeg() const { return mPathFFmpeg; }

		//! Lets ffmpeg print its log. Default: false.
		Format & verbose( bool verbose = true ) { mVerbose = verbose; return *this; }
		bool getVerbose() const { return mVerbose; }

	private:
		ci::fs::path mPathFFmpeg = "ffmpeg";
		std::string mCodecVideo = "mpeg4";
		std::string mCodecAudio = "aac";
		std::string mBitRateVideo = "2000k";
		std::string mBitRateAudio = "128k";
		float mFrameRate = 30.0f;

		size_t mAudioSampleRate = 44100;
		size_t mNumAudioInputChannels = 2;

		ci::SurfaceChannelOrder mVideoChannelOrder = ci::SurfaceChannelOrder( ci::SurfaceChannelOrder::RGB );

		std::string mExtraOutputArgs;

		bool mRecordVideo = true;
		bool mRecordAudio = false;
		bool mVerbose = false;

		friend class FFmpegMovieWriter;
	};

	static FFmpegMovieWriterRef create( const ci::fs::path &path,
			int32_t width, int32_t height, const Format &format )
	{ return FFmpegMovieWriterRef( new FFmpegMovieWriter( path,
				width, height, format ) ); }

	~FFmpegMovieWriter();

	void addFrame( ci::Surface8uRef surface );
	void addAudioBuffer( const ci::audio::Buffer *buffer );

	//! True if ffmpeg could not be started, exited early or a write to it failed.
	//! Frames added after a failure are dropped.
	bool hasFailed() const { return mFailed; }

 protected:
	FFmpegMovieWriter( const ci::fs::path &path, int32_t width, int32_t height,
			const Format &format );

	const Format mFormat;

#if defined( _WIN32 )
	void *mFFmpegProcess = nullptr; // HANDLE
	void *mPipeHandleVideo = nullptr; // HANDLE
	void *mPipeHandleAudio = nullptr; // HANDLE
	void *createPipe( const ci::fs::path &name );
	//! Waits until ffmpeg opens the pipe. False if ffmpeg exits first.
	bool connectPipe( void *pipe );
	//! Writes all of data. False if the pipe broke or ffmpeg exited.
	bool writeToPipe( void *pipe, const uint8_t *data, size_t size );
	void closePipe( void **pipe );
#else
	pid_t mFFmpegPid = -1;
#endif
	//! Marks the writer failed and unblocks producers waiting on the frame buffers.
	void fail();
	std::atomic< bool > mFailed { false };
	//! Set when ffmpegThreadFn has finished starting ffmpeg (or failed to).
	std::atomic< bool > mStartupDone { false };
	void waitForStartup();

	void setupFFmpeg();
	void cleanupFFmpeg();
	void ffmpegThreadFn();
	std::shared_ptr< std::thread > mThreadFFmpeg;
	std::atomic< bool > mThreadFFmpegInitialized;

	ci::fs::path mPathMovie;
	int32_t mMovieWidth;
	int32_t mMovieHeight;

	static int32_t sPipeId;

	ci::fs::path mPipeVideo;

	void setupVideoThread();
	void cleanupVideoThread();
	void videoThreadFn();
	std::shared_ptr< std::thread > mThreadVideo;
	bool mVideoThreadShouldQuit;

	ci::ConcurrentCircularBuffer< ci::Surface8uRef > *mVideoFrames = nullptr;

	ci::fs::path mPipeAudio;

	void setupAudioThread();
	void cleanupAudioThread();
	void audioThreadFn();
	std::shared_ptr< std::thread > mThreadAudio;
	bool mAudioThreadShouldQuit;

	struct AudioFrame
	{
		float *mData;
		size_t mSize;
	};

	ci::ConcurrentCircularBuffer< AudioFrame * > *mAudioFrames = nullptr;
	//ci::ConcurrentCircularBuffer< ci::audio::BufferInterleaved * > *mAudioFrames = nullptr;

	size_t mNumAudioSamplesRecorded = 0;
	size_t mNumVideoFramesRecorded = 0;
};

}
