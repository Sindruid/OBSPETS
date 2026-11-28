// The Pet's own sound: it plays its character's .wav files itself, with no helper media source.
// Sounds can overlap. Each sound goes to the stream, to the streamer, or both, by its reaction:
// - the stream: the Pet source itself (on the audio tracks, never monitored; its fader in OBS's
//   Audio Mixer)
// - the streamer: a private "ears" source the Pet keeps (OBS's audio monitoring, in no scene and on
//   no track, so the stream never gets it from OBS; its volume and mute follow the Pet's fader)
#pragma once

#include <obs-module.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace obspet {

// A sound decoded to stereo float at OBS's sample rate.
struct Clip {
	std::vector<float> l, r;
};
using ClipPtr = std::shared_ptr<const Clip>;

// A .wav file (PCM 8/16/24/32-bit or 32-bit float, any rate, mono or more channels) -> the clip,
// or nullptr with `err` saying why.
ClipPtr load_wav(const std::string &path, uint32_t rate, std::string &err);

// Mixes whatever is playing and hands it to OBS in 10 ms blocks, on its own thread, all the time
// (silence between sounds keeps OBS's timing for the source steady).
class Mixer {
public:
	explicit Mixer(obs_source_t *source);
	~Mixer();
	void play(ClipPtr c);   // any thread
	void set_gain(float g) { gain_ = g; }   // any thread: everything it plays, times this
	uint32_t rate() const { return rate_; }

private:
	void loop();
	struct Voice {
		ClipPtr c;
		size_t pos = 0;
	};
	obs_source_t *source_;
	uint32_t rate_ = 48000;
	std::mutex m_;
	std::condition_variable cv_;
	std::vector<Voice> voices_;
	bool stop_ = false;
	std::atomic<float> gain_{1.0f};
	std::thread thread_;
};

// The streamer's side (see the top). Created private and kept active; monitoring is set on the UI
// thread (pet-source.cpp).
void register_ears_source();
obs_source_t *create_ears(const char *pet_name);
void destroy_ears(obs_source_t *ears);

// OBS 32+ says when an Audio Output Capture (Desktop Audio) records the monitoring device: whatever
// the streamer hears, the stream then gets too, through that capture. (OBS 31 doesn't say.)
void watch_monitor_capture();     // module load
void unwatch_monitor_capture();   // module unload
bool monitor_reaches_stream();    // any thread: that capture is on and not muted
std::string monitor_capture_name();   // its name ("" when none)
std::string monitor_device_name();    // OBS's monitoring device ("Default" for the default one)

} // namespace obspet
