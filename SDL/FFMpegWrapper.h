#pragma once

#define DEFAULT_FRAME_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/audio_fifo.h"
#include "libswresample/swresample.h"
#include "SDL.h"

	typedef struct
	{
		char* input_filename;
		int channels;
		AVSampleFormat sample_fmt;
		int frame_size;
	} MediaOptions;
	struct AVAudioFifo {
		AVFifoBuffer **buf;             /**< single buffer for interleaved, per-channel buffers for planar */
		int nb_buffers;                 /**< number of buffers */
		int nb_samples;                 /**< number of samples currently in FIFO */
		int allocated_samples;          /**< current allocated size, in samples */
		int channels;                   /**< number of channels */
		enum AVSampleFormat sample_fmt; /**< sample format */
		int sample_size;                /**< size, in bytes, of one sample in a buffer */
	};

	int find_audio_stream_index(AVFormatContext* format_context);
	int get_bytes_per_sample(AVSampleFormat sample_fmt);
	int resample_decoded_frame(uint8_t*** resampled_data, AVFrame* frame_to_decode);
	int decode_frame_iteration(AVFrame* frame_to_decode);
	int decode_frame();
	AVCodecContext* init_decoder(char* filename);
	int init_resampler();
	int init_fifo();

	int read_fifo(uint8_t*** read_buffer, int* last_index, 
		int buffer_target, int buffer_count, int size, int bytes_per_sample);

	int init_resampler_fifo(MediaOptions* options);
#ifdef __cplusplus
}
#endif