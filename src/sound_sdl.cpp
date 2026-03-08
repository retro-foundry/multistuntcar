#ifdef linux

#include "dx_linux.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

bool Sound3D = false;

struct sound_buffer_t {
  std::vector<float> samples;
  int channels;
  int sample_rate;
  int bytes_per_frame;
  int frame_count;
  int base_frequency;
};

struct sound_source_t {
  sound_buffer_t* buff;
  double frame_pos;
  double pitch_scale;
  float volume;
  float pan;
  bool playing;
  bool looping;
};

static SDL_AudioDeviceID g_audio_device = 0;
static SDL_AudioSpec g_audio_spec = {};
static std::vector<sound_source_t*> g_sources;
static bool sound_initialized = false;

static inline float clampf(float v, float lo, float hi)
{
  return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void audio_callback(void* userdata, Uint8* stream, int len)
{
  (void)userdata;

  float* out = reinterpret_cast<float*>(stream);
  const int out_frames = len / static_cast<int>(sizeof(float) * 2);
  memset(stream, 0, len);

  for (sound_source_t* source : g_sources) {
    if (!source || !source->playing || !source->buff || source->buff->frame_count <= 0) {
      continue;
    }

    sound_buffer_t* buff = source->buff;
    const float pan = clampf(source->pan, -1.0f, 1.0f);
    const float gain_l = source->volume * sqrtf(0.5f * (1.0f - pan));
    const float gain_r = source->volume * sqrtf(0.5f * (1.0f + pan));
    const double base_step = static_cast<double>(buff->sample_rate) / static_cast<double>(g_audio_spec.freq);
    const double step = base_step * source->pitch_scale;

    for (int frame = 0; frame < out_frames; ++frame) {
      if (!source->playing) {
        break;
      }

      int idx = static_cast<int>(source->frame_pos);
      if (idx >= buff->frame_count) {
        if (source->looping) {
          source->frame_pos = fmod(source->frame_pos, static_cast<double>(buff->frame_count));
          idx = static_cast<int>(source->frame_pos);
        } else {
          source->playing = false;
          break;
        }
      }

      int next = idx + 1;
      if (next >= buff->frame_count) {
        next = source->looping ? 0 : idx;
      }

      const float frac = static_cast<float>(source->frame_pos - static_cast<double>(idx));
      float src_l = 0.0f;
      float src_r = 0.0f;
      float next_l = 0.0f;
      float next_r = 0.0f;

      if (buff->channels == 1) {
        src_l = src_r = buff->samples[idx];
        next_l = next_r = buff->samples[next];
      } else {
        const int i0 = idx * 2;
        const int i1 = next * 2;
        src_l = buff->samples[i0];
        src_r = buff->samples[i0 + 1];
        next_l = buff->samples[i1];
        next_r = buff->samples[i1 + 1];
      }

      const float mix_l = src_l + (next_l - src_l) * frac;
      const float mix_r = src_r + (next_r - src_r) * frac;

      out[frame * 2] += mix_l * gain_l;
      out[frame * 2 + 1] += mix_r * gain_r;

      source->frame_pos += step;
    }
  }

  for (int i = 0; i < out_frames * 2; ++i) {
    out[i] = clampf(out[i], -1.0f, 1.0f);
  }
}

bool sound_init(void)
{
  if (sound_initialized) {
    return true;
  }

  SDL_AudioSpec desired = {};
  desired.freq = 44100;
  desired.format = AUDIO_F32SYS;
  desired.channels = 2;
  desired.samples = 1024;
  desired.callback = audio_callback;

  g_audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &g_audio_spec, 0);
  if (g_audio_device == 0) {
    printf("SDL audio init failed: %s\n", SDL_GetError());
    return false;
  }

  SDL_PauseAudioDevice(g_audio_device, 0);
  sound_initialized = true;
  return true;
}

void sound_destroy(void)
{
  if (!sound_initialized) {
    return;
  }

  SDL_LockAudioDevice(g_audio_device);
  for (sound_source_t* source : g_sources) {
    delete source;
  }
  g_sources.clear();
  SDL_UnlockAudioDevice(g_audio_device);

  SDL_CloseAudioDevice(g_audio_device);
  g_audio_device = 0;
  sound_initialized = false;
}

bool sound_listener_position(float x, float y, float z)
{
  (void)x;
  (void)y;
  (void)z;
  return sound_initialized;
}

bool sound_listener_velocity(float x, float y, float z)
{
  (void)x;
  (void)y;
  (void)z;
  return sound_initialized;
}

bool sound_listener_orientation(float fx, float fy, float fz, float ux, float uy, float uz)
{
  (void)fx;
  (void)fy;
  (void)fz;
  (void)ux;
  (void)uy;
  (void)uz;
  return sound_initialized;
}

void sound_position(sound_source_t* source, float x, float y, float z, float min, float max)
{
  (void)y;
  (void)z;
  if (!source) {
    return;
  }
  float denom = (max > min) ? max : 1.0f;
  source->pan = clampf(x / denom, -1.0f, 1.0f);
}

void sound_set_pitch(sound_source_t* source, float pitch)
{
  if (!source) {
    return;
  }
  source->pitch_scale = (pitch > 0.0f) ? pitch : 1.0f;
}

void sound_set_frequency(sound_source_t* source, long frequency)
{
  if (!source || !source->buff || source->buff->base_frequency <= 0) {
    return;
  }
  source->pitch_scale = static_cast<double>(frequency) / static_cast<double>(source->buff->base_frequency);
  if (source->pitch_scale <= 0.0) {
    source->pitch_scale = 1.0;
  }
}

void sound_volume(sound_source_t* source, long millibels)
{
  if (!source) {
    return;
  }
  millibels = (millibels > 0) ? 0 : millibels;
  source->volume = static_cast<float>(pow(10.0, millibels / 2000.0));
}

void sound_pan(sound_source_t* source, long pan)
{
  if (!source) {
    return;
  }
  source->pan = clampf(static_cast<float>(pan) / 10000.0f, -1.0f, 1.0f);
}

void sound_play(sound_source_t* source)
{
  if (!sound_initialized || !source) {
    return;
  }
  SDL_LockAudioDevice(g_audio_device);
  source->playing = true;
  source->looping = false;
  SDL_UnlockAudioDevice(g_audio_device);
}

void sound_play_looping(sound_source_t* source)
{
  if (!sound_initialized || !source) {
    return;
  }
  SDL_LockAudioDevice(g_audio_device);
  source->playing = true;
  source->looping = true;
  SDL_UnlockAudioDevice(g_audio_device);
}

void sound_stop(sound_source_t* source)
{
  if (!sound_initialized || !source) {
    return;
  }
  SDL_LockAudioDevice(g_audio_device);
  source->playing = false;
  SDL_UnlockAudioDevice(g_audio_device);
}

bool sound_is_playing(sound_source_t* source)
{
  if (!source) {
    return false;
  }
  return source->playing;
}

void sound_set_position(sound_source_t* source, long newpos)
{
  if (!source || !source->buff || source->buff->bytes_per_frame <= 0) {
    return;
  }
  source->frame_pos = static_cast<double>(newpos / source->buff->bytes_per_frame);
}

long sound_get_position(sound_source_t* source)
{
  if (!source || !source->buff) {
    return 0;
  }
  return static_cast<long>(source->frame_pos) * source->buff->bytes_per_frame;
}

sound_buffer_t* sound_load(void* data, int size, int bits, int sign, int channels, int freq)
{
  if (!sound_initialized || !data || size <= 0 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || freq <= 0) {
    return NULL;
  }

  const int bytes_per_sample = bits / 8;
  const int bytes_per_frame = bytes_per_sample * channels;
  const int frame_count = size / bytes_per_frame;
  if (frame_count <= 0) {
    return NULL;
  }

  sound_buffer_t* buffer = new sound_buffer_t();
  buffer->channels = channels;
  buffer->sample_rate = freq;
  buffer->bytes_per_frame = bytes_per_frame;
  buffer->frame_count = frame_count;
  buffer->base_frequency = freq;
  buffer->samples.resize(static_cast<size_t>(frame_count * channels));

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  const int sample_count = frame_count * channels;
  for (int i = 0; i < sample_count; ++i) {
    float sample = 0.0f;
    if (bits == 8) {
      if (sign) {
        sample = static_cast<float>(reinterpret_cast<const int8_t*>(bytes)[i]) / 128.0f;
      } else {
        sample = (static_cast<float>(bytes[i]) - 128.0f) / 128.0f;
      }
    } else {
      const uint8_t* p = bytes + (i * 2);
      uint16_t raw_u16 = static_cast<uint16_t>(p[0] | (p[1] << 8));
      if (sign) {
        int16_t raw_s16 = static_cast<int16_t>(raw_u16);
        sample = static_cast<float>(raw_s16) / 32768.0f;
      } else {
        sample = (static_cast<float>(raw_u16) - 32768.0f) / 32768.0f;
      }
    }
    buffer->samples[static_cast<size_t>(i)] = clampf(sample, -1.0f, 1.0f);
  }

  return buffer;
}

sound_source_t* sound_source(sound_buffer_t* buffer)
{
  if (!sound_initialized || !buffer) {
    return NULL;
  }

  sound_source_t* source = new sound_source_t();
  source->buff = buffer;
  source->frame_pos = 0.0;
  source->pitch_scale = 1.0;
  source->volume = 1.0f;
  source->pan = 0.0f;
  source->playing = false;
  source->looping = false;

  SDL_LockAudioDevice(g_audio_device);
  g_sources.push_back(source);
  SDL_UnlockAudioDevice(g_audio_device);

  return source;
}

void sound_release_source(sound_source_t* source)
{
  if (!source) {
    return;
  }

  if (sound_initialized && g_audio_device != 0) {
    SDL_LockAudioDevice(g_audio_device);
    g_sources.erase(std::remove(g_sources.begin(), g_sources.end(), source), g_sources.end());
    SDL_UnlockAudioDevice(g_audio_device);
  }

  delete source;
}

void sound_release_buffer(sound_buffer_t* buffer)
{
  delete buffer;
}

#endif
