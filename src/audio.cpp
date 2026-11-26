#include "audio.hpp"
#include "util.hpp"

#include <media-io/audio-resampler.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obspet {

// ------------------------------------------------------------------ .wav

ClipPtr load_wav(const std::string &path, uint32_t rate, std::string &err)
{
	FILE *f = os_fopen(path.c_str(), "rb");
	if (!f) {
		err = "can't open it";
		return nullptr;
	}
	std::vector<unsigned char> all;
	{
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n > 0 && n < 64 * 1024 * 1024) {
			all.resize((size_t)n);
			if (fread(all.data(), 1, all.size(), f) != all.size())
				all.clear();
		}
	}
	fclose(f);
	auto u16 = [](const unsigned char *p) { return (uint32_t)(p[0] | p[1] << 8); };
	auto u32 = [](const unsigned char *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); };
	if (all.size() < 12 || memcmp(all.data(), "RIFF", 4) || memcmp(all.data() + 8, "WAVE", 4)) {
		err = "it isn't a .wav file (only .wav files can play)";
		return nullptr;
	}
	uint32_t fmt = 0, chans = 0, src_rate = 0, bits = 0;
	const unsigned char *data = nullptr;
	size_t data_len = 0;
	for (size_t at = 12; at + 8 <= all.size();) {
		const unsigned char *ch = all.data() + at;
		size_t size = u32(ch + 4), body = at + 8;
		size_t have = std::min(size, all.size() - body);
		if (!memcmp(ch, "fmt ", 4) && have >= 16) {
			fmt = u16(ch + 8);
			chans = u16(ch + 10);
			src_rate = u32(ch + 12);
			bits = u16(ch + 22);
			if (fmt == 0xFFFE && have >= 26)   // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
				fmt = u16(ch + 8 + 24);
		} else if (!memcmp(ch, "data", 4)) {
			data = all.data() + body;
			data_len = have;
			break;
		}
		at = body + size + (size & 1);
	}
	bool pcm = fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
	bool flt = fmt == 3 && bits == 32;
	// (a rate or channel count no real file has would only make the buffers below huge)
	if (!data || !chans || chans > 32 || src_rate < 1000 || src_rate > 768000 || (!pcm && !flt)) {
		err = "this kind of .wav isn't supported (use 16- or 24-bit PCM)";
		return nullptr;
	}
	size_t step = bits / 8, frame = step * chans, frames = std::min<size_t>(data_len / frame, (size_t)src_rate * 120);   // (2 min at most)
	auto sample = [&](const unsigned char *p) -> float {
		if (flt) {
			float v;
			memcpy(&v, p, 4);
			return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
		}
		switch (bits) {
		case 8:
			return ((int)p[0] - 128) / 128.0f;
		case 16:
			return (int16_t)u16(p) / 32768.0f;
		case 24:
			return (int32_t)(u32(p - 1) & 0xFFFFFF00u) / 2147483648.0f;   // (p-1: shift into the top bytes)
		default:
			return (int32_t)u32(p) / 2147483648.0f;
		}
	};
	// (24-bit reads 4 bytes ending at the sample; the byte before it is masked off. The first
	// sample's p-1 is inside the "data" chunk header, so it's always readable.)
	std::vector<float> l(frames), r(frames);
	for (size_t i = 0; i < frames; i++) {
		const unsigned char *p = data + i * frame;
		l[i] = sample(p);
		r[i] = chans > 1 ? sample(p + step) : l[i];
	}
	auto c = std::make_shared<Clip>();
	if (src_rate == rate || frames == 0) {
		c->l = std::move(l);
		c->r = std::move(r);
		return c;
	}
	// to OBS's rate with its own resampler; a little silence after the end flushes its last samples
	resample_info from = {src_rate, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
	resample_info to = {rate, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
	audio_resampler_t *rs = audio_resampler_create(&to, &from);
	if (!rs) {
		err = "couldn't convert its sample rate";
		return nullptr;
	}
	size_t want = (size_t)((double)frames * rate / src_rate);
	l.resize(frames + src_rate / 10);
	r.resize(frames + src_rate / 10);
	const uint8_t *in[MAX_AV_PLANES] = {(const uint8_t *)l.data(), (const uint8_t *)r.data()};
	uint8_t *out[MAX_AV_PLANES] = {};
	uint32_t got = 0;
	uint64_t ts = 0;
	if (audio_resampler_resample(rs, out, &got, &ts, in, (uint32_t)l.size()) && got) {
		size_t n = std::min<size_t>(got, want);
		c->l.assign((const float *)out[0], (const float *)out[0] + n);
		c->r.assign((const float *)out[1], (const float *)out[1] + n);
	}
	audio_resampler_destroy(rs);
	if (c->l.empty()) {
		err = "couldn't convert its sample rate";
		return nullptr;
	}
	return c;
}

// ------------------------------------------------------------------ the mixer

Mixer::Mixer(obs_source_t *source) : source_(source)
{
	obs_audio_info oai;
	if (obs_get_audio_info(&oai) && oai.samples_per_sec)
		rate_ = oai.samples_per_sec;
	thread_ = std::thread([this] { loop(); });
}

Mixer::~Mixer()
{
	{
		std::lock_guard<std::mutex> g(m_);
		stop_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void Mixer::play(ClipPtr c)
{
	if (!c || c->l.empty())
		return;
	{
		std::lock_guard<std::mutex> g(m_);
		if (voices_.size() >= 8)   // (never a pile-up)
			voices_.erase(voices_.begin());
		voices_.push_back({std::move(c), 0});
	}
	cv_.notify_all();
}

void Mixer::loop()
{
	const uint32_t n = rate_ / 100;   // 10 ms
	const uint64_t block_ns = 10000000;
	std::vector<float> L(n), R(n);
	uint64_t t = 0;
	std::unique_lock<std::mutex> lk(m_);
	// It never stops: silence goes out between sounds. A stream that stopped and restarted would
	// start each sound "now", behind OBS's mix, so OBS would cut or stretch the sound's start
	// ("adding N ms of audio buffering") after every quiet spell.
	while (!stop_) {
		std::fill(L.begin(), L.end(), 0.0f);
		std::fill(R.begin(), R.end(), 0.0f);
		for (auto &v : voices_) {
			size_t k = std::min<size_t>(n, v.c->l.size() - v.pos);
			for (size_t i = 0; i < k; i++) {
				L[i] += v.c->l[v.pos + i];
				R[i] += v.c->r[v.pos + i];
			}
			v.pos += k;
		}
		voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice &v) { return v.pos >= v.c->l.size(); }),
			      voices_.end());
		lk.unlock();
		for (uint32_t i = 0; i < n; i++) {   // overlapping sounds: never past full scale
			L[i] = std::clamp(L[i], -1.0f, 1.0f);
			R[i] = std::clamp(R[i], -1.0f, 1.0f);
		}
		if (!t)
			t = os_gettime_ns();
		obs_source_audio a = {};
		a.data[0] = (const uint8_t *)L.data();
		a.data[1] = (const uint8_t *)R.data();
		a.frames = n;
		a.speakers = SPEAKERS_STEREO;
		a.format = AUDIO_FORMAT_FLOAT_PLANAR;
		a.samples_per_sec = rate_;
		a.timestamp = t;
		obs_source_output_audio(source_, &a);
		t += block_ns;
		if (!os_sleepto_ns(t))
			t = os_gettime_ns();   // fell behind: carry on from now
		lk.lock();
	}
}

} // namespace obspet
