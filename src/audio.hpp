// The Pet's own sound: it plays its character's .wav files itself (the source has audio, so it has
// a fader in OBS's Audio Mixer), with no helper media source. Sounds can overlap.
#pragma once

#include <obs-module.h>

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

// Mixes whatever is playing and hands it to OBS in 10 ms blocks, on its own thread, only while
// something plays.
class Mixer {
public:
	explicit Mixer(obs_source_t *source);
	~Mixer();
	void play(ClipPtr c);   // any thread
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
	std::thread thread_;
};

} // namespace obspet
