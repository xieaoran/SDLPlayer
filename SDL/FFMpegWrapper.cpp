// FFMpegWrapper.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FFMpegWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

	AVBitStreamFilterContext* aac_bitstream_filter_context;
	AVBitStreamFilterContext* dump_extra_filter_context;

	AVStream* audio_stream;

	AVCodec* decoder;
	AVFormatContext* decoder_format_context;
	AVCodecContext* decoder_codec_context;
	AVPacket packet_to_decode;

	SwrContext* resample_context;

	AVAudioFifo* buffer_fifo;
	int64_t buffer_timestamp;

	int audio_stream_index;
	int operation_result;
	int input_finished;
	int global_headers;

	MediaOptions* convert_options;

	int find_audio_stream_index(AVFormatContext* format_context)
	{
		int audio_stream_index = -1;
		for (int stream_index = 0; stream_index < (int)format_context->nb_streams; stream_index++)
		{
			if (format_context->streams[stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				audio_stream_index = stream_index;
				break;
			}
		}
		return audio_stream_index;
	}

	int get_bytes_per_sample(AVSampleFormat sample_fmt)
	{
		if (sample_fmt == AV_SAMPLE_FMT_FLT || sample_fmt == AV_SAMPLE_FMT_FLTP) return 4;
		if (sample_fmt == AV_SAMPLE_FMT_DBL || sample_fmt == AV_SAMPLE_FMT_DBLP) return 8;
		if (sample_fmt == AV_SAMPLE_FMT_U8 || sample_fmt == AV_SAMPLE_FMT_U8P) return 1;
		if (sample_fmt == AV_SAMPLE_FMT_S16 || sample_fmt == AV_SAMPLE_FMT_S16P) return 2;
		if (sample_fmt == AV_SAMPLE_FMT_S32 || sample_fmt == AV_SAMPLE_FMT_S32P) return 4;
		return -1;
	}

	int resample_decoded_frame(uint8_t*** resampled_data, AVFrame* frame_to_decode)
	{
		*resampled_data = (uint8_t**)av_malloc(convert_options->channels*sizeof(uint8_t*));
		if (!resampled_data) return AVERROR_BUG;

		operation_result = av_samples_alloc(*resampled_data, NULL, convert_options->channels,
			frame_to_decode->nb_samples, convert_options->sample_fmt, 0);
		if (operation_result < 0) return operation_result;

		operation_result = swr_convert(resample_context, *resampled_data, frame_to_decode->nb_samples,
			(const uint8_t**)frame_to_decode->extended_data, frame_to_decode->nb_samples);
		return operation_result;
	}

	int decode_frame_iteration(AVFrame* frame_to_decode)
	{
		int decoded_bytes, delayed_decoded_bytes;
		int data_present, delayed_data_present;
		int data_present_total = 0;
		uint8_t** resampled_data;
		int resampled_frames_count;

		decoded_bytes = avcodec_decode_audio4(decoder_codec_context, frame_to_decode, &data_present, &packet_to_decode);
		if (decoded_bytes < 0) {
			av_free_packet(&packet_to_decode);
			return decoded_bytes;
		}

		if (data_present)
		{
			resampled_frames_count = resample_decoded_frame(&resampled_data, frame_to_decode);
			if (resampled_frames_count < 0) {
				av_free_packet(&packet_to_decode);
				return resampled_frames_count;
			}
			operation_result = av_audio_fifo_write(buffer_fifo,
				(void**)resampled_data, resampled_frames_count);
			av_free(resampled_data[0]);
			av_free(resampled_data);
			if (operation_result < 0) {
				av_free_packet(&packet_to_decode);
				return operation_result;
			}
		}

		data_present_total = data_present_total || data_present;

		if (decoder->capabilities & AV_CODEC_CAP_DELAY) {
			packet_to_decode.data = NULL;
			packet_to_decode.size = 0;
			while (true) {
				delayed_decoded_bytes = avcodec_decode_audio4(decoder_codec_context, frame_to_decode, &delayed_data_present, &packet_to_decode);
				if (delayed_decoded_bytes < 0) {
					av_free_packet(&packet_to_decode);
					return delayed_decoded_bytes;
				}
				else if (delayed_data_present) {
					resampled_frames_count = resample_decoded_frame(&resampled_data, frame_to_decode);
					if (resampled_frames_count < 0) {
						av_free_packet(&packet_to_decode);
						return resampled_frames_count;
					}

					operation_result = av_audio_fifo_write(buffer_fifo,
						(void**)(resampled_data), resampled_frames_count);
					if (operation_result < 0) {
						av_free_packet(&packet_to_decode);
						return operation_result;
					}
					data_present_total = data_present_total || delayed_data_present;
				}
				else break;
			}
		}

		if (input_finished && data_present) input_finished = 0;
		av_free_packet(&packet_to_decode);
		return data_present_total;
	}

	int decode_frame()
	{
		AVFrame* frame_to_decode = av_frame_alloc();
		operation_result = av_read_frame(decoder_format_context, &packet_to_decode);
		if (packet_to_decode.stream_index != audio_stream_index) return 0;
		if (operation_result == AVERROR_EOF) {
			input_finished = 1;
			return AVERROR_EOF;
		}

		else if (operation_result < 0) {
			av_frame_free(&frame_to_decode);
			return operation_result;
		}

		operation_result = decode_frame_iteration(frame_to_decode);
		if (operation_result < 0) {
			av_frame_free(&frame_to_decode);
			return operation_result;
		}
		av_frame_free(&frame_to_decode);
		return buffer_fifo->nb_samples;
	}

	AVCodecContext* init_decoder(char* filename)
	{
		decoder_format_context = avformat_alloc_context();

		operation_result = avformat_open_input(&decoder_format_context, filename, NULL, NULL);
		if (operation_result < 0) return NULL;

		operation_result = avformat_find_stream_info(decoder_format_context, NULL);
		if (operation_result < 0) return NULL;

		audio_stream_index = find_audio_stream_index(decoder_format_context);
		if (audio_stream_index == -1) return NULL;

		audio_stream = decoder_format_context->streams[audio_stream_index];
		decoder_codec_context = audio_stream->codec;
		decoder = avcodec_find_decoder(decoder_codec_context->codec_id);
		if (!decoder) return NULL;

		operation_result = avcodec_open2(decoder_codec_context, decoder, NULL);
		if (operation_result < 0) return NULL;

		return decoder_codec_context;
	}

	int init_resampler()
	{
		uint64_t decoder_channel_layout = (decoder_codec_context->channel_layout) ?
			decoder_codec_context->channel_layout :
			av_get_default_channel_layout(decoder_codec_context->channels);
		uint64_t playback_channel_layout = av_get_default_channel_layout(convert_options->channels);
		int resample_rate = decoder_codec_context->sample_rate;
		resample_context = swr_alloc_set_opts(NULL, playback_channel_layout, convert_options->sample_fmt,
			resample_rate, decoder_channel_layout, decoder_codec_context->sample_fmt,
			decoder_codec_context->sample_rate, 0, NULL);
		if (!resample_context) return AVERROR_BUG;

		operation_result = swr_init(resample_context);
		if (operation_result < 0) return operation_result;

		return 0;
	}

	int init_fifo()
	{
		buffer_fifo = av_audio_fifo_alloc(convert_options->sample_fmt, convert_options->channels, 1);
		if (!buffer_fifo) return AVERROR_BUG;

		return 0;
	}

	int read_fifo(uint8_t*** read_buffer, int* last_index, 
		int buffer_target, int buffer_count, int samples, int bytes_per_sample)
	{
		if (buffer_fifo == NULL) return AVERROR_BUFFER_TOO_SMALL;
		int real_buffer_count = buffer_fifo->nb_samples / samples;
		int real_buffer_read = real_buffer_count < buffer_target ? real_buffer_count : buffer_target;
		int buffer_count_index;
		for (buffer_count_index = 0; buffer_count_index < real_buffer_read; buffer_count_index++)
		{
			operation_result = av_audio_fifo_read(buffer_fifo, (void**)read_buffer[*last_index], samples);
			(*last_index)++;
			if (*last_index == buffer_count) *last_index = 0;
			if (operation_result < 0) break;
		}
		return input_finished == 1 ? AVERROR_EOF : real_buffer_read;
	}

	int init_resampler_fifo(MediaOptions* options)
	{
		convert_options = options;

		operation_result = init_resampler();
		if (operation_result < 0) return operation_result;

		operation_result = init_fifo();
		if (operation_result < 0) return operation_result;

		input_finished = 0;
		buffer_timestamp = 0;

		operation_result = 0;
		return operation_result;
	}


#ifdef __cplusplus
}
#endif
