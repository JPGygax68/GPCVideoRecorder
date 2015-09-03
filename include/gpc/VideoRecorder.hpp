#pragma once

#include <string>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace gpc {

	class Recorder {
	public:
        enum SourceFormat {
            RGB = 1,
            BGRA = 2
        };

		typedef uint8_t RGBValue [3];
        typedef uint8_t RGBAValue[4];
        typedef AVRational FrameRate;

		explicit Recorder();
		~Recorder();

		auto setFrameRate(const FrameRate &framerate) -> Recorder &;

		void open(const std::string &filename, unsigned width = 0, unsigned height = 0, SourceFormat src_fmt = RGB);

		void close();

        bool isOpen() const { return channel_open; }

		auto currentFrameNum() const -> int { return frame_num; }

        //void recordFrameFromRGB(const void *pixels, int64_t timestamp, bool flip_y = true);
        // TODO: create single method that automatically adapts to source format defined when calling open()
        void recordFrameFromRGB(const void *pixels, bool flip_y = true);
        void recordFrameFromBGRA(const void *pixels, bool flip_y = true);

        //void pause();
        //void resume();

        auto width () const -> unsigned { return cctx->width; }
        auto height() const -> unsigned { return cctx->height; }

	private:

        FrameRate framerate;
		unsigned _width, _rows;
		AVCodec *codec;
        AVFormatContext *fctx;
		AVCodecContext *cctx;
        bool channel_open;
		//FILE *file;
		AVFrame *frame;
		int frame_num;
        AVPixelFormat src_fmt;
		SwsContext *sws_ctx;
        AVIOContext *avio_ctx;
		AVPacket pkt;
		int got_output;

		char errbuf[1024];
	};

} // ns gpc