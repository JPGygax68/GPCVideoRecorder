#include <stdexcept>
#include <cassert>
#include <vector>
#include <cstdint> 

#include "gpc/VideoRecorder.hpp"

using std::runtime_error;

static const uint8_t endcode[] = { 0, 0, 1, 0xb7 };

namespace gpc {

	Recorder::Recorder() : 
#ifdef USE_LIBX264
        encoder(nullptr), nals(nullptr), num_nals(0),
#else
        codec(nullptr), cctx(nullptr), frame(nullptr),
#endif
        fp(nullptr), frame_num(-1), sws_ctx(nullptr), got_output(0), framerate({ 1, 50 })
	{
#ifdef USE_LIBX264
        memset((char*)&pic_raw, 0, sizeof(pic_raw));
#endif
        // TODO: is it ok to call av_register_xxx() multiple times ?
		av_register_all(); // TODO: av_register_output_format() instead ?
	}

	Recorder::~Recorder()
	{
		if (fp) closeFile();
	}

	auto Recorder::setFrameRate(unsigned num, unsigned den) -> Recorder &
	{
		framerate.num = num, framerate.den = den;
		return *this;
	}

	void Recorder::openFile(const std::string &filename, unsigned width_, unsigned rows_)
	{
		using std::string;

        int err = 0;
        char errbuf[256];

		if (width_ != 0) _width = width_;
		if (rows_ != 0) _rows = rows_;

		assert(_width != 0 && _rows != 0);

		fp = fopen(filename.c_str(), "wb");
		if (!fp) throw runtime_error("Unable to open output video fp");

#ifdef USE_LIBX264
        
		unsigned aligned_width  = 4 * ((_width + 3) / 4);
		unsigned aligned_height = 4 * ((_rows  + 3) / 4);
        x264_picture_alloc(&pic_in, X264_CSP_I420, aligned_width, aligned_height);
		pic_in.i_type = X264_TYPE_AUTO;
		pic_in.i_qpplus1 = 0; //frame qp adjustment

		//x264_param_default_preset(&params, "ultrafast", "zerolatency"); // TODO: provide a way to make this customizable
		x264_param_default_preset(&params, "medium", "film");
        params.i_threads = 8;
		params.i_width = aligned_width; // _width;
		params.i_height = aligned_height; // _rows;
        params.i_fps_num = framerate.num;
        params.i_fps_den = framerate.den;
		params.i_keyint_max = framerate.num;
		params.b_intra_refresh = 1;
		params.rc.i_rc_method = X264_RC_CRF;
		params.rc.f_rf_constant = 25;
		params.rc.f_rf_constant_max = 35;
		params.b_annexb = 1;
		params.b_repeat_headers = 1;
		params.i_log_level = X264_LOG_DEBUG;

		x264_param_apply_profile(&params, "baseline");

		// create the encoder using our params
        encoder = x264_encoder_open(&params);
        if (!encoder) throw std::runtime_error("Cannot open the encoder");

        // write headers
        int nheader = 0;
        int r = x264_encoder_headers(encoder, &nals, &nheader);
        if (r < 0) throw std::runtime_error("x264_encoder_headers() failed"); // TODO: report error code/string

#ifdef NOT_DEFINED
		// nals[2] seems to be uninitialized, leading to a random value for header_size
		int header_size = 0;
        for (int i = 0; i < nheader; i++) header_size += nals[i].i_payload;
        if (!fwrite(nals[0].p_payload, header_size, 1, fp)) throw std::runtime_error("Cannot write headers");
#endif

#else // USE_LIBX264

        // TODO: offer choices
        codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG); // AV_CODEC_ID_H264);
        if (!codec) throw runtime_error("Unabled to find encoder");

        cctx = avcodec_alloc_context3(codec);
        if (!cctx) throw runtime_error("Unabled to allocate codec context");
        memset(cctx, 0, sizeof(*cctx));

        // The following bit rate settings are intended to allow the codec to do anything it wants
        cctx->bit_rate = 8 * 3 * _width * _rows * framerate.den / framerate.num / 4;
        cctx->bit_rate_tolerance = cctx->bit_rate / 2;

        cctx->width = _width;
        cctx->height = _rows;
        cctx->codec_type = AVMEDIA_TYPE_VIDEO;
        cctx->time_base = framerate;
        cctx->gop_size = 0; // 10;
        cctx->max_b_frames = 1;
        cctx->bit_rate = 15000000;
        cctx->pix_fmt = AV_PIX_FMT_YUV422P; // AV_PIX_FMT_YUV420P; // TODO: offer choices

        av_opt_set(cctx->priv_data, "preset", "slow", 0); // specific to H264

        if ((err = avcodec_open2(cctx, codec, nullptr)) < 0)
            throw runtime_error(std::string("Unable to open codec ") + av_make_error_string(errbuf, sizeof(errbuf), err));

        frame = av_frame_alloc();
		if (!frame) throw runtime_error("Failed to allocate AV frame");
		frame->format = cctx->pix_fmt;
		frame->width = cctx->width;
		frame->height = cctx->height;

		int ret = av_image_alloc(frame->data, frame->linesize, cctx->width, cctx->height, cctx->pix_fmt, 1);
		if (ret < 0) throw runtime_error(string("Failed to allocate raw picture buffer: ") + av_make_error_string(errbuf, sizeof(errbuf), ret));

		got_output = 0;

#endif

		frame_num = 0;

        sws_ctx = sws_getContext(_width, _rows, AV_PIX_FMT_RGB24, _width, _rows, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
		if (!sws_ctx) throw std::runtime_error("failed to obtain software scaler context");
	}

	void Recorder::recordFrameFromRGB(const void *pixels_, bool flip_y)
	{
		using std::string;

		RGBValue *pixels = const_cast<RGBValue*>(reinterpret_cast<const RGBValue*>(pixels_));

		if (flip_y) {
            
            std::vector<RGBValue> swap(_width);

            for (unsigned y = 0; y < _rows / 2; y++) {
				// Copy "swap" row (from first half)
				memcpy(&swap[0], &pixels[y * _width], _width * sizeof(RGBValue));
				// Copy row in second half to first half
				memcpy(&pixels[y * _width], &pixels[(_rows - y - 1) * _width], _width * sizeof(RGBValue));
				// Copy "swap" row to second half
				memcpy(&pixels[(_rows - y - 1) * _width], &swap[0], _width * sizeof(RGBValue));
			}
		}

#ifdef USE_LIBX264

        // copy the pixels into our "raw input" container.
        int bytes_filled = avpicture_fill(&pic_raw, (uint8_t*)pixels, AV_PIX_FMT_RGB24, _width, _rows);
		if (!bytes_filled) throw std::runtime_error("Cannot fill the raw input buffer");

        // convert to I420 for x264
        int h = sws_scale(sws_ctx, pic_raw.data, pic_raw.linesize, 0, _rows, pic_in.img.plane, pic_in.img.i_stride);
		if (h != _rows) throw std::runtime_error("scale failed");

        // and encode and store into pic_out
        pic_in.i_pts = frame_num;

        int frame_size = x264_encoder_encode(encoder, &nals, &num_nals, &pic_in, &pic_out);

		if (frame_size < 0) 
			throw std::runtime_error("Failed to encode the frame");
        if (frame_size > 0) {
            if (!fwrite(nals[0].p_payload, frame_size, 1, fp)) 
				throw std::runtime_error("Error while trying to write nal");
        }

#else

        // Initialize video stream packet
        av_init_packet(&pkt);
        pkt.data = nullptr;    // packet data will be allocated by the encoder
        pkt.size = 0;

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
		if (ret < 0) {
			// throw runtime_error(string("Failed to encode the frame: ") + av_make_error_string(errbuf, sizeof(errbuf), ret));
		}
		else if (got_output) {
			fwrite(pkt.data, 1, pkt.size, fp);
			av_free_packet(&pkt);
		}

#endif

        // Advance frame counter
		frame_num++;
	}

	void Recorder::closeFile()
	{
#ifdef USE_LIBX264

        if(encoder) {
            x264_picture_clean(&pic_in);
            memset(&pic_in, 0, sizeof(pic_in));
            memset(&pic_out, 0, sizeof(pic_out));

            x264_encoder_close(encoder);
            encoder = nullptr;
        }

        if (sws_ctx) {
            sws_freeContext(sws_ctx);
            sws_ctx = nullptr;
        }

        memset(&pic_raw, 0, sizeof(pic_raw));

#else
        if (frame) {
            // Get the delayed frames
            for (got_output = 1; got_output; frame_num++) {
                int ret = avcodec_encode_video2(cctx, &pkt, nullptr, &got_output);
                if (ret < 0) {
                    throw runtime_error("Error encoding frame");
                }
                else if (got_output) {
                    fwrite(pkt.data, 1, pkt.size, fp);
                    av_free_packet(&pkt);
                }
            }
        }

		/* add sequence end code to have a real mpeg fp */
		fwrite(endcode, 1, sizeof(endcode), fp); // VideoLAN doesn't complain when this is absent
		fclose(fp);
		sws_freeContext(sws_ctx);
		avcodec_close(cctx);
		av_free(cctx);
		if (frame) av_freep(&frame->data[0]);
		av_frame_free(&frame);
#endif
		fp = nullptr;
	}

} // ns gpc