// SDL.cpp : 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include "stdlib.h"
#include "FFMpegWrapper.h"
#ifdef __cplusplus
extern "C" {
#endif
#define COMMON_DELAY 200
#define BUFFER_COUNT 10
#define BUFFER_MIN 5

	SDL_AudioSpec audio_spec, actual_spec;
	SDL_AudioDeviceID dev;
	uint8_t ***audio_buffer_plane;
	char* filename;
	int frame_size;
	int bytes_per_sample;
	int first_index, last_index;
	char exit_flag;

	AVSampleFormat convert_ff_format(SDL_AudioFormat sdl_format)
	{
		switch (sdl_format)
		{
		case AUDIO_U8:
			return AV_SAMPLE_FMT_U8;
		case AUDIO_S16:
			return AV_SAMPLE_FMT_S16;
		case AUDIO_S32:
			return AV_SAMPLE_FMT_S32;
		case AUDIO_F32:
			return AV_SAMPLE_FMT_FLT;
		default:
			return AV_SAMPLE_FMT_NONE;
		}
	}

	SDL_AudioFormat convert_sdl_format(AVSampleFormat ff_format)
	{
		if (ff_format == AV_SAMPLE_FMT_FLT || ff_format == AV_SAMPLE_FMT_FLTP) return AUDIO_F32;
		if (ff_format == AV_SAMPLE_FMT_DBL || ff_format == AV_SAMPLE_FMT_DBLP) return AUDIO_S32;
		if (ff_format == AV_SAMPLE_FMT_U8 || ff_format == AV_SAMPLE_FMT_U8P) return AUDIO_U8;
		if (ff_format == AV_SAMPLE_FMT_S16 || ff_format == AV_SAMPLE_FMT_S16P) return AUDIO_S16;
		if (ff_format == AV_SAMPLE_FMT_S32 || ff_format == AV_SAMPLE_FMT_S32P) return AUDIO_S32;
		return AUDIO_S16;
	}

	int get_bytes_per_sample_sdl(SDL_AudioFormat sdl_format, int channels)
	{
		int bytes_per_channel;
		switch (sdl_format)
		{
		case AUDIO_U8:
			bytes_per_channel = 1;
			break;
		case AUDIO_S16:
			bytes_per_channel = 2;
			break;
		case AUDIO_S32:
			bytes_per_channel = 4;
			break;
		case AUDIO_F32:
			bytes_per_channel = 4;
			break;
		default:
			bytes_per_channel = 2;
			break;
		}
		return bytes_per_channel * channels;
	}

	void audio_callback(void *udata, Uint8 *stream, int len) {
		if (exit_flag == 1 && first_index == last_index) {
			exit_flag = 2;
			return;
		}
		int buffers_left = last_index - first_index >= 0 ? last_index - first_index : BUFFER_COUNT + last_index - first_index;
		if (buffers_left == 0) return;
		SDL_memcpy(stream, audio_buffer_plane[first_index][0], len);
		first_index++;
		if (first_index == BUFFER_COUNT) first_index = 0;
	}

	int audio_load()
	{
		av_register_all();
		AVCodecContext* result_context = init_decoder(filename);
		if (result_context == NULL) return -1;

		SDL_Init(SDL_INIT_AUDIO);
		audio_spec.channels = result_context->channels;
		audio_spec.format = convert_sdl_format(result_context->sample_fmt);
		audio_spec.freq = result_context->sample_rate;
		audio_spec.callback = audio_callback;
		const char* deviceName = SDL_GetAudioDeviceName(0, 0);
		dev = SDL_OpenAudioDevice(deviceName, 0, &audio_spec, &actual_spec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);

		MediaOptions* convert_options = (MediaOptions*)malloc(sizeof(MediaOptions));
		convert_options->channels = actual_spec.channels;
		convert_options->input_filename = filename;
		convert_options->sample_fmt = convert_ff_format(actual_spec.format);
		init_resampler_fifo(convert_options);

		audio_buffer_plane = (uint8_t***)malloc(sizeof(Uint8**)*BUFFER_COUNT);
		for (int buffer_index = 0; buffer_index < BUFFER_COUNT; buffer_index++)
		{
			audio_buffer_plane[buffer_index] = (uint8_t**)malloc(sizeof(Uint8*));
			audio_buffer_plane[buffer_index][0] = (uint8_t*)malloc(actual_spec.size);
		}

		bytes_per_sample = get_bytes_per_sample_sdl(actual_spec.format, actual_spec.channels);
		first_index = 0;
		last_index = 0;
		exit_flag = 0;

		return 0;
	}

	int audio_playblack(void* data)
	{
		SDL_PauseAudioDevice(dev, 0);
		while (exit_flag < 2) {
			SDL_Delay(COMMON_DELAY * 10);
		}
		return 0;
	}

	int audio_decode(void* data)
	{
		while (exit_flag < 2)
		{
			int buffers_left = last_index - first_index >= 0 ? last_index - first_index : BUFFER_COUNT + last_index - first_index;
			if (buffers_left < BUFFER_MIN)
			{
				int buffer_target = BUFFER_COUNT - buffers_left - 1;
				int sdl_result;

				int decoded_samples = 0;
				while (decoded_samples < buffer_target*actual_spec.samples)
				{
					decoded_samples = decode_frame();
					if(decoded_samples < 0)
					{
						exit_flag = 1;
						return 0;
					}
				}
				sdl_result = read_fifo(audio_buffer_plane, &last_index,
					buffer_target, BUFFER_COUNT, actual_spec.samples, bytes_per_sample);

				if (sdl_result < 0) exit_flag = 1;
			}
			else
			{
				SDL_Delay(COMMON_DELAY);
			}
		}
		return 0;
	}

	int main(int argc, char *argv[]) {
		printf_s("%s", "请将任意格式的音乐文件放至该文件夹下，然后输入文件名。\n");
		filename = (char*)malloc(sizeof(char) * 256);
		scanf("%s", filename);
		getchar();
		audio_load();
		SDL_Thread* decode_thread = SDL_CreateThread(audio_decode, "decode_thread", (void*)NULL);
		SDL_Thread* playback_thread = SDL_CreateThread(audio_playblack, "playback_thread", (void *)NULL);
		SDL_DetachThread(decode_thread);
		SDL_WaitThread(playback_thread, NULL);
		SDL_Quit();
		return 0;
	}

#ifdef __cplusplus
}
#endif