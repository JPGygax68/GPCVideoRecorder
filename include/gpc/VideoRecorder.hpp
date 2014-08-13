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
		typedef uint8_t RGBValue[3];
		typedef AVRational FrameRate;

		explicit Recorder();
		~Recorder();

		auto setFrameRate(unsigned num, unsigned denom)->Recorder &;

		void openFile(const std::string &filename, unsigned width = 0, unsigned height = 0);

		void closeFile();

		auto currentFrameNum() const -> int { return frame_num; }

		void recordFrameFromRGB(const void *pixels, bool flip_y = true);

        auto width() const -> unsigned { return _width; }
        auto rows() const -> unsigned { return _rows; }

	private:

		FrameRate framerate;
		unsigned _width, _rows;
		AVCodec *codec;
		AVCodecContext *cctx;
		FILE *file;
		AVFrame *frame;
		int frame_num;
		SwsContext *sws_ctx;
		AVPacket pkt;
		int got_output;
		char errbuf[1024];
	};

} // ns gpc