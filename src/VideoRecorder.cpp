#include <stdexcept>
#include <cassert>
#include <vector>

#include "gpc/VideoRecorder.hpp"

using std::runtime_error;

static const uint8_t endcode[] = { 0, 0, 1, 0xb7 };

namespace gpc {

	Recorder::Recorder()
		: codec(nullptr), cctx(nullptr), file(nullptr), frame(nullptr), frame_num(-1), sws_ctx(nullptr), got_output(0), framerate({ 1, 50 })
	{
		// TODO: is it ok to call av_register_xxx() multiple times ?
		av_register_all(); // TODO: av_register_output_format() instead ?

	}

	auto Recorder::setFrameRate(unsigned num, unsigned den) -> Recorder &
	{
		framerate.num = num, framerate.den = den;
		return *this;
	}

	void Recorder::openFile(const std::string &filename, unsigned width_, unsigned rows_)
	{
		if (width_ != 0) _width = width_;
		if (rows_ != 0) _rows = rows_;

		assert(_width != 0 && _rows != 0);

		// TODO: offer choices
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (!codec) throw runtime_error("Unabled to find H264 encoder");

		cctx = avcodec_alloc_context3(codec);
		if (!cctx) throw runtime_error("Unabled to allocate codec context");

		// The following bit rate settings are intended to allow the codec to do anything it wants
		cctx->bit_rate = 8 * 3 * _width * _rows / 2;
		cctx->bit_rate_tolerance = cctx->bit_rate;

		cctx->width = _width;
		cctx->height = _rows;
		cctx->time_base = framerate;
		cctx->gop_size = 10;
		cctx->max_b_frames = 1;
		cctx->pix_fmt = AV_PIX_FMT_YUV420P; // TODO: offer choices

		av_opt_set(cctx->priv_data, "preset", "slow", 0); // specific to H264

		if (avcodec_open2(cctx, codec, nullptr) < 0) throw runtime_error("Unable to open codec");

		file = fopen(filename.c_str(), "wb");
		if (!file) throw runtime_error("Unable to open output video file");

		frame = av_frame_alloc();
		if (!frame) throw runtime_error("Failed to allocate AV frame");
		frame->format = cctx->pix_fmt;
		frame->width = cctx->width;
		frame->height = cctx->height;

		int ret = av_image_alloc(frame->data, frame->linesize, cctx->width, cctx->height, cctx->pix_fmt, 32);
		if (ret < 0) throw runtime_error("Failed to allocate raw picture buffer");

		frame_num = 0;

		got_output = 0;

		sws_ctx = sws_getContext(_width, _rows, AV_PIX_FMT_RGB24, _width, _rows, cctx->pix_fmt, 0, 0, 0, 0);
	}

	void Recorder::recordFrameFromRGB(const void *pixels_, bool flip_y)
	{
		std::vector<RGBValue> swap(_width);

		// Initialize video stream packet
		av_init_packet(&pkt);
		pkt.data = nullptr;    // packet data will be allocated by the encoder
		pkt.size = 0;

		RGBValue *pixels = const_cast<RGBValue*>(reinterpret_cast<const RGBValue*>(pixels_));

		if (flip_y) {
			for (unsigned y = 0; y < _rows / 2; y++) {
				// Copy "swap" row (from first half)
				memcpy(&swap[0], &pixels[y * _width], _width * sizeof(RGBValue));
				// Copy row in second half to first half
				memcpy(&pixels[y * _width], &pixels[(_rows - y - 1) * _width], _width * sizeof(RGBValue));
				// Copy "swap" row to second half
				memcpy(&pixels[(_rows - y - 1) * _width], &swap[0], _width * sizeof(RGBValue));
			}
		}

		// Convert pixels to video frame
		const uint8_t * inData[1] = { reinterpret_cast<const uint8_t*>(pixels) }; // RGB24 have one plane
		int inLinesize[1] = { 3 * _width }; // RGB stride
		if (sws_scale(sws_ctx, inData, inLinesize, 0, _rows, frame->data, frame->linesize) != _rows)
			throw runtime_error("Software scaling returns incorrect slice height");

		// Example image
		if (false) {
			// Y
			for (int y = 0; y < cctx->height; y++)
				for (int x = 0; x < cctx->width; x++)
					frame->data[0][y * frame->linesize[0] + x] = x + y + frame_num * 3;
			// Cb and Cr
			for (int y = 0; y < cctx->height / 2; y++) {
				for (int x = 0; x < cctx->width / 2; x++) {
					frame->data[1][y * frame->linesize[1] + x] = 128 + y + frame_num * 2;
					frame->data[2][y * frame->linesize[2] + x] = 64 + x + frame_num * 5;
				}
			}
		}

		frame->pts = frame_num;

		// Encode the frame
		int ret = avcodec_encode_video2(cctx, &pkt, frame, &got_output);
		if (ret < 0) throw runtime_error("Failed to encode the frame");
		if (got_output) {
			fwrite(pkt.data, 1, pkt.size, file);
			av_free_packet(&pkt);
		}

		// Advance frame counter
		frame_num++;
	}

	void Recorder::closeFile()
	{
		// Get the delayed frames
		for (got_output = 1; got_output; frame_num++) {
			int ret = avcodec_encode_video2(cctx, &pkt, nullptr, &got_output);
			if (ret < 0) throw runtime_error("Error encoding frame");
			if (got_output) {
				fwrite(pkt.data, 1, pkt.size, file);
				av_free_packet(&pkt);
			}
		}

		/* add sequence end code to have a real mpeg file */
		//fwrite(endcode, 1, sizeof(endcode), file); // VideoLAN doesn't complain when this is absent
		fclose(file);
		sws_freeContext(sws_ctx);
		avcodec_close(cctx);
		av_free(cctx);
		av_freep(&frame->data[0]);
		av_frame_free(&frame);
	}

} // ns gpc