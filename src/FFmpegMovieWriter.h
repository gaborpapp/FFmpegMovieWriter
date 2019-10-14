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

#include <unistd.h>

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

 protected:
	FFmpegMovieWriter( const ci::fs::path &path, int32_t width, int32_t height,
			const Format &format );

	const Format mFormat;

	pid_t mFFmpegPid;

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
