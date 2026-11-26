// Drawing the pet: one sprite frame, a shadow, a few rectangles for the glyphs, and (for a few
// seconds) a name label and a yanked chat message drawn from private text sources.
#include "pet.hpp"

#include <graphics/matrix4.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <cmath>
#include <cstdio>

namespace obspet {

static const double LABEL_LIFE = 4.5;   // seconds the "viewer pet <name>" label stays
static const int LABEL_JOIN = 14;       // px the two halves of the label overlap (their outline margins)

const char *EFFECT = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float4 frame;   // x, y, w, h in texels
uniform float2 texsize;
uniform float flip;
uniform float4 color;
uniform float alpha;

// pixel art: point sampling keeps each sprite pixel a hard square at any Size
sampler_state pt {
	Filter   = Point;
	AddressU = Clamp;
	AddressV = Clamp;
};
// text (labels, a yanked message): linear, so it stays smooth while it turns, or at the size an
// OBSPETS Chat is shown at. On a still, unscaled label it gives the same pixels as point sampling.
sampler_state lin {
	Filter   = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertInOut {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertInOut VSDefault(VertInOut v)
{
	VertInOut o;
	o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
	o.uv = v.uv;
	return o;
}

float4 PSSprite(VertInOut v) : TARGET
{
	float2 p = float2(lerp(v.uv.x, 1.0 - v.uv.x, flip), v.uv.y) * frame.zw;
	p = clamp(p, 0.01, frame.zw - 0.01);
	return image.Sample(pt, (frame.xy + p) / texsize);
}

float4 PSSolid(VertInOut v) : TARGET
{
	return color;
}

float4 PSTex(VertInOut v) : TARGET
{
	return image.Sample(lin, v.uv) * alpha;
}

technique Sprite { pass { vertex_shader = VSDefault(v); pixel_shader = PSSprite(v); } }
technique Solid  { pass { vertex_shader = VSDefault(v); pixel_shader = PSSolid(v); } }
technique Tex    { pass { vertex_shader = VSDefault(v); pixel_shader = PSTex(v); } }
)";

// ------------------------------------------------------------------ text sources

const char *text_id()
{
	static const char *id = obs_get_source_output_flags("text_gdiplus") ? "text_gdiplus" : "text_ft2_source_v2";
	return id;
}

static void text_color(obs_data_t *s, uint32_t color)
{
	obs_data_set_int(s, "color", color);
	if (strcmp(text_id(), "text_gdiplus") != 0) {
		obs_data_set_int(s, "color1", color);
		obs_data_set_int(s, "color2", color);
	}
}

obs_source_t *text_source(const char *name, obs_data_t *font)
{
	Data s = data_new();
	obs_data_set_obj(s.get(), "font", font);
	obs_data_set_string(s.get(), "text", " ");
	text_color(s.get(), 0xFFDEE4EC);
	obs_data_set_bool(s.get(), "outline", true);
	obs_data_set_int(s.get(), "outline_size", 4);
	obs_data_set_int(s.get(), "outline_color", 0xFF030305);
	obs_data_set_int(s.get(), "outline_opacity", 65);
	return obs_source_create_private(text_id(), name, s.get());
}

void set_text(obs_source_t *src, const std::string &text, uint32_t color)
{
	if (!src)
		return;
	Data s = data_new();
	obs_data_set_string(s.get(), "text", text.c_str());
	text_color(s.get(), color);
	obs_source_update(src, s.get());
}

void set_text(obs_source_t *src, const std::string &text, const std::string &hex)
{
	set_text(src, text, abgr(hex));
}

// ------------------------------------------------------------------ the yanked line's picture

static std::string unbase64(const std::string &s)
{
	static int T[256];
	static bool init = false;
	if (!init) {
		for (int &v : T)
			v = -1;
		const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		for (int i = 0; i < 64; i++)
			T[(unsigned char)A[i]] = i;
		init = true;
	}
	std::string out;
	int acc = 0, bits = 0;
	for (unsigned char c : s) {
		if (T[c] < 0)
			continue;
		acc = (acc << 6) | T[c];
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			out.push_back((char)((acc >> bits) & 0xFF));
		}
	}
	return out;
}

// A yanked line from an OBSPETS Chat comes as a picture in the chat's own look (base64 PNG).
void load_yank_img(Pet &d, const std::string &b64)
{
	obs_enter_graphics();
	if (d.yimg)
		gs_texture_destroy(d.yimg);
	d.yimg = nullptr;
	obs_leave_graphics();
	if (b64.empty())
		return;
	std::string path = config_path("yank.png");
	os_mkdirs(config_path("").c_str());
	std::string bytes = unbase64(b64);
	FILE *f = os_fopen(path.c_str(), "wb");
	if (!f)
		return;
	fwrite(bytes.data(), 1, bytes.size(), f);
	fclose(f);
	obs_enter_graphics();
	d.yimg = gs_texture_create_from_file(path.c_str());
	if (d.yimg) {
		d.yw = gs_texture_get_width(d.yimg);
		d.yh = gs_texture_get_height(d.yimg);
	}
	obs_leave_graphics();
	os_unlink(path.c_str());
	if (!d.yimg)
		warn("couldn't load the yanked message's picture");
}

void free_gfx(Pet &d)
{
	obs_enter_graphics();
	if (d.tex)
		gs_texture_destroy(d.tex);
	if (d.yimg)
		gs_texture_destroy(d.yimg);
	if (d.effect)
		gs_effect_destroy(d.effect);
	if (d.tr)
		gs_texrender_destroy(d.tr);
	if (d.ytr)
		gs_texrender_destroy(d.ytr);
	if (d.px_tr)
		gs_texrender_destroy(d.px_tr);
	if (d.px_stage)
		gs_stagesurface_destroy(d.px_stage);
	obs_leave_graphics();
	d.tex = d.yimg = nullptr;
	d.effect = nullptr;
	d.tr = d.ytr = d.px_tr = nullptr;
	d.px_stage = nullptr;
	for (obs_source_t **s : {&d.lname, &d.lrest, &d.yname, &d.ytext}) {
		if (*s)
			obs_source_release(*s);
		*s = nullptr;
	}
}

// ------------------------------------------------------------------ another chat's pixels

// For a chat overlay that isn't an OBSPETS Chat, the only way to know where its newest message is:
// look at what it draws. Four times a second, it draws the chat small (at most 96 x 270) into a
// texture, copies that to the CPU, and finds the highest row with anything opaque in it. The copy
// is read a quarter second later, so the GPU never waits. A chat with a solid background reads as
// its own top edge, which is still a fine place to sit.
void read_chat_pixels(Pet &d)
{
	if (!d.want_px || d.px_src.empty()) {
		d.px_staged = false;
		return;
	}
	if (d.now - d.px_at < 0.25)
		return;
	d.px_at = d.now;   // (before drawing: a chat that holds this pet comes back here, and stops)

	if (d.px_staged && d.px_stage) {
		uint8_t *data = nullptr;
		uint32_t line = 0;
		if (gs_stagesurface_map(d.px_stage, &data, &line)) {
			int found = -1;
			for (uint32_t r = 0; r < d.px_h && found < 0; r++) {
				const uint8_t *row = data + (size_t)r * line;
				int hits = 0;
				for (uint32_t c = 0; c < d.px_w; c++)
					if (row[c * 4 + 3] > 24 && ++hits >= 2) {   // (two, so a stray pixel isn't a message)
						found = (int)r;
						break;
					}
			}
			gs_stagesurface_unmap(d.px_stage);
			d.px_top = found < 0 ? 0 : 1.0 - (double)found / d.px_h;
		}
		d.px_staged = false;
	}

	obs_source_t *s = obs_get_source_by_name(d.px_src.c_str());
	if (!s)
		return;
	uint32_t sw = obs_source_get_width(s), sh = obs_source_get_height(s);
	if (sw && sh) {
		uint32_t w = std::min<uint32_t>(sw, 96), h = std::min<uint32_t>(sh, 270);
		if (!d.px_tr)
			d.px_tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		gs_texrender_reset(d.px_tr);
		if (gs_texrender_begin(d.px_tr, w, h)) {
			vec4 c;
			vec4_zero(&c);
			gs_clear(GS_CLEAR_COLOR, &c, 0, 0);
			gs_ortho(0, (float)sw, 0, (float)sh, -100, 100);
			gs_blend_state_push();
			gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
			obs_source_video_render(s);
			gs_blend_state_pop();
			gs_texrender_end(d.px_tr);
			if (!d.px_stage || d.px_w != w || d.px_h != h) {
				if (d.px_stage)
					gs_stagesurface_destroy(d.px_stage);
				d.px_stage = gs_stagesurface_create(w, h, GS_RGBA);
				d.px_w = w;
				d.px_h = h;
			}
			if (d.px_stage) {
				gs_stage_texture(d.px_stage, gs_texrender_get_texture(d.px_tr));
				d.px_staged = true;
			}
		}
	}
	obs_source_release(s);
}

// ------------------------------------------------------------------ drawing

static void rect(Pet &d, double x, double y, double w, double h, float r, float g, float b, float a)
{
	vec4 c;
	vec4_set(&c, r, g, b, a);
	gs_effect_set_vec4(d.p_color, &c);
	gs_matrix_push();
	gs_matrix_translate3f((float)std::floor(x + 0.5), (float)std::floor(y + 0.5), 0);
	gs_matrix_scale3f((float)std::max(1.0, std::floor(w + 0.5)), (float)std::max(1.0, std::floor(h + 0.5)), 1);
	while (gs_effect_loop(d.effect, "Solid"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

// a glyph centered on x with its bottom on y, `px` screen pixels per glyph pixel, over a soft shadow
static void draw_glyph(Pet &d, const Glyph &g, double x, double y, double px, const Rgb &c, double a, double *ox0 = nullptr,
		       double *oy0 = nullptr, double *opx = nullptr)
{
	px = std::max(1.0, std::floor(px + 0.5));
	double x0 = std::floor(x - g.w * px / 2 + 0.5), y0 = std::floor(y - g.h * px + 0.5);
	double sh = std::max(1.0, std::floor(px / 2));
	for (auto &r : g.runs)
		rect(d, x0 + r[0] * px + sh, y0 + r[1] * px + sh, r[2] * px, px, 0, 0, 0, (float)(a * 0.4));
	for (auto &r : g.runs)
		rect(d, x0 + r[0] * px, y0 + r[1] * px, r[2] * px, px, c.r, c.g, c.b, (float)a);
	if (ox0)
		*ox0 = x0, *oy0 = y0, *opx = px;
}

// two text sources side by side (a name, then the rest) drawn into a texrender, so the pair,
// outline and all, can be faded or turned as one
static gs_texture_t *pair_texture(gs_texrender_t *&tr, obs_source_t *a, obs_source_t *b, bool solo, uint32_t &w, uint32_t &h)
{
	if (!a || !b)
		return nullptr;
	uint32_t w1 = obs_source_get_width(a), h1 = obs_source_get_height(a);
	uint32_t w2 = obs_source_get_width(b), h2 = obs_source_get_height(b);
	if (solo) {
		w2 = LABEL_JOIN;
		h2 = 0;
	}
	if (w1 == 0 || w2 == 0)
		return nullptr;
	w = w1 + w2 - LABEL_JOIN;
	h = std::max(h1, h2);
	if (!tr)
		tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(tr);
	if (gs_texrender_begin(tr, w, h)) {
		vec4 c;
		vec4_zero(&c);
		gs_clear(GS_CLEAR_COLOR, &c, 0, 0);
		gs_ortho(0, (float)w, 0, (float)h, -100, 100);
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(a);
		if (!solo) {
			gs_matrix_push();
			gs_matrix_translate3f((float)(w1 - LABEL_JOIN), 0, 0);
			obs_source_video_render(b);
			gs_matrix_pop();
		}
		gs_blend_state_pop();
		gs_texrender_end(tr);
	}
	return gs_texrender_get_texture(tr);
}

static void draw_tex(Pet &d, gs_texture_t *tex, double x, double y, uint32_t w, uint32_t h, float alpha, double angle = 0,
		     bool centred = false)
{
	gs_effect_set_texture(d.p_image, tex);
	gs_effect_set_float(d.p_alpha, alpha);
	gs_matrix_push();
	gs_matrix_translate3f((float)std::floor(x + 0.5), (float)std::floor(y + 0.5), 0);
	if (centred) {
		gs_matrix_rotaa4f(0, 0, 1, (float)angle);
		gs_matrix_translate3f(-(float)std::floor(w / 2.0), -(float)std::floor(h / 2.0), 0);
	}
	gs_matrix_scale3f((float)w, (float)h, 1);
	while (gs_effect_loop(d.effect, "Tex"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();
}

static void draw_yank(Pet &d)
{
	if (!d.has_yank)
		return;
	gs_texture_t *tex;
	uint32_t w = 0, h = 0;
	if (d.yimg) {
		tex = d.yimg;
		w = d.yw;
		h = d.yh;
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);   // a picture: straight alpha
	} else {
		tex = pair_texture(d.ytr, d.yname, d.ytext, false, w, h);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	}
	if (tex) {
		if (d.yimg && d.yank.k != 1) {   // an OBSPETS Chat's picture, at the size the chat is shown
			w = (uint32_t)std::max(1.0, std::round(w * d.yank.k));
			h = (uint32_t)std::max(1.0, std::round(h * d.yank.k));
		}
		d.yank.hw = w / 2.0;
		d.yank.hh = h / 2.0;
		draw_tex(d, tex, d.yank.x, d.yank.y, w, h, 1, d.yank.a, true);
	}
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	// Auto mod deleted it: a red line through the message (not the name), turning with it. (An
	// OBSPETS Chat's picture has its own, drawn by the page in the chat's look.)
	if (tex && d.yank.strike && !d.yimg) {
		double name_w = d.yname ? std::max(0.0, (double)obs_source_get_width(d.yname) - LABEL_JOIN) : 0;
		double x0 = name_w + h * 0.3, x1 = w - h * 0.2;   // (the message starts with two spaces)
		double th = std::max(2.0, std::round(h * 0.09));
		if (x1 > x0) {
			gs_matrix_push();
			gs_matrix_translate3f((float)std::floor(d.yank.x + 0.5), (float)std::floor(d.yank.y + 0.5), 0);
			gs_matrix_rotaa4f(0, 0, 1, (float)d.yank.a);
			gs_matrix_translate3f(-(float)std::floor(w / 2.0), -(float)std::floor(h / 2.0), 0);
			rect(d, x0 - 1, h * 0.5 - th / 2 - 1, x1 - x0 + 2, th + 2, 0, 0, 0, 0.55f);
			rect(d, x0, h * 0.5 - th / 2, x1 - x0, th, 0.88f, 0.28f, 0.24f, 1);
			gs_matrix_pop();
		}
	}
}

static void draw_label(Pet &d)
{
	if (!d.label_on)
		return;
	uint32_t w = 0, h = 0;
	gs_texture_t *tex = pair_texture(d.tr, d.lname, d.lrest, d.label_solo, w, h);
	if (!tex)
		return;
	double t = d.label_t;
	double a = std::min({1.0, t / 0.25, (LABEL_LIFE - t) / 0.6});
	double rise = std::pow(1 - std::min(1.0, t / 0.35), 2) * 8;
	// beside it, level with its body, on the side it had more room on when the label came up
	double gap = d.pack->half * d.px;
	double x = d.label_side > 0 ? d.x + gap : d.x - gap - w;
	x = clampd(x, 8, d.W - w - 8);
	double y = std::max(8.0, d.y - d.pack->top * 0.4 * d.px - h / 2.0 + rise);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	draw_tex(d, tex, x, y, w, h, (float)std::max(0.0, a));
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
}

static void draw_parts(Pet &d)
{
	const Pack &P = *d.pack;
	double S = d.S, base = std::max(2.0, S * 0.7);
	for (auto &p : d.parts) {
		double t = p.t, k = p.t / p.life;
		if (t < 0)
			continue;
		switch (p.k) {
		case Part::K::Z: {
			double a = std::min(1.0, t / 0.5) * (1 - k) * 0.85;
			draw_glyph(d, P.glyph("z"), p.x + p.f * k * 6 * S, p.y - k * 14 * S, base * (0.7 + 0.3 * std::min(1.0, k * 2)),
				   P.color("z"), a);
			break;
		}
		case Part::K::Heart: {
			double a = std::min(1.0, t / 0.2) * (1 - std::max(0.0, (k - 0.55) / 0.45)) * 0.95;
			double x = p.x + p.vx * t + std::sin(t * 3.2 + p.ph) * 2 * S;
			double y = p.y - p.vy * t;
			double x0, y0, px;
			draw_glyph(d, P.glyph(p.g ? p.g : "heart"), x, y, base * (0.55 + 0.45 * std::min(1.0, t / 0.3)),
				   P.color(p.c), a, &x0, &y0, &px);
			if (!p.g) {
				const Rgb &gl = P.color("glint");
				rect(d, x0 + px, y0 + px, px, px, gl.r, gl.g, gl.b, (float)(a * 0.8));
			}
			break;
		}
		case Part::K::Mark: {
			// rides above its head (or beside it); pops in big and settles, fades out at the end
			double a = std::min(1.0, (p.life - t) / 0.25);
			double pop = 1 + 0.6 * std::max(0.0, 1 - t / 0.14);
			if (p.throb)
				pop *= 1 + 0.14 * std::sin(t * 15);
			double bob = d.bobbing ? std::sin(d.bob / 0.24 * 3.14159265358979) * 1.5 * S : 0;
			double hy = d.y - (d.anim == "sleep" ? P.top_asleep : P.top) * d.px - bob;
			double hx = d.x + d.face * (p.side ? P.half - 1 : P.head) * d.px;
			if (p.side)
				hy += 2 * S;
			draw_glyph(d, P.glyph(p.g ? p.g : "dots"), hx, hy, base * (p.big ? 1.35 : 1) * pop, P.color(p.c), a);
			break;
		}
		case Part::K::Spark: {
			double e = (1 - std::exp(-t * 7)) / 7;
			draw_glyph(d, P.glyph("spark"), p.x + p.vx * e, p.y + p.vy * e, base * 0.75, P.color("spark"), 1 - k * k);
			break;
		}
		}
	}
}

void Pet::render()
{
	if (!tex || !effect || !pack || !started)
		return;
	read_chat_pixels(*this);
	const Pack &P = *pack;
	bool fb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	// shadow on whatever it stands on, fading as it leaves the ground
	double gy = mode == Mode::Air ? ay1 : y;
	double a = 0.34 * std::max(0.0, 1 - std::max(0.0, gy - y) / (50 * S));
	if (a > 0.02) {
		double sh = P.shadow * px;
		rect(*this, x - sh, gy - S, 2 * sh, S, 0, 0, 0, (float)a);
		rect(*this, x - sh * 2 / 3, gy, sh * 4 / 3, S * 0.5, 0, 0, 0, (float)(a * 0.6));
	}

	// a chat message it yanked out, under the pet so it can pounce on it
	draw_yank(*this);

	// the pet: a held pose, or the animation's frame
	Cell cell;
	if (!fixed.empty()) {
		auto it = P.poses.find(fixed);
		cell = it != P.poses.end() ? it->second : P.anim("idle").cells[0];
	} else {
		const Anim &an = P.anim(anim);
		cell = an.cells[(size_t)std::fmod(std::max(0.0, std::floor(at * an.fps)), (double)std::max(1, an.n()))];
	}
	vec4 v;
	vec4_set(&v, (float)(cell.col * P.cw), (float)(cell.row * P.ch), (float)P.cw, (float)P.ch);
	gs_effect_set_vec4(p_frame, &v);
	vec2 ts;
	vec2_set(&ts, (float)tw, (float)th);
	gs_effect_set_vec2(p_texsize, &ts);
	gs_effect_set_float(p_flip, ((face < 0) != P.faces_left) ? 1.0f : 0.0f);
	gs_effect_set_texture(p_image, tex);
	double w = P.cw * px * (1 - sq * 0.6), h = P.ch * px * (1 + sq);
	double bb = bobbing ? std::sin(bob / 0.24 * 3.14159265358979) * 1.5 * S : 0;
	gs_matrix_push();
	gs_matrix_translate3f((float)std::floor(x - w / 2 + jx + 0.5), (float)std::floor(y - h - bb + P.feet * px + 0.5), 0);
	gs_matrix_scale3f((float)w, (float)h, 1);
	while (gs_effect_loop(effect, "Sprite"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();

	// z's, hearts, "!" and sparks, then the label
	draw_parts(*this);
	draw_label(*this);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(fb);
}

} // namespace obspet
