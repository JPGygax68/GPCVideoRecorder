#include <stdexcept>
#include <cassert>
#include <vector>
#include <iostream> // TODO: remove!
#include <chrono>

#include "gpc/VideoRecorder.hpp"

using std::runtime_error;

static const uint8_t endcode[] = { 0, 0, 1, 0xb7 };

namespace gpc {

    Recorder::Recorder() : 
        framerate({ 1, 25 }),
        codec(nullptr), fctx(nullptr), cctx(nullptr), avio_ctx(nullptr), sws_ctx(nullptr), channel_open(false),
        /*file(nullptr),*/ frame(nullptr), frame_num(-1), got_output(0)
	{
		// TODO: is it ok to call av_register_xxx() multiple times ?
		av_register_all(); // TODO: av_register_output_format() instead ?
	}

	Recorder::~Recorder()
	{
		//if (file) close();
	}

	auto Recorder::setFrameRate(const FrameRate &fr) -> Recorder &
	{
		framerate = fr;
		return *this;
	}

	void Recorder::open(const std::string &filename, unsigned width_, unsigned rows_, SourceFormat src_fmt_)
	{
        const char * CODEC_NAME = "libx264"; // "nvenc"; // "libx264"
		using std::string;
        int err = 0;

		if (width_ != 0) _width = width_;
		if (rows_ != 0) _rows = rows_;

		assert(_width != 0 && _rows != 0);

		// TODO: offer choices
		//codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        codec = avcodec_find_encoder_by_name(CODEC_NAME);
        if (!codec) throw runtime_error("Unabled to find H264 encoder");

        cctx = avcodec_alloc_context3(codec);
		if (!cctx) throw runtime_error("Unabled to allocate codec context");

        if ((err = avcodec_get_context_defaults3(cctx, codec)) < 0)
            // throw runtime_error(string("Failed to get codec context defaults:") + av_make_error_string(errbuf, sizeof(errbuf), err));
            throw runtime_error(string("Failed to get codec context defaults: ") + std::to_string(err));

        // The following bit rate settings are intended to allow the codec to do anything it wants
		//cctx->bit_rate = 8 * 3 * _width * _rows * framerate.den / framerate.num / 4;
		//cctx->bit_rate_tolerance = cctx->bit_rate / 2;
        cctx->width = _width;
		cctx->height = _rows;
		cctx->codec_type = AVMEDIA_TYPE_VIDEO;
        //cctx->codec_id = codec->id;
        cctx->pix_fmt = AV_PIX_FMT_YUV420P; // codec->pix_fmts[0]; //AV_PIX_FMT_BGRA; //TODO: offer choices
        cctx->time_base = framerate;
		//cctx->gop_size = 0xffffffff; // 0; 10;
		//cctx->max_b_frames = 1;

        if (strcmp(CODEC_NAME, "libx264") == 0) {
            // https://trac.ffmpeg.org/wiki/Encode/H.264
            //av_opt_set(cctx->priv_data, "preset", "slow", 0); // specific to libx264
            av_opt_set(cctx->priv_data, "preset", "ultrafast", 0); // reduces CPU load by half!
            av_opt_set(cctx->priv_data, "tune", "zerolatency", 0); // TODO: sometimes close to 0, sometimes not - find out why
        }
        else if (strcmp(CODEC_NAME, "nvenc") == 0) {
            //av_opt_set(cctx->priv_data, "preset", "fast", 0);
            av_opt_set(cctx->priv_data, "tune", "zerolatency", 0);
        }

		if ((err = avcodec_open2(cctx, codec, nullptr)) < 0)
            throw runtime_error(string("Unable to open codec:") + av_make_error_string(errbuf, sizeof(errbuf), err));
            //throw runtime_error(string("Unable to open codec: ") + std::to_string(err));

        if ((err = avio_open(&avio_ctx, filename.c_str(), AVIO_FLAG_WRITE)))
            throw runtime_error(string("Failed to open output stream: ") + av_make_error_string(errbuf, sizeof(errbuf), err));
            //throw runtime_error(string("Failed to open output stream: ") + std::to_string(err));

		frame = av_frame_alloc();
		if (!frame) throw runtime_error("Failed to allocate AV frame");
		frame->format = cctx->pix_fmt;
		frame->width = cctx->width;
		frame->height = cctx->height;

		if ((err = av_image_alloc(frame->data, frame->linesize, cctx->width, cctx->height, cctx->pix_fmt, 1)) < 0)
		    //throw runtime_error(string("Failed to allocate raw picture buffer: ") + av_make_error_string(errbuf, sizeof(errbuf), err));
            throw runtime_error(string("Failed to allocate raw picture buffer: ") + std::to_string(err));

		got_output = 0;

        switch (src_fmt_) {
        case RGB: src_fmt = AV_PIX_FMT_RGB24; break;
        case BGRA: src_fmt = AV_PIX_FMT_BGR32; break;
        default: throw std::runtime_error("unsupported pixel format");
        }
		sws_ctx = sws_getContext(_width, _rows, src_fmt, _width, _rows, cctx->pix_fmt, 0, 0, 0, 0);

        channel_open = true;
    
        last_ts = av_gettime_relative();
        frame_num = 0;
    }

	void Recorder::recordFrameFromRGB(const void *pixels_, bool flip_y) // , int64_t timestamp, bool flip_y)
	{
		using std::string;

		std::vector<RGBValue> swap(_width);

		// Initialize video stream packet
		av_init_packet(&pkt);
		pkt.data = nullptr;    // packet data will be allocated by the encoder
		pkt.size = 0;
        pkt.pts = pkt.dts = 0; // the timestamp does not appear to be necessary when streaming

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

		// frame->pts = timestamp; // frame_num;

		// Encode the frame
		int ret = avcodec_encode_video2(cctx, &pkt, frame, &got_output);
		if (ret < 0) {
			// throw runtime_error(string("Failed to encode the frame: ") + av_make_error_string(errbuf, sizeof(errbuf), ret));
		}
		else if (got_output) {
            avio_write(avio_ctx, pkt.data, pkt.size);
			av_free_packet(&pkt);
		}

		// Advance frame counter
		frame_num++;
	}

    // PROFILING
    static std::chrono::high_resolution_clock cpu_timer;
    static double   total_flipy_time = 0;
    static uint64_t flipy_count = 0;
    static double   total_swscale_time = 0;
    static uint64_t swscale_count = 0;
    static double   total_encode_time = 0;
    static uint64_t encode_count = 0;
    static double   total_write_time = 0;
    static uint64_t write_count = 0;

    void Recorder::recordFrameFromBGRA(const void *pixels_, bool flip_y) // , int64_t timestamp, bool flip_y)
    {
        using std::string;
        decltype( cpu_timer.now() ) ts_start;

        // Get current timestamp; if less than a full frame period has elapsed since
        // the last call to this method, don't encode/send the frame
        int64_t current_ts = av_gettime_relative();
        //if (current_ts < (last_ts + framerate.num * 1000000 / framerate.den)) return;

        std::vector<RGBAValue> swap(_width);

        // Initialize video stream packet
        av_init_packet(&pkt);
        pkt.data = nullptr;    // packet data will be allocated by the encoder
        pkt.size = 0;

        RGBAValue *pixels = const_cast<RGBAValue*>(reinterpret_cast<const RGBAValue*>(pixels_));

        if (flip_y) {

            ts_start = cpu_timer.now();
            for (unsigned y = 0; y < _rows / 2; y++) {
                // Copy "swap" row (from first half)
                memcpy(&swap[0], &pixels[y * _width], _width * sizeof(RGBAValue));
                // Copy row in second half to first half
                memcpy(&pixels[y * _width], &pixels[(_rows - y - 1) * _width], _width * sizeof(RGBAValue));
                // Copy "swap" row to second half
                memcpy(&pixels[(_rows - y - 1) * _width], &swap[0], _width * sizeof(RGBAValue));
            }
            total_flipy_time += std::chrono::duration_cast<std::chrono::nanoseconds>(cpu_timer.now() - ts_start).count();
            flipy_count++;
        }

        // Convert pixels to video frame
        const uint8_t * inData[1] = { reinterpret_cast<const uint8_t*>(pixels) }; // RGB24 have one plane
        int inLinesize[1] = { 4 * _width }; // RGBA stride
        ts_start = cpu_timer.now();
        if (sws_scale(sws_ctx, inData, inLinesize, 0, _rows, frame->data, frame->linesize) != _rows)
            throw runtime_error("Software scaling returns incorrect slice height");
        total_swscale_time += std::chrono::duration_cast<std::chrono::nanoseconds>(cpu_timer.now() - ts_start).count();
        swscale_count++;

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

        frame->pts = frame_num; // * 90000 / 25;

        // Encode the frame
        ts_start = cpu_timer.now();
        int ret = avcodec_encode_video2(cctx, &pkt, frame, &got_output);
        total_encode_time += std::chrono::duration_cast<std::chrono::nanoseconds>(cpu_timer.now() - ts_start).count();
        encode_count++;
        if (ret < 0) {
            // throw runtime_error(string("Failed to encode the frame: ") + av_make_error_string(errbuf, sizeof(errbuf), ret));
        }
        else if (got_output) {
            ts_start = cpu_timer.now();
            avio_write(avio_ctx, pkt.data, pkt.size);
            total_write_time += std::chrono::duration_cast<std::chrono::nanoseconds>(cpu_timer.now() - ts_start).count();
            write_count++;
            av_free_packet(&pkt);
        }

        // Advance frame counter and record timestamp
        frame_num++;
        last_ts = current_ts;
    }

    void Recorder::close()
	{
		// Get the delayed frames
		for (; got_output; frame_num++) {
			int ret = avcodec_encode_video2(cctx, &pkt, nullptr, &got_output);
			if (ret < 0) {
				//throw runtime_error("Error encoding frame");
			}
			else if (got_output) {
                avio_write(avio_ctx, pkt.data, pkt.size);
                av_free_packet(&pkt);
			}
		}

		/* add sequence end code to have a real mpeg file */
		avio_write(avio_ctx, endcode, sizeof(endcode)); // VideoLAN doesn't complain when this is absent
        avio_closep(&avio_ctx);
        sws_freeContext(sws_ctx);
        avcodec_close(cctx);
        avcodec_free_context(&cctx); // THIS SOMETIMES CRASHES
        av_freep(&frame->data[0]);
		av_frame_free(&frame);
        channel_open = false;

        // PROFILING
        std::cout << "Average flip Y duration [ns] : " << total_flipy_time   / flipy_count   << std::endl;
        std::cout << "Average swscale duration [ns]: " << total_swscale_time / swscale_count << std::endl;
        std::cout << "Average encode duration [ns] : " << total_encode_time  / encode_count  << std::endl;
        std::cout << "Average write duration [ns]  : " << total_write_time   / write_count   << std::endl;
    }

} // ns gpc