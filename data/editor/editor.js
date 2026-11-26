// OBSPETS character editor: the Characters view of the settings page (data/settings/), loaded there
// as a module so its names stay apart from settings.js. settings.js switches views and calls
// window.OBSPET_EDITOR (bottom of this file). The plugin's side is src/editor.cpp; every request
// carries the per-run key from the page's URL.
//
// A character made here is a folder of plain files:
//   pack.json            what the editor knows (frames, speed, nudges, sounds, details)
//   anims/<slot>.png     one strip per animation (and per jump pose drawn on its own)
//   sounds/<role>/*.wav  48 kHz stereo, trimmed and set to a stream-friendly loudness
//   source/sheet.png     the sprite sheet the animations were picked from, if any (pack.json's
//                        `source` says which cells), so the sheet dialog can reopen. Not exported.
//   sheet.png, pack.lua  generated from the above on every save, for the Pet to load
// A role's sounds in pack.json: a list of files, "@role" to share another role's, false for
// silence, or absent for the built-in sound from its `sound_set` (data/sounds/<set>/<role>/).
// All image and audio processing happens here, in the browser.
'use strict';

const KEY = new URLSearchParams(location.search).get('k') || '';

// ------------------------------------------------------------------ what a character has

// `what`: what the pet uses it for, short enough for a tile. `group`: "reaction" ones never borrow
// another: the reactions that need one stay off until the character has it, or names a stand-in.
const ANIMS = [
	{ k: 'idle', name: 'Idle', what: 'Standing about. The rest borrow it', req: true },
	{ k: 'look', name: 'Look', what: 'Watching chat and alerts' },
	{ k: 'walk', name: 'Walk', what: 'Wandering, going to chat' },
	{ k: 'run', name: 'Run', what: 'Zoomies, raids, dashes' },
	{ k: 'sleep', name: 'Sleep', what: "Naps, z's float up" },
	{ k: 'paw', name: 'Swat', what: 'Swats, play, ripping lines' },
	{ k: 'clean', name: 'Content', what: 'Pleased: grooming, panting' },
	{ k: 'clean2', name: 'Content 2', what: 'Idling, soaking up hearts' },
	{ k: 'stretch', name: 'Stretch', what: 'Before naps, snubbing chat' },
	{ k: 'call', name: 'Call', what: 'A bark or meow, with the sound' },
	{ k: 'beg', name: 'Beg', what: 'Begging chat', group: 'reaction' },
	{ k: 'scared', name: 'Scared', what: 'Startled, scared off', group: 'reaction' },
	{ k: 'angry', name: 'Angry', what: 'Cross at chat', group: 'reaction' },
];
// The chat reactions each one is needed for (the plugin's REACT_NEEDS): off without it
const NEEDED_BY = {
	look: ['Curious', 'Unimpressed'], run: ['Zoomies'], sleep: ['Naps'], paw: ['Plays', 'Swats at chat', 'Rips a message out'],
	beg: ['Begs'], scared: ['Startled', 'Scared'], angry: ['Angry'],
};
const SHEET_ORDER = ['idle', 'look', 'clean', 'clean2', 'walk', 'run', 'sleep', 'paw', 'stretch', 'call', 'beg', 'scared', 'angry'];
const FALLBACK = {
	look: 'idle', clean: 'idle', clean2: 'clean', walk: 'idle', run: 'walk', sleep: 'idle', paw: 'idle',
	stretch: 'clean', call: 'look', beg: 'look', scared: 'look', angry: 'scared',
};
const POSES = [
	{ k: 'crouch', name: 'Crouch', what: 'Before a jump or pounce', fb: 'idle' },
	{ k: 'rise', name: 'Rise', what: 'Going up', fb: 'crouch' },
	{ k: 'fall', name: 'Fall', what: 'Coming down', fb: 'rise' },
	{ k: 'land', name: 'Land', what: 'Touching down', fb: 'crouch' },
];
// words in a file name that say which slot it is ("cat_walk.png", "Run-Sheet.gif")
const SLOT_WORDS = {
	idle: ['idle', 'sit', 'stand', 'breath'], look: ['look', 'alert', 'listen', 'notice'], walk: ['walk'],
	run: ['run', 'dash', 'sprint', 'gallop'], sleep: ['sleep', 'nap', 'rest', 'lie'], clean2: ['clean2', 'groom2', 'wash2', 'lick2'],
	clean: ['clean', 'groom', 'wash', 'lick', 'pant'], paw: ['paw', 'swat', 'swipe', 'attack', 'hit', 'claw'],
	stretch: ['stretch', 'yawn', 'bow'], call: ['call', 'bark', 'meow', 'speak', 'howl'], beg: ['beg', 'plead', 'situp', 'sit_up'],
	scared: ['scared', 'scare', 'fright', 'hurt', 'cower', 'puff'], angry: ['angry', 'hiss', 'growl', 'mad', 'rage'],
	crouch: ['crouch'], rise: ['rise', 'jumpup', 'jump_up'], fall: ['fall', 'jumpdown', 'jump_down'], land: ['land'],
};

const VOICE = [
	{ k: 'pet', name: 'Petted', what: 'Channel-point pets, love from chat (plays while asleep too)' },
	{ k: 'notice', name: 'Notices', what: 'A follow, bits, a raid starting, a greeting' },
	{ k: 'cheer', name: 'Happy', what: "A sub, a gift, a raid's welcome" },
	{ k: 'question', name: 'Curious', what: 'Curious, a hello, startled' },
	{ k: 'plead', name: 'Begging', what: 'Wants attention' },
	{ k: 'angry', name: 'Angry', what: 'Angry or scared' },
];
const FOLEY = [
	{ k: 'swipe', name: 'Swipe', what: 'Each swat' },
	{ k: 'land', name: 'Land', what: 'Landing a jump' },
	{ k: 'pounce', name: 'Pounce', what: 'A heavier landing' },
	{ k: 'fling', name: 'Fling', what: 'A chat message it yanked, flying off' },
	{ k: 'pop', name: 'Pop', what: '…and bursting' },
];
const ROLES = [...VOICE, ...FOLEY];
const isVoice = (r) => VOICE.some((v) => v.k === r);
const LUFS_VOICE = -38, LUFS_FOLEY = -41;
// The sound sets that come with OBSPETS (data/sounds/<set>/). A set without a role uses "default"'s.
const SOUND_SETS = { default: 'Soft (suits any pet)', cat: 'Cat', dog: 'Dog', none: 'None' };
const setName = (k) => SOUND_SETS[k] || k[0].toUpperCase() + k.slice(1);
const soundSet = () => (S.pack && S.pack.sound_set) || 'default';
// the built-in files a role plays: [set, files] (files empty: none)
function builtin(role, set = soundSet()) {
	if (set === 'none') return [set, []];
	for (const s of [set, 'default']) if (S.defaults[s]?.[role]?.length) return [s, S.defaults[s][role]];
	return [set, []];
}

const COLORS = [
	{ k: 'heart', name: 'Hearts', g: 'heart', def: '#c42a1f' }, { k: 'glint', name: 'Heart shine', g: 'heart', def: '#ece4de' },
	{ k: 'bang', name: 'Alert “!”', g: 'bang', def: '#f8c934' }, { k: 'anger', name: 'Anger mark', g: 'anger', def: '#db3329' },
	{ k: 'mark', name: '“?”', g: 'what', def: '#ece4de' }, { k: 'dots', name: '“…”', g: 'dots', def: '#ece4de' }, { k: 'note', name: 'Music notes', g: 'note', def: '#f8c934' },
	{ k: 'note2', name: 'Music notes 2', g: 'note', def: '#ece4de' }, { k: 'spark', name: 'Sparks', g: 'spark', def: '#ece4de' },
	{ k: 'z', name: "Sleep z's", g: 'z', def: '#ece4de' },
];
const GLYPHS = {
	z: ['####', '..#.', '.#..', '####'], heart: ['.##.##.', '#######', '#######', '.#####.', '..###..', '...#...'],
	bang: ['##', '##', '##', '##', '..', '##'], spark: ['..#..', '..#..', '##.##', '..#..', '..#..'],
	anger: ['.#...#.', '##...##', '.......', '.......', '##...##', '.#...#.'],
	what: ['.###.', '#...#', '...#.', '..#..', '.....', '..#..'], note: ['..###', '..#.#', '..#..', '..#..', '###..', '###..'],
	dots: ['##.##.##', '##.##.##'],
};

const TESTS = [
	['Alerts', [['follow', 'Someone follows'], ['sub', 'Someone subscribes'], ['gift:5', '5 gifted subs'], ['bits:100', '100 bits'],
		['raid:8', 'A raid'], ['pet', 'A channel-point pet']]],
	['Chat', [['mood:happy', 'Happy hop'], ['mood:love', 'Hearts'], ['mood:greet', 'Says hi'], ['mood:curious', 'Curious'],
		['mood:show_off', 'Shows off'], ['mood:play', 'Plays'], ['mood:zoomies', 'Zoomies'], ['mood:vibe', 'Bobs to music'],
		['mood:beg', 'Begs'], ['mood:sleepy', 'Naps'], ['mood:judge', 'Unimpressed'], ['mood:angry', 'Angry'],
		['mood:scared', 'Scared'], ['mood:swat_chat', 'Swats the chat']]],
];

// ------------------------------------------------------------------ little helpers

const $ = (s, el = document) => el.querySelector(s);
function h(tag, props, ...kids) {
	const el = document.createElement(tag);
	for (const [k, v] of Object.entries(props || {})) {
		if (v == null || v === false) continue;
		if (k === 'class') el.className = v;
		else if (k === 'text') el.textContent = v;
		else if (k === 'html') el.innerHTML = v;
		else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
		else if (k in el && typeof v !== 'string') el[k] = v;
		else el.setAttribute(k, v === true ? '' : v);
	}
	for (const c of kids.flat()) if (c != null && c !== false) el.append(c instanceof Node ? c : document.createTextNode(c));
	return el;
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const round1 = (v) => Math.round(v * 10) / 10;
const round2 = (v) => Math.round(v * 100) / 100;
function slug(s, max = 40) {
	return (s || '').toLowerCase().normalize('NFKD').replace(/[̀-ͯ]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, max) || 'pet';
}
function fileSlug(name) {
	return slug(name.replace(/\.[a-z0-9]+$/i, ''), 40).replace(/^-/, '') || 'sound';
}
function canvas(w, h) {
	const c = document.createElement('canvas');
	c.width = Math.max(1, Math.round(w));
	c.height = Math.max(1, Math.round(h));
	return c;
}
const ctx2d = (c) => {
	const x = c.getContext('2d', { willReadFrequently: true });
	x.imageSmoothingEnabled = false;
	return x;
};
const blobOf = (c) => new Promise((res) => c.toBlob(res, 'image/png'));
const naturalSort = (a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: 'base' });

let toastTimer;
function toast(msg, err = false) {
	const t = $('#toast');
	t.textContent = msg;
	t.className = 'toast' + (err ? ' err' : '');
	t.hidden = false;
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => (t.hidden = true), err ? 6000 : 3200);
}

// ------------------------------------------------------------------ the plugin

async function api(what, params = {}, init = {}) {
	const q = new URLSearchParams({ ...params, k: KEY });
	const r = await fetch(`/editor/api/${what}?${q}`, init);
	if (!r.ok) {
		let why = r.statusText;
		try { why = (await r.json()).error || why; } catch { /* not json */ }
		throw new Error(why);
	}
	return r;
}
const apiJson = async (what, params) => (await api(what, params)).json();
const put = (id, path, body) => api('put', { id, path }, { method: 'POST', body });
const post = (what, params) => api(what, params, { method: 'POST' });

// ------------------------------------------------------------------ state

const S = {
	packs: [], pets: 0,
	defaults: {},             // the plugin's sound sets: set -> role -> files
	id: null, info: null,
	editable: false,          // a pack made here, in the user's folder (or a built-in one: see builtin)
	builtin: false,           // built in: the first change saves it as the user's own copy (ownCopy)
	pack: null,               // pack.json
	strips: {},               // slot -> canvas: frames side by side
	srcs: {},                 // slot -> the image it was cut from, for "Change frames"
	tab: 'anims', sel: 'idle',
	show: null,               // "Hear it" playing: { k: the animation it shows, role }
	zoom: 4, turn: false, guides: false, bg: 0, ghost: false, scrub: -1,
	dirty: new Set(), dirtySheet: false, dirtyMeta: false,
};

function blankPack(name) {
	return {
		format: 1, name, describe: '', pronouns: '', personality: '', faces: 'right',
		anims: {}, stands: {}, poses: {}, body: { auto: true }, speed: null, sounds: {}, sound_set: 'default', colors: {}, ...lookDefaults(),
	};
}

// ------------------------------------------------------------------ geometry: strips -> cells

const meta = (k) => S.pack.anims[k] || S.pack.poses[k];
const has = (k) => !!(S.strips[k] && meta(k) && (S.pack.anims[k] || S.pack.poses[k].own));
// the animation another one stands in for it with (`angry = "scared"` in pack.lua), if it has none of its own
const standIn = (k) => (!has(k) && S.pack.stands?.[k] && S.pack.stands[k] !== k ? S.pack.stands[k] : null);
const hasOrStand = (k) => has(k) || !!standIn(k);
function resolveAnim(k) {
	let x = k;
	for (let i = 0; x && !has(x) && i < 2 * SHEET_ORDER.length; i++) x = standIn(x) || FALLBACK[x];
	return x && has(x) ? x : null;
}
// the strip and frame a pose shows
function poseFrame(k) {
	let x = k;
	for (let i = 0; i < 6 && x; i++) {
		if (x === 'idle') break;
		const p = S.pack.poses[x];
		if (p && p.own && S.strips[x]) return { k: x, i: 0 };
		if (p && p.anim && has(p.anim)) return { k: p.anim, i: clamp(p.frame | 0, 0, geom(p.anim).n - 1) };
		x = POSES.find((q) => q.k === x).fb;
	}
	return has('idle') ? { k: 'idle', i: 0 } : null;
}
function geom(k) {
	const m = meta(k), c = S.strips[k];
	const n = Math.max(1, (S.pack.anims[k] ? m.frames : 1) | 0);
	return { n, fw: Math.floor(c.width / n), fh: c.height, dx: m.dx | 0, dy: m.dy | 0, flip: !!m.flip };
}
function slotsDrawn() {
	return [...SHEET_ORDER.filter(has), ...POSES.map((p) => p.k).filter(has)];
}
// The Pet scales its motion (jumps, walking) by the cell height, tuned for a 32 px cell holding a
// body about 15 px tall, so the cell gets headroom to match the idle drawing's height.
let tall = null;
function idleTall() {
	if (tall && tall.version === version) return tall.v;
	const b = has('idle') && alphaBox(S.strips.idle, 0, 0, S.strips.idle.width, S.strips.idle.height);
	tall = { version, v: b ? b.maxY - b.minY + 1 : 0 };
	return tall.v;
}
function cellSize() {
	let cw = 8, ch = 8;
	for (const k of slotsDrawn()) {
		const g = geom(k);
		cw = Math.max(cw, g.fw + 2 * Math.abs(g.dx));
		ch = Math.max(ch, g.fh + Math.max(0, g.dy));
	}
	ch = Math.max(ch, Math.round((idleTall() * 32) / 15));
	return { cw: cw + (cw % 2), ch };
}
// frame i of slot k into the cell whose top-left is (x, y), z screen pixels per sprite pixel.
// The drawings as they are (or `src`, the same strip recolored): the sheet is built with this, and
// the plugin changes the colors itself.
function drawCell(ctx, k, i, x, y, z, cw, ch, clip = true, src = null) {
	const g = geom(k), c = src || S.strips[k];
	const ox = Math.floor((cw - g.fw) / 2) + g.dx, oy = ch - g.fh - g.dy;
	ctx.save();
	if (clip) {
		ctx.beginPath();
		ctx.rect(x, y, cw * z, ch * z);
		ctx.clip();
	}
	if (g.flip) {
		ctx.translate(x + (ox + g.fw / 2) * z, 0);
		ctx.scale(-1, 1);
		ctx.drawImage(c, i * g.fw, 0, g.fw, g.fh, (-g.fw / 2) * z, y + oy * z, g.fw * z, g.fh * z);
	} else {
		ctx.drawImage(c, i * g.fw, 0, g.fw, g.fh, x + ox * z, y + oy * z, g.fw * z, g.fh * z);
	}
	ctx.restore();
}

// a frame as the Pet shows it: with the pack's look (hue, tint, swapped colors...). Only for
// showing: never for the sheet that's saved (the plugin changes the colors as it loads it).
function drawPet(ctx, k, i, x, y, z, cw, ch) {
	drawCell(ctx, k, i, x, y, z, cw, ch, true, lookStrip(k));
}
// -> the pack's hue shift in degrees, -180..180 (0: the colors as drawn)
function hueNow() {
	const v = +(S.pack && S.pack.hue) || 0;
	return clamp(Math.round(v), -180, 180);
}

// ------------------------------------------------------------------ the look: the pet's own colors
// The same steps, in the same order, as sheet_texture() in the plugin (pet-source.cpp): exact colors
// swapped, then hue, saturation, contrast, lightness, and a tint multiplied in. Alpha stays.

const HEX6 = /^#[0-9a-f]{6}$/i;
function lookDefaults() {
	return { hue: 0, saturation: 1, contrast: 1, lightness: 0, tint: '#ffffff', tint_amount: 0, recolor: {} };
}
function lookNow() {
	const p = S.pack || {};
	const n = (v, lo, hi, d) => (v == null || v === '' || !Number.isFinite(+v) ? d : clamp(+v, lo, hi));
	const recolor = {};
	for (const [a, b] of Object.entries(p.recolor || {}))
		if (HEX6.test(a) && HEX6.test(b) && a.toLowerCase() !== b.toLowerCase()) recolor[a.toLowerCase()] = b.toLowerCase();
	return {
		hue: hueNow(), saturation: n(p.saturation, 0, 2, 1), contrast: n(p.contrast, 0, 2, 1), lightness: n(p.lightness, -1, 1, 0),
		tint: HEX6.test(p.tint || '') ? p.tint.toLowerCase() : '#ffffff', tint_amount: n(p.tint_amount, 0, 1, 0), recolor,
	};
}
const lookAny = (L) => !!(L.hue || L.saturation !== 1 || L.contrast !== 1 || L.lightness || L.tint_amount > 0 || Object.keys(L.recolor).length);
const hexOf = (r, g, b) => '#' + ((1 << 24) | (r << 16) | (g << 8) | b).toString(16).slice(1);
function lookPixels(d, L) {
	const r = (L.hue * Math.PI) / 180, a = Math.cos(r), b = Math.sin(r);
	const m = [
		[0.213 + 0.787 * a - 0.213 * b, 0.715 - 0.715 * a - 0.715 * b, 0.072 - 0.072 * a + 0.928 * b],
		[0.213 - 0.213 * a + 0.143 * b, 0.715 + 0.285 * a + 0.140 * b, 0.072 - 0.072 * a - 0.283 * b],
		[0.213 - 0.213 * a - 0.787 * b, 0.715 - 0.715 * a + 0.715 * b, 0.072 + 0.928 * a + 0.072 * b],
	];
	const t = [1, 3, 5].map((i) => parseInt(L.tint.slice(i, i + 2), 16) / 255);
	const swap = new Map(Object.entries(L.recolor).map(([x, y]) => [parseInt(x.slice(1), 16), parseInt(y.slice(1), 16)]));
	const c = [0, 0, 0], o = [0, 0, 0];
	for (let i = 0; i < d.length; i += 4) {
		if (!d[i + 3]) continue;
		let key = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
		if (swap.size && swap.has(key)) key = swap.get(key);
		c[0] = (key >> 16) / 255;
		c[1] = ((key >> 8) & 255) / 255;
		c[2] = (key & 255) / 255;
		if (L.hue) {
			for (let k = 0; k < 3; k++) o[k] = m[k][0] * c[0] + m[k][1] * c[1] + m[k][2] * c[2];
			c[0] = o[0]; c[1] = o[1]; c[2] = o[2];
		}
		if (L.saturation !== 1) {
			const y = 0.213 * c[0] + 0.715 * c[1] + 0.072 * c[2];
			for (let k = 0; k < 3; k++) c[k] = y + (c[k] - y) * L.saturation;
		}
		for (let k = 0; k < 3; k++) {
			let v = clamp(c[k], 0, 1);
			v = clamp((v - 0.5) * L.contrast + 0.5, 0, 1);
			v = L.lightness >= 0 ? v + (1 - v) * L.lightness : v * (1 + L.lightness);
			v = v * (1 - L.tint_amount) + v * t[k] * L.tint_amount;
			d[i + k] = clamp(Math.round(v * 255), 0, 255);
		}
	}
}
// slot k's strip with the look applied (the strip itself when there is nothing to change)
const looked = new WeakMap();
function lookStrip(k) {
	const c = S.strips[k], L = lookNow();
	if (!c || !lookAny(L)) return c;
	const key = JSON.stringify(L) + '|' + version;
	const e = looked.get(c);
	if (e && e.key === key) return e.canvas;
	const out = canvas(c.width, c.height), x = ctx2d(out);
	x.drawImage(c, 0, 0);
	const img = x.getImageData(0, 0, out.width, out.height);
	lookPixels(img.data, L);
	x.putImageData(img, 0, 0);
	looked.set(c, { key, canvas: out });
	return out;
}
// the colors the drawings use, most pixels first: [{ hex, n }]
function drawnColors() {
	const count = new Map();
	for (const k of slotsDrawn()) {
		const c = S.strips[k];
		const d = ctx2d(c).getImageData(0, 0, c.width, c.height).data;
		for (let i = 0; i < d.length; i += 4) {
			if (d[i + 3] < 128) continue;
			const key = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
			count.set(key, (count.get(key) || 0) + 1);
		}
	}
	return [...count].sort((a, b) => b[1] - a[1]).map(([key, n]) => ({ hex: hexOf(key >> 16, (key >> 8) & 255, key & 255), n }));
}

function alphaBox(img, x0, y0, w, h) {
	const d = ctx2d(img).getImageData(x0, y0, w, h).data;
	let minX = w, minY = h, maxX = -1, maxY = -1;
	for (let y = 0; y < h; y++)
		for (let x = 0; x < w; x++)
			if (d[(y * w + x) * 4 + 3] > 16) {
				if (x < minX) minX = x;
				if (x > maxX) maxX = x;
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
	return maxX < 0 ? null : { minX, minY, maxX, maxY };
}
// where the drawings sit in the cell: the union of every frame of `k`
function slotBox(k, frames) {
	const { cw, ch } = cellSize();
	const g = geom(k);
	const tmp = canvas(cw, ch), x = ctx2d(tmp);
	let box = null;
	for (const i of frames || [...Array(g.n).keys()]) {
		x.clearRect(0, 0, cw, ch);
		drawCell(x, k, i, 0, 0, 1, cw, ch);
		const b = alphaBox(tmp, 0, 0, cw, ch);
		if (!b) continue;
		box = box ? { minX: Math.min(box.minX, b.minX), minY: Math.min(box.minY, b.minY), maxX: Math.max(box.maxX, b.maxX), maxY: Math.max(box.maxY, b.maxY) } : b;
	}
	return box;
}

// feet: empty rows under the idle drawing; body: measured unless the user set it
let measured = null, version = 0;
function measure() {
	if (measured && measured.version === version) return measured.out;
	const out = measureNow();
	measured = { version, out };
	return out;
}
function measureNow() {
	const { cw, ch } = cellSize();
	const out = { feet: 0, body: { half: 9, top: 15, top_asleep: 10, head: 3, paw: [9, 8], shadow: 6 }, cw, ch };
	if (!has('idle')) return out;
	const ib = slotBox('idle');
	if (!ib) return out;
	const ground = ib.maxY + 1, cx = cw / 2;
	out.feet = ch - ground;
	const right = S.pack.faces !== 'left';
	const half = Math.max(2, Math.round((ib.maxX - ib.minX + 1) / 2));
	const front = right ? ib.maxX + 1 - cx : cx - ib.minX;
	const top = Math.max(2, ground - ib.minY);
	let asleep = Math.round(top * 0.67);
	const sb = has('sleep') && slotBox('sleep');
	if (sb) asleep = Math.max(2, ground - sb.minY);
	let paw = [Math.round(front), Math.round(top * 0.5)];
	if (has('paw')) {
		const m = S.pack.anims.paw;
		const pb = slotBox('paw', [clamp(m.hit ?? 0, 0, geom('paw').n - 1)]);
		if (pb) paw = [Math.round(right ? pb.maxX + 1 - cx : cx - pb.minX), Math.round(top * 0.5)];
	}
	out.body = { half, top, top_asleep: asleep, head: Math.max(0, Math.round(front - half * 0.7)), paw, shadow: Math.max(2, Math.round(half * 0.67)) };
	return out;
}
function bodyNow() {
	const m = measure();
	const b = S.pack.body || {};
	return { ...m, body: b.auto === false ? { ...m.body, ...b, paw: b.paw || m.body.paw } : m.body };
}
function speedNow(ch) {
	const unit = (ch || cellSize().ch) / 32;
	const def = { walk: Math.round(24 * unit), run: Math.round(70 * unit) };
	return { def, ...(S.pack.speed ? { walk: S.pack.speed.walk ?? def.walk, run: S.pack.speed.run ?? def.run } : def) };
}

// ------------------------------------------------------------------ making the files the Pet loads

function buildSheet() {
	const { cw, ch } = cellSize();
	const rows = SHEET_ORDER.filter(has);
	const ownPoses = POSES.map((p) => p.k).filter(has);
	const cols = Math.max(1, ownPoses.length, ...rows.map((k) => geom(k).n));
	const nrows = rows.length + (ownPoses.length ? 1 : 0);
	const c = canvas(cw * cols, ch * Math.max(1, nrows)), x = ctx2d(c);
	const where = { anims: {}, poses: {} };
	rows.forEach((k, r) => {
		for (let i = 0; i < geom(k).n; i++) drawCell(x, k, i, i * cw, r * ch, 1, cw, ch);
		where.anims[k] = r;
	});
	ownPoses.forEach((k, i) => {
		drawCell(x, k, 0, i * cw, rows.length * ch, 1, cw, ch);
		where.poses[k] = [i, rows.length];
	});
	return { canvas: c, where, cw, ch };
}

// A Lua string: every quote, backslash and control character escaped (an imported pack.json can hold anything)
const luaStr = (s) => '"' + String(s ?? '').replace(/[\\"\x00-\x1f\x7f]/g, (c) => '\\' + String(c.charCodeAt(0)).padStart(3, '0')) + '"';
// A Lua number: always a plain number, never text from pack.json written into the file as code
const luaNum = (v, d = 0) => (Number.isFinite(+v) ? +v : d);
function buildLua(where, cw, ch) {
	const p = S.pack, m0 = bodyNow(), sp0 = speedNow(ch);
	const m = { ...m0, feet: luaNum(m0.feet), body: Object.fromEntries(['half', 'top', 'top_asleep', 'head', 'shadow'].map((k) => [k, luaNum(m0.body[k])])) };
	m.body.paw = [luaNum(m0.body.paw?.[0]), luaNum(m0.body.paw?.[1])];
	const sp = { walk: luaNum(sp0.walk, sp0.def.walk), run: luaNum(sp0.run, sp0.def.run) };
	const L = [];
	L.push('-- Made with the OBSPETS character editor (OBSPETS Settings, Characters).');
	L.push('-- Change it there: the editor writes this file again from pack.json every time it saves.');
	L.push('return {');
	L.push(`\tname = ${luaStr(p.name)},`);
	if (p.describe) L.push(`\tdescribe = ${luaStr(p.describe)},`);
	if (p.pronouns) L.push(`\tpronouns = ${luaStr(p.pronouns)},`);
	if (p.personality) L.push(`\tpersonality = ${luaStr(p.personality)},`);
	L.push('\tsheet = "sheet.png",');
	L.push(`\tcell = { ${cw}, ${ch} },`);
	L.push(`\tfaces = ${luaStr(p.faces === 'left' ? 'left' : 'right')},`);
	L.push(`\tfeet = ${m.feet},`);
	const look = lookNow();
	if (look.hue) L.push(`\thue = ${look.hue},   -- degrees its colors turn`);
	if (look.saturation !== 1) L.push(`\tsaturation = ${round2(look.saturation)},   -- 0 grey, 1 as drawn, 2 twice as vivid`);
	if (look.contrast !== 1) L.push(`\tcontrast = ${round2(look.contrast)},`);
	if (look.lightness) L.push(`\tlightness = ${round2(look.lightness)},   -- -1 black .. 1 white`);
	if (look.tint_amount > 0) L.push(`\ttint = ${luaStr(look.tint)}, tint_amount = ${round2(look.tint_amount)},   -- multiplied in: colors a white pet`);
	const swaps = Object.entries(look.recolor);
	if (swaps.length) L.push(`\trecolor = { ${swaps.map(([a, b]) => `["${a}"] = "${b}"`).join(', ')} },`);
	L.push('\tanims = {');
	for (const k of SHEET_ORDER) {
		if (!(k in where.anims)) continue;
		const a = p.anims[k], n = geom(k).n;
		let s = `\t\t${k} = { row = ${luaNum(where.anims[k])}, frames = ${luaNum(n, 1)}, fps = ${luaNum(round1(a.fps || 8), 8)}`;
		if (k === 'paw') s += `, whoosh = ${clamp(a.whoosh | 0, 0, n - 1)}, hit = ${clamp(a.hit | 0, 0, n - 1)}`;
		L.push(s + ' },');
	}
	for (const k of SHEET_ORDER) {
		const to = !(k in where.anims) && p.stands?.[k];
		if (to && SHEET_ORDER.includes(to)) L.push(`		${k} = ${luaStr(to)},   -- stands in`);
	}
	L.push('\t},');
	L.push('\tposes = {');
	for (const { k } of POSES) {
		const q = p.poses[k];
		if (k in where.poses) L.push(`\t\t${k} = { row = ${where.poses[k][1]}, col = ${where.poses[k][0]} },`);
		else if (q && q.anim && q.anim in where.anims) L.push(`\t\t${k} = { row = ${where.anims[q.anim]}, col = ${clamp(q.frame | 0, 0, geom(q.anim).n - 1)} },`);
	}
	L.push('\t},');
	const b = m.body;
	L.push(`\tbody = { half = ${b.half}, top = ${b.top}, top_asleep = ${b.top_asleep}, head = ${b.head}, paw = { ${b.paw[0]}, ${b.paw[1]} }, shadow = ${b.shadow} },`);
	L.push(`\tspeed = { walk = ${sp.walk}, run = ${sp.run} },`);
	if (soundSet() !== 'default') L.push(`\tsound_set = ${luaStr(soundSet())},`);
	L.push('\tsounds = {');
	for (const { k } of ROLES) {
		const v = p.sounds[k];
		if (v === false) L.push(`\t\t${k} = false,`);
		else if (typeof v === 'string' && v.startsWith('@')) L.push(`\t\t${k} = ${luaStr(v.slice(1))},`);
		else if (Array.isArray(v) && v.length) L.push(`\t\t${k} = { ${v.map((f) => luaStr(`sounds/${k}/${f}`)).join(', ')} },`);
	}
	L.push('\t},');
	const cols = COLORS.filter((c) => p.colors[c.k]).map((c) => `${c.k} = ${luaStr(p.colors[c.k])}`);
	if (cols.length) L.push(`\tcolors = { ${cols.join(', ')} },`);
	L.push('}');
	return L.join('\n') + '\n';
}

// ------------------------------------------------------------------ saving

let saveTimer = null, saving = null;
function changed(sheet = true) {
	version++;
	if (!S.editable) return;
	S.dirtyMeta = true;
	if (sheet) S.dirtySheet = true;
	setSaved('busy', 'Unsaved changes');
	clearTimeout(saveTimer);
	saveTimer = setTimeout(save, 500);
	paint();
}
function setSaved(cls, text) {
	const el = $('#edSaved');
	el.className = 'saved ' + (cls || '');
	el.textContent = text || '';
}
async function save() {
	if (!S.editable) return;
	if (saving) {
		await saving;
		return save();
	}
	if (!S.dirtyMeta && !S.dirty.size && !S.dirtySheet) return;
	if (S.builtin) {
		saving = ownCopy();
		await saving;
		saving = null;
		return;
	}
	const id = S.id;
	saving = (async () => {
		setSaved('busy', 'Saving…');
		try {
			const files = [...S.dirty];
			S.dirty.clear();
			for (const k of files) {
				if (S.strips[k] && meta(k)) await put(id, `anims/${k}.png`, await blobOf(S.strips[k]));
				else await post('del', { id, path: `anims/${k}.png` });
			}
			const sheet = S.dirtySheet;
			S.dirtySheet = S.dirtyMeta = false;
			await saveSource(id);
			const b = buildSheet();
			if (sheet && has('idle')) await put(id, 'sheet.png', await blobOf(b.canvas));
			await put(id, 'pack.lua', buildLua(b.where, b.cw, b.ch));
			await put(id, 'pack.json', JSON.stringify(S.pack, null, '\t'));
			await post('done', { id });
			const shown = S.packs.find((p) => p.id === id)?.on_stream;
			setSaved('', shown ? 'Saved · live on your Pet' : 'Saved');
		} catch (e) {
			setSaved('err', 'Not saved');
			toast("Couldn't save: " + e.message + '. Is OBS still open?', true);
		}
	})();
	await saving;
	saving = null;
}

// ------------------------------------------------------------------ loading

function imageFrom(blob) {
	return new Promise((res, rej) => {
		const u = URL.createObjectURL(blob);
		const im = new Image();
		im.onload = () => {
			const c = canvas(im.naturalWidth, im.naturalHeight);
			ctx2d(c).drawImage(im, 0, 0);
			URL.revokeObjectURL(u);
			res(c);
		};
		im.onerror = () => {
			URL.revokeObjectURL(u);
			rej(new Error('not an image'));
		};
		im.src = u;
	});
}

async function refreshPacks() {
	const r = await apiJson('packs');
	S.packs = r.packs;
	S.pets = r.pets;
	renderPackOptions();
}
function renderPackOptions() {
	const sel = $('#pack');
	sel.replaceChildren(
		...S.packs.map((p) => {
			const notes = [p.user ? '' : p.folder ? 'from a folder' : 'built in', p.on_stream ? 'in OBS' : ''].filter(Boolean);
			const name = p.id === S.id && S.pack ? S.pack.name : p.name;
			return h('option', { value: p.id, text: name + (notes.length ? ` (${notes.join(', ')})` : '') });
		}),
	);
	if (S.id) sel.value = S.id;
	updateUseButton();
}
function updateUseButton() {
	const p = S.packs.find((x) => x.id === S.id);
	$('#use').hidden = !p || p.on_stream || !S.pets || !has('idle');
}

async function openPack(id) {
	if (saving) await saving;
	clearTimeout(saveTimer);
	await save();
	S.id = id;
	S.strips = {};
	S.srcs = {};
	S.sheetMap = null;
	S.sourceDirty = false;
	S.dirty.clear();
	S.dirtySheet = S.dirtyMeta = false;
	S.scrub = -1;
	stopHearing();
	stopSound();
	setHash(id);
	$('#pack').value = id;
	setSaved('', '');
	let info;
	try {
		info = await apiJson('pack', { id });
	} catch (e) {
		toast("Couldn't open it: " + e.message, true);
		return;
	}
	S.info = info;
	S.builtin = false;
	if (info.editor && info.user) {
		S.editable = true;
		S.pack = Object.assign(blankPack(info.name || id), info.editor);
		for (const k of [...Object.keys(S.pack.anims), ...Object.keys(S.pack.poses).filter((k) => S.pack.poses[k].own)]) {
			try {
				S.strips[k] = await imageFrom(await (await api('file', { id, path: `anims/${k}.png` })).blob());
			} catch {
				delete S.pack.anims[k];
				delete S.pack.poses[k];
			}
		}
		await loadSource(id);
	} else {
		await convert(info);
		// built in: editable, and the first change saves it as your own copy (ownCopy)
		S.builtin = !info.user && !info.error;
		S.editable = S.builtin;
	}
	if (!has(S.sel) && !S.pack.poses[S.sel]) S.sel = 'idle';
	fitZoom();
	showBanner();
	renderPackOptions();
	renderAll();
}

// a pack written by hand (pack.lua + one sheet) -> strips, so it shows and can be copied
async function convert(info) {
	const p = blankPack(info.name || info.id);
	S.pack = p;
	if (info.error) return;
	Object.assign(p, { describe: info.describe, pronouns: info.pronouns, personality: info.personality, faces: info.faces });
	p.colors = { ...info.colors };
	for (const k of ['hue', 'saturation', 'contrast', 'lightness', 'tint_amount']) if (info[k] != null && Number.isFinite(+info[k])) p[k] = +info[k];
	if (HEX6.test(info.tint || '')) p.tint = info.tint;
	p.recolor = { ...(info.recolor || {}) };
	let sheet;
	try {
		sheet = await imageFrom(await (await api('sheet', { id: info.id })).blob());
	} catch {
		return;
	}
	const [cw, ch] = info.cell;
	const cut = (cells) => {
		const c = canvas(cw * cells.length, ch), x = ctx2d(c);
		cells.forEach(([col, row], i) => x.drawImage(sheet, col * cw, row * ch, cw, ch, i * cw, 0, cw, ch));
		return c;
	};
	const cellKey = {};
	p.stands = { ...(info.stands || {}) };
	for (const [k, a] of Object.entries(info.anims)) {
		S.strips[k] = cut(a.cells);
		p.anims[k] = { frames: a.cells.length, fps: a.fps, dx: 0, dy: 0 };
		if (k === 'paw') Object.assign(p.anims[k], { whoosh: a.whoosh, hit: a.hit });
		a.cells.forEach(([c, r], i) => (cellKey[c + ',' + r] ??= { anim: k, frame: i }));
	}
	for (const [k, [c, r]] of Object.entries(info.poses)) {
		const hit = cellKey[c + ',' + r];
		if (hit) p.poses[k] = { ...hit };
		else {
			S.strips[k] = cut([[c, r]]);
			p.poses[k] = { own: true, dx: 0, dy: 0 };
		}
	}
	// measured from the animations, like a character made here (one click keeps the pack's own instead)
	p.body = { auto: true };
	p.speed = { ...info.speed };
	p.sound_set = info.sound_set || 'default';
	// sounds: a role with the same files as an earlier one shares them
	const seen = [];
	for (const { k } of ROLES) {
		const files = info.sounds[k];
		if (!files || !files.length) {
			if (info.default_sounds && !info.default_sounds.includes(k) && builtin(k, p.sound_set)[1].length) p.sounds[k] = false;
			continue;
		}
		const same = seen.find((s) => s.files.join('|') === files.join('|'));
		if (same) p.sounds[k] = '@' + same.k;
		else {
			p.sounds[k] = files.map((f, i) => ({ from: { role: k, i }, name: f }));
			seen.push({ k, files });
		}
	}
}

function showBanner() {
	const b = $('#edBanner');
	const info = S.info;
	b.replaceChildren();
	b.hidden = S.editable && !S.builtin;
	if (b.hidden) return;
	// (the settings page's banner: a title line, a muted line under it, buttons on the right)
	const body = (title, sub) => h('div', { class: 'banner-body' }, h('div', { class: 'banner-title', text: title }), h('div', { class: 'banner-sub', text: sub }));
	const btns = (...kids) => h('div', { class: 'banner-actions' }, ...kids);
	b.className = 'banner' + (info.error ? ' err' : '');
	if (S.builtin && S.packs.find((p) => p.id === S.id)?.folder) {
		b.append(body(`${S.pack.name} comes from a folder your Pet uses`, "Change anything and it's saved as your own copy. The folder stays as it was."));
	} else if (S.builtin) {
		b.append(body(`${S.pack.name} comes with OBSPETS`, "Change anything and it's saved as your own copy. The built-in one stays as it was."));
	} else if (info.error) {
		b.append(body("This character doesn't load", info.error));
	} else if (!info.user) {
		b.append(body(`${S.pack.name} comes with OBSPETS`, 'Make your own copy to change it.'),
			btns(h('button', { class: 'primary small', text: 'Make a copy', onclick: () => makeCopy() })));
	} else {
		b.append(body(`${S.pack.name} was written by hand (pack.lua)`, 'Edit it here and the editor takes it over; its old files are kept in its .history folder.'),
			btns(h('button', { class: 'small', text: 'Make a copy', onclick: () => makeCopy() }),
				h('button', { class: 'primary small', text: 'Edit it here', onclick: () => takeOver() })));
	}
}
const escapeHtml = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// ------------------------------------------------------------------ new, copy, take over, delete, export, import

function freeId(name) {
	const base = slug(name);
	let id = base, n = 2;
	while (S.packs.some((p) => p.id === id)) id = `${base}-${n++}`.slice(0, 40);
	return id;
}

function freeName(name) {
	const base = name.replace(/ \d+$/, '');
	let n = 2;
	while (S.packs.some((p) => p.name.toLowerCase() === `${base} ${n}`.toLowerCase())) n++;
	return `${base} ${n}`;
}

async function askName(title, sub, def, okText) {
	return new Promise((res) => {
		const inp = h('input', { value: def, maxlength: 40 });
		const done = (v) => {
			closeModal();
			res(v);
		};
		openModal(
			h('h2', { text: title }), h('p', { class: 'sub', text: sub }),
			h('label', { class: 'form', style: 'padding:0' }, inp),
			h('div', { class: 'actions' }, h('button', { text: 'Cancel', onclick: () => done(null) }),
				h('button', { class: 'primary', text: okText, onclick: () => inp.value.trim() && done(inp.value.trim()) })),
		);
		inp.addEventListener('keydown', (e) => e.key === 'Enter' && inp.value.trim() && done(inp.value.trim()));
		setTimeout(() => inp.select(), 30);
	});
}

async function newPack() {
	await save();
	const name = await askName('A new character', 'What is it called? You can change this later.', '', 'Create');
	if (!name) return;
	await refreshPacks();
	const id = freeId(name);
	S.id = id;
	S.pack = blankPack(name);
	S.strips = {};
	S.editable = true;
	S.builtin = false;
	S.info = { id, user: true };
	await writeAll(id, {});
	await refreshPacks();
	await openPack(id);
	toast('Now drop in your sprite sheet, or an idle animation: a PNG strip or a GIF.');
}

// every file of the open character into pack `id` (sounds from `sources`: name -> Blob)
async function writeAll(id, sources) {
	const keys = slotsDrawn();
	let n = 0;
	setSaved('busy', 'Copying…');
	for (const k of keys) await put(id, `anims/${k}.png`, await blobOf(S.strips[k]));
	for (const [role, v] of Object.entries(S.pack.sounds)) {
		if (!Array.isArray(v)) continue;
		const names = [];
		for (const f of v) {
			const src = typeof f === 'string' ? f : f.name;
			const blob = sources[role + '/' + src];
			if (!blob) continue;
			const name = uniqueName(names, fileSlug(src));
			await put(id, `sounds/${role}/${name}.wav`, await asWav(blob));
			names.push(name);
			n++;
		}
		S.pack.sounds[role] = names.map((x) => x + '.wav');
		if (!names.length) delete S.pack.sounds[role];
	}
	await saveSource(id, true);
	const b = buildSheet();
	if (has('idle')) await put(id, 'sheet.png', await blobOf(b.canvas));
	await put(id, 'pack.lua', buildLua(b.where, b.cw, b.ch));   // (before pack.json, so a hand-written one goes to .history)
	await put(id, 'pack.json', JSON.stringify(S.pack, null, '\t'));
	await post('done', { id });
	S.dirty.clear();
	S.dirtySheet = S.dirtyMeta = false;
	setSaved('', 'Saved');
	return n;
}
function uniqueName(taken, base) {
	let n = base, i = 2;
	while (taken.includes(n)) n = `${base}-${i++}`;
	return n;
}
async function asWav(blob) {
	const head = new Uint8Array(await blob.slice(0, 12).arrayBuffer());
	const riff = String.fromCharCode(...head.slice(0, 4)) === 'RIFF' && String.fromCharCode(...head.slice(8, 12)) === 'WAVE';
	if (riff) return blob;
	const buf = await to48k(await decodeAudio(await blob.arrayBuffer()));
	return encodeWav(buf);
}
// the sounds of the open character, as Blobs keyed "role/file"
async function currentSounds() {
	const out = {};
	for (const [role, v] of Object.entries(S.pack.sounds)) {
		if (!Array.isArray(v)) continue;
		for (const f of v) {
			const name = typeof f === 'string' ? f : f.name;
			try {
				const r = typeof f === 'string' ? await api('file', { id: S.id, path: `sounds/${role}/${f}` }) : await api('sound', { id: S.id, role: f.from.role, i: f.from.i });
				out[role + '/' + name] = await r.blob();
			} catch { /* gone */ }
		}
	}
	return out;
}

async function makeCopy() {
	const name = await askName('Make a copy', 'Your copy goes in your own characters folder, where you can change everything.', freeName(S.pack.name), 'Make the copy');
	if (!name) return;
	try {
		await save();
		const sounds = await currentSounds();
		await refreshPacks();
		const id = freeId(name);
		S.pack = JSON.parse(JSON.stringify({ ...S.pack, name }));
		await writeAll(id, sounds);
		await refreshPacks();
		await openPack(id);
		toast(`${name} is yours to change.`);
	} catch (e) {
		toast("Couldn't copy it: " + e.message, true);
	}
}
// A built-in character can't be changed where it is (it's part of the plugin, which only an admin
// can write to, and an update replaces it). So the first change makes your own copy, under the same
// name, and that is what you go on editing; a Pet that showed the built-in one shows the copy.
async function ownCopy() {
	const from = S.id, name = S.pack.name, v0 = version;
	try {
		setSaved('busy', 'Saving as your own copy…');
		const sounds = await currentSounds();
		await refreshPacks();
		const wasOn = S.packs.find((p) => p.id === from)?.on_stream;
		const id = freeId(name);
		await writeAll(id, sounds);
		S.id = id;
		S.builtin = false;
		S.info = { ...S.info, id, user: true, editor: true };
		setHash(id);
		if (wasOn) await post('use', { id });
		await refreshPacks();
		showBanner();
		renderSounds();   // (its sounds are files of its own now; colors aren't redrawn, so an open picker stays open)
		toast(`Saved as your own ${name}. The built-in one stays as it was.`);
		setSaved('', wasOn ? 'Saved · live on your Pet' : 'Saved');
	} catch (e) {
		setSaved('err', 'Not saved');
		toast("Couldn't save your copy: " + e.message + '. Is OBS still open?', true);
		return;
	}
	if (version !== v0) {   // changed again while it was copying: save that too
		S.dirtyMeta = S.dirtySheet = true;
		clearTimeout(saveTimer);
		saveTimer = setTimeout(save, 300);
	}
}
// ------------------------------------------------------------------ editing a drawing elsewhere

// "Show file" opens the slot's PNG strip (anims/<slot>.png, its frames side by side) selected in its
// folder, to edit in any art program. A built-in character is first saved as your own copy, since
// that's the file you can change. When the editor gets focus again and the file changed, it takes
// the new drawing (more or fewer frames of the same width included) and rebuilds the sheet.
let watching = null;   // { id, k, path, sig }
async function fileSig(blob) {
	const b = new Uint8Array(await blob.arrayBuffer());
	let x = 0;
	for (let i = 0; i < b.length; i++) x = (x * 31 + b[i]) | 0;
	return b.length + ':' + x;
}
async function showFile(k) {
	try {
		if (S.builtin) {
			S.dirtyMeta = S.dirtySheet = true;
			clearTimeout(saveTimer);
			await save();
			if (S.builtin) return;   // (the copy failed: save() said why)
		}
		if (S.dirty.size || S.dirtyMeta || S.dirtySheet || saving) {
			clearTimeout(saveTimer);
			await save();
		}
		const path = `anims/${k}.png`;
		const blob = await (await api('file', { id: S.id, path })).blob();
		watching = { id: S.id, k, path, sig: await fileSig(blob) };
		await post('reveal', { id: S.id, path });
		toast(`Opened its folder with ${k}.png selected. Edit it, save it, and come back: the editor picks up the change.`);
	} catch (e) {
		toast("Couldn't show the file: " + e.message, true);
	}
}
async function checkWatched() {
	const w = watching;
	if (!w || w.id !== S.id || !S.strips[w.k] || saving) return;
	let blob;
	try {
		blob = await (await api('file', { id: w.id, path: w.path })).blob();
	} catch {
		return;
	}
	const sig = await fileSig(blob);
	if (sig === w.sig) return;
	w.sig = sig;
	let c;
	try {
		c = await imageFrom(blob);
	} catch {
		return toast(`${w.k}.png isn't a PNG the editor can read any more.`, true);
	}
	const old = geom(w.k), m = meta(w.k);
	if (S.pack.anims[w.k] && c.width !== S.strips[w.k].width && c.width % old.fw === 0) m.frames = c.width / old.fw;
	S.strips[w.k] = c;
	delete S.srcs[w.k];
	S.scrub = -1;
	S.dirty.add(w.k);
	changed();
	renderInspector();
	toast(`Took the new ${w.k}.png.`);
}
window.addEventListener('focus', checkWatched);

async function ownFirst() {
	if (!S.builtin) return true;
	if (saving) await saving;
	if (S.builtin) {
		saving = ownCopy();
		await saving;
		saving = null;
	}
	return !S.builtin;
}

async function takeOver() {
	try {
		const sounds = await currentSounds();
		const id = S.id;
		S.pack = JSON.parse(JSON.stringify(S.pack));
		await writeAll(id, sounds);
		await refreshPacks();
		await openPack(id);
		toast('You can change it here now.');
	} catch (e) {
		toast("Couldn't take it over: " + e.message, true);
	}
}
async function removePack() {
	const p = S.packs.find((x) => x.id === S.id);
	if (!p || !p.user) return toast("Built-in characters can't be deleted.", true);
	const ok = await confirmBox(`Delete ${S.pack.name}?`, 'It moves to the .trash folder in your characters folder, so you can get it back for 30 days.', 'Delete');
	if (!ok) return;
	try {
		await post('remove', { id: S.id });
		await refreshPacks();
		S.id = null;
		await openPack((S.packs.find((x) => x.user) || S.packs[0]).id);
	} catch (e) {
		toast("Couldn't delete it: " + e.message, true);
	}
}
function confirmBox(title, sub, okText) {
	return new Promise((res) => {
		const done = (v) => {
			closeModal();
			res(v);
		};
		openModal(h('h2', { text: title }), h('p', { class: 'sub', text: sub }),
			h('div', { class: 'actions' }, h('button', { text: 'Cancel', onclick: () => done(false) }),
				h('button', { class: 'primary', text: okText, onclick: () => done(true) })));
	});
}

async function exportPack() {
	try {
		await save();
		const files = [];
		const add = async (name, blob) => files.push({ name, data: new Uint8Array(await blob.arrayBuffer()) });
		const b = buildSheet();
		for (const k of slotsDrawn()) await add(`anims/${k}.png`, await blobOf(S.strips[k]));
		const sounds = await currentSounds();
		const pack = JSON.parse(JSON.stringify(S.pack));
		delete pack.source;   // the artist's whole sheet stays on this PC: only the frames in use travel
		for (const [role, v] of Object.entries(pack.sounds)) {
			if (!Array.isArray(v)) continue;
			pack.sounds[role] = [];
			for (const f of v) {
				const nm = typeof f === 'string' ? f : f.name;
				const blob = sounds[role + '/' + nm];
				if (!blob) continue;
				const out = uniqueName(pack.sounds[role].map((x) => x.replace(/\.wav$/, '')), fileSlug(nm)) + '.wav';
				await add(`sounds/${role}/${out}`, await asWav(blob));
				pack.sounds[role].push(out);
			}
		}
		const keep = S.pack;
		S.pack = pack;
		await add('pack.lua', new Blob([buildLua(b.where, b.cw, b.ch)]));
		S.pack = keep;
		await add('pack.json', new Blob([JSON.stringify(pack, null, '\t')]));
		await add('sheet.png', await blobOf(b.canvas));
		const a = h('a', { href: URL.createObjectURL(zip(files)), download: `${slug(S.pack.name)}.zip` });
		a.click();
		toast('Saved as a .zip. Import it here (⋯ → Import from a file), or unzip it into a characters folder.');
	} catch (e) {
		toast("Couldn't export it: " + e.message, true);
	}
}

async function importZip(file) {
	try {
		const entries = await unzip(await file.arrayBuffer());
		const root = (() => {
			const j = entries.find((e) => /(^|\/)pack\.json$/.test(e.name));
			return j ? j.name.slice(0, -'pack.json'.length) : null;
		})();
		if (root == null) throw new Error('no pack.json in it (only characters made in this editor can be imported)');
		const get = (n) => entries.find((e) => e.name === root + n);
		const pack = Object.assign(blankPack('Imported'), JSON.parse(new TextDecoder().decode(get('pack.json').data)));
		await save();
		await refreshPacks();
		if (S.packs.some((p) => p.name.toLowerCase() === pack.name.toLowerCase())) pack.name = freeName(pack.name);
		const id = freeId(pack.name);
		S.pack = pack;
		S.strips = {};
		for (const k of [...Object.keys(pack.anims), ...Object.keys(pack.poses).filter((k) => pack.poses[k].own)]) {
			const e = get(`anims/${k}.png`);
			if (e) S.strips[k] = await imageFrom(new Blob([e.data]));
			else {
				delete pack.anims[k];
				delete pack.poses[k];
			}
		}
		const sounds = {};
		for (const [role, v] of Object.entries(pack.sounds)) {
			if (!Array.isArray(v) || !ROLES.some((r) => r.k === role)) continue;
			for (const f of v) {
				const e = get(`sounds/${role}/${f}`);
				if (e) sounds[role + '/' + f] = new Blob([e.data]);
			}
		}
		S.editable = true;
		S.builtin = false;
		await writeAll(id, sounds);
		await refreshPacks();
		await openPack(id);
		toast(`Imported ${pack.name}.`);
	} catch (e) {
		toast("Couldn't import it: " + e.message, true);
	}
}

// ------------------------------------------------------------------ the modal

function openModal(...kids) {
	$('#edDialog').replaceChildren(...kids);
	$('#edModal').hidden = false;
}
function closeModal() {
	$('#edModal').hidden = true;
	$('#edDialog').replaceChildren();
	modalTick = null;
	$('#edDialog').className = 'dialog';
}
let modalTick = null;

// ------------------------------------------------------------------ hearing a character

// "Hear it" plays each voice role in turn, so trying a character takes one click. HEAR: the role and
// what the stage shows meanwhile (an animation it lacks borrows another, like everywhere else).
const HEAR = [['notice', 'look'], ['question', 'look'], ['cheer', 'run'], ['pet', 'clean'], ['plead', 'idle'], ['angry', 'scared']];
const pickOne = (a) => a[Math.floor(Math.random() * a.length)];
const defaultSound = (set, role, i) => () => api('default-sound', { set, role, i }).then((r) => r.blob());
// what the open character plays for a role, unsaved changes included: a list of blob getters
function roleSounds(role) {
	let v = S.pack.sounds[role];
	if (typeof v === 'string') v = S.pack.sounds[(role = v.slice(1))];
	if (v === false) return [];
	if (Array.isArray(v) && v.length) return v.map((f) => () => soundBlob(role, f));
	const [set, files] = builtin(role);
	return files.map((_, i) => defaultSound(set, role, i));
}
// ...and for any character, from its saved pack.json
function packSounds(info, role) {
	const own = info.sounds?.[role];
	if (own?.length) return own.map((_, i) => () => api('sound', { id: info.id, role, i }).then((r) => r.blob()));
	if (!(info.default_sounds || []).includes(role)) return [];
	const [set, files] = builtin(role, info.sound_set || 'default');
	return files.map((_, i) => defaultSound(set, role, i));
}

let hearing = null;   // the run playing now: { off, btn, label }
function stopHearing() {
	const run = hearing;
	if (!run) return;
	hearing = null;
	run.off = true;
	S.show = null;
	stopSound();
	run.btn.classList.remove('on');
	run.btn.textContent = run.label;
}
// plays one of `sounds(role)` for each voice role, calling `onRole(role, anim)` as each starts
async function hear(btn, sounds, onRole) {
	const was = hearing?.btn;
	stopHearing();
	if (was === btn) return;
	const run = (hearing = { off: false, btn, label: btn.textContent });
	btn.classList.add('on');
	btn.textContent = '\u25A0 Stop';
	let any = false;
	for (const [role, anim] of HEAR) {
		const gets = sounds(role);
		if (!gets.length) continue;
		let buf;
		try {
			buf = await decodeAudio(await (await pickOne(gets)()).arrayBuffer());
		} catch {
			continue;
		}
		if (run.off) return;
		any = true;
		onRole?.(role, anim);
		await new Promise((res) => playBuffer(buf, 0, undefined, 1, res));
		if (run.off) return;
		await new Promise((res) => setTimeout(res, 380));
		if (run.off) return;
	}
	if (!any) toast('It has no voice sounds: every role is silent.');
	if (hearing === run) stopHearing();
}
function hearOpen() {
	if (!S.pack || !has('idle')) return;
	hear($('#hear'), roleSounds, (role, anim) => (S.show = { k: anim, role }));
}

// ------------------------------------------------------------------ the characters gallery

// Every character, idling, with its sounds a click away: the quick way to try each one.
async function openGallery() {
	await save();
	const cards = [];
	const grid = h('div', { class: 'gallery' });
	const close = () => { stopHearing(); closeModal(); };
	openModal(h('h2', { text: 'Characters' }),
		h('p', { class: 'sub', text: 'The ones that come with OBSPETS and the ones you made. Hear each one, then open it to edit it or show it on your Pet.' }),
		grid, h('div', { class: 'actions' }, h('button', { text: 'Close', onclick: close })));
	for (const p of S.packs) {
		const g = { id: p.id, c: canvas(150, 96), role: '' };
		g.note = h('span', { class: 'gnote', text: p.user ? 'Yours' : p.folder ? 'From a folder' : 'Comes with OBSPETS' });
		g.hearBtn = h('button', { class: 'small', text: '\u25B6 Hear it', disabled: true });
		g.hearBtn.addEventListener('click', () => hear(g.hearBtn, (role) => packSounds(g.info, role), (role) => (g.role = role)).then(() => (g.role = '')));
		const here = p.id === S.id;
		const open = h('button', { class: 'small primary', text: here ? 'Open now' : 'Open', disabled: here,
			onclick: () => { close(); openPack(p.id); } });
		grid.append(h('div', { class: 'gcard' + (here ? ' on' : '') }, g.c,
			h('div', { class: 'gname', text: p.id === S.id && S.pack ? S.pack.name : p.name }), g.note, h('div', { class: 'gbtns' }, g.hearBtn, open)));
		cards.push(g);
	}
	const draw = (g, t) => {
		const x = ctx2d(g.c), W = g.c.width, H = g.c.height, [cw, ch] = g.info.cell;
		x.clearRect(0, 0, W, H);
		const idle = g.info.anims.idle, cells = idle.cells;
		const [col, row] = cells[Math.floor(t * (idle.fps || 6)) % cells.length];
		const feet = g.info.feet || 0, z = Math.max(1, Math.floor(Math.min((W - 8) / cw, (H - 8) / (ch - feet))));
		x.drawImage(g.sheet, col * cw, row * ch, cw, ch, Math.round((W - cw * z) / 2), H - 4 - (ch - feet) * z, cw * z, ch * z);
		if (g.role) {
			x.font = '600 11px "Segoe UI", system-ui, sans-serif';
			x.fillStyle = '#ece4de';
			x.fillText('\u266A ' + VOICE.find((v) => v.k === g.role).name, 6, 14);
		}
	};
	modalTick = () => {
		const t = performance.now() / 1000;
		for (const g of cards) if (g.sheet) draw(g, t);
	};
	await Promise.all(cards.map(async (g) => {
		try {
			g.info = await apiJson('pack', { id: g.id });
			g.hearBtn.disabled = false;
			const own = Object.values(g.info.sounds || {}).some((f) => f.length);
			const set = g.info.sound_set || 'default';
			g.note.textContent += ' \u00B7 ' + (own ? 'own sounds' : set === 'none' ? 'no sounds' : set === 'default' ? 'soft sounds' : setName(set).toLowerCase() + ' sounds');
			if (g.info.anims?.idle?.cells?.length) g.sheet = await imageFrom(await (await api('sheet', { id: g.id })).blob());
		} catch { /* shows its name only */ }
	}));
}

// ------------------------------------------------------------------ importing images

// the frames in a file (or several files): an animated image decodes to its frames, a still
// image comes back as it is for the grid picker
async function readImages(files) {
	const imgs = files.filter((f) => /^image\//.test(f.type) || /\.(png|gif|webp|apng|jpe?g|bmp)$/i.test(f.name)).sort(naturalSort);
	if (!imgs.length) throw new Error('that isn’t an image');
	if (imgs.length > 1) {
		const frames = [];
		for (const f of imgs) frames.push(await imageFrom(f));
		return { frames, name: imgs[0].name };
	}
	const f = imgs[0];
	if ('ImageDecoder' in window && /gif|webp|apng|png/i.test(f.type || f.name)) {
		try {
			const dec = new ImageDecoder({ data: f.stream(), type: f.type || 'image/gif' });
			await dec.tracks.ready;
			const n = dec.tracks.selectedTrack.frameCount;
			if (n > 1) {
				const frames = [];
				let dur = 0;
				for (let i = 0; i < n && i < 120; i++) {
					const { image } = await dec.decode({ frameIndex: i });
					const c = canvas(image.displayWidth, image.displayHeight);
					ctx2d(c).drawImage(image, 0, 0);
					dur += image.duration || 0;
					image.close();
					frames.push(c);
				}
				dec.close();
				const fps = dur > 0 ? clamp(Math.round((frames.length / (dur / 1e6)) * 10) / 10, 1, 30) : null;
				return { frames, fps, name: f.name };
			}
			dec.close();
		} catch { /* a still image after all */ }
	}
	return { sheet: await imageFrom(f), name: f.name };
}

function guessGrid(c) {
	const w = c.width, hh = c.height;
	const d = ctx2d(c).getImageData(0, 0, w, hh).data;
	const on = (x, y) => d[(y * w + x) * 4 + 3] > 16;
	// the smallest square grid whose lines don't cut through a drawing
	const cuts = (s) => {
		let n = 0;
		for (let x = s; x < w; x += s) for (let y = 0; y < hh; y++) if (on(x - 1, y) && on(x, y)) n++;
		for (let y = s; y < hh; y += s) for (let x = 0; x < w; x++) if (on(x, y - 1) && on(x, y)) n++;
		return n;
	};
	const sizes = [...new Set([8, 16, 24, 32, 48, 64, 96, 128, 192, 256, hh])].sort((a, b) => a - b);
	for (const s of sizes)
		if (w % s === 0 && hh % s === 0 && w / s <= 64 && hh / s <= 64 && w / s * (hh / s) > 1 && cuts(s) <= Math.max(2, (w + hh) * 0.02))
			return { cols: w / s, rows: hh / s };
	if (w % hh === 0 && w / hh <= 64) return { cols: w / hh, rows: 1 };
	// a single row with gaps between the frames
	const empty = (x) => {
		for (let y = 0; y < hh; y++) if (d[(y * w + x) * 4 + 3] > 16) return false;
		return true;
	};
	let runs = 0, inRun = false;
	for (let x = 0; x < w; x++) {
		const e = empty(x);
		if (!e && !inRun) runs++;
		inRun = !e;
	}
	if (runs > 1 && w % runs === 0) return { cols: runs, rows: 1 };
	return { cols: 1, rows: 1 };
}
const cellEmpty = (src, x, y, w, hh) => !alphaBox(src, x, y, w, hh);

// art drawn at 2x, 3x...: every k-by-k block (from each frame's corner) is one color
function pixelScale(src, cells, w, hh) {
	const d = ctx2d(src).getImageData(0, 0, src.width, src.height).data;
	const W = src.width;
	const same = (a, b) => (d[a + 3] < 8 && d[b + 3] < 8) || (d[a] === d[b] && d[a + 1] === d[b + 1] && d[a + 2] === d[b + 2] && d[a + 3] === d[b + 3]);
	for (const k of [8, 6, 5, 4, 3, 2]) {
		if (w % k || hh % k) continue;
		let ok = true;
		for (const [cx, cy] of cells) {
			for (let y = 0; y < hh && ok; y++)
				for (let x = 0; x < w && ok; x++) {
					const bx = cx + x - (x % k), by = cy + y - (y % k);
					if (!same(((cy + y) * W + cx + x) * 4, (by * W + bx) * 4)) ok = false;
				}
			if (!ok) break;
		}
		if (ok) return k;
	}
	return 1;
}

// trim a strip to its drawing: bottom on the floor, centered on the first frame's body (or on
// x = `center`: frames cut from one sheet keep the sheet's own alignment)
function tightStrip(frames, center = null) {
	const boxes = frames.map((f) => alphaBox(f, 0, 0, f.width, f.height));
	const any = boxes.filter(Boolean);
	if (!any.length) return null;
	const top = Math.min(...any.map((b) => b.minY)), bottom = Math.max(...any.map((b) => b.maxY)) + 1;
	const first = boxes.find(Boolean);
	const c0 = center ?? (first.minX + first.maxX + 1) / 2;
	const half = Math.ceil(Math.max(...any.map((b) => Math.max(c0 - b.minX, b.maxX + 1 - c0))));
	const fw = half * 2, fh = bottom - top, left = Math.round(c0 - half);
	const out = canvas(fw * frames.length, fh), x = ctx2d(out);
	frames.forEach((f, i) => {
		x.save();
		x.beginPath();
		x.rect(i * fw, 0, fw, fh);
		x.clip();
		x.drawImage(f, i * fw - left, -top);
		x.restore();
	});
	return { strip: out, n: frames.length };
}

// How big a sheet is drawn in a dialog: as big as fits in maxW × maxH (whole steps when it's
// enlarged, so the pixels stay square). A long thin strip, or a sheet of many tiny cells, would
// shrink to slivers that can't be clicked: that one is drawn readable instead, and its view scrolls.
// `cell`: the smaller side of one cell, as first guessed.
function sheetScale(sheet, maxW, maxH, cap, cell) {
	const fit = Math.min(maxW / sheet.width, maxH / sheet.height);
	const s = fit >= 2 ? Math.min(cap, Math.floor(fit)) : fit;
	const need = Math.max(96 / Math.min(sheet.width, sheet.height), 24 / Math.max(1, cell));
	if (s >= need) return { scale: s, scroll: false };
	return { scale: Math.min(cap, Math.ceil(need)), scroll: true };
}
// the canvas a sheet is drawn on: one that fits also shrinks with a narrow window; one that scrolls keeps its size
function sheetCanvas(sheet, z) {
	const c = canvas(Math.round(sheet.width * z.scale), Math.round(sheet.height * z.scale));
	if (!z.scroll) c.classList.add('fit');
	return c;
}

// The import dialog: the image with its grid; click the first frame, then the last.
function frameDialog({ title, src, frames, fps, single, cols, rows }) {
	return new Promise((resolve) => {
		let sheet = src;
		if (frames) {   // separate frames: lay them out as one row
			const w = Math.max(...frames.map((f) => f.width)), hh = Math.max(...frames.map((f) => f.height));
			sheet = canvas(w * frames.length, hh);
			const x = ctx2d(sheet);
			frames.forEach((f, i) => x.drawImage(f, i * w + Math.floor((w - f.width) / 2), hh - f.height));
			cols = frames.length;
			rows = 1;
		}
		const g = cols ? { cols, rows } : guessGrid(sheet);
		const st = { cols: g.cols, rows: g.rows, a: 0, b: 0, picking: false, fps: fps || 8, skip: true };
		const cw = () => Math.floor(sheet.width / st.cols), chh = () => Math.floor(sheet.height / st.rows);
		const defaultSel = () => {
			st.a = 0;
			let b = single ? 0 : st.rows === 1 ? st.cols - 1 : st.cols - 1;
			while (b > 0 && cellEmpty(sheet, (b % st.cols) * cw(), Math.floor(b / st.cols) * chh(), cw(), chh())) b--;
			st.b = b;
		};
		defaultSel();
		// the space left of the side column (the dialog is 760 px)
		const z = sheetScale(sheet, 540, 460, 8, Math.min(cw(), chh()));
		const scale = z.scale;
		const view = sheetCanvas(sheet, z);
		const pxIn = h('select', {}, ...[1, 2, 3, 4, 5, 6, 8].map((k) => h('option', { value: k, text: k === 1 ? '1× (as drawn)' : `${k}× (shrink it)` })));
		const detectPx = () => {
			const fr = chosen();
			st.px = fr.length ? pixelScale(sheet, fr, cw(), chh()) : 1;
			pxIn.value = String(st.px);
		};
		pxIn.addEventListener('change', () => (st.px = +pxIn.value));
		const prev = canvas(150, 150);
		const colsIn = h('input', { type: 'number', min: 1, max: 128, value: st.cols });
		const rowsIn = h('input', { type: 'number', min: 1, max: 128, value: st.rows });
		const fpsIn = h('input', { type: 'range', min: 1, max: 30, step: 0.5, value: st.fps });
		const fpsVal = h('span', { class: 'val' });
		const info = h('div', { class: 'muted small' });
		const skipIn = h('input', { type: 'checkbox', checked: true });
		const chosen = () => {
			const out = [];
			const lo = Math.min(st.a, st.b), hi = Math.max(st.a, st.b);
			for (let i = lo; i <= hi; i++) {
				const x = (i % st.cols) * cw(), y = Math.floor(i / st.cols) * chh();
				if (st.skip && cellEmpty(sheet, x, y, cw(), chh())) continue;
				out.push([x, y]);
			}
			if (st.a > st.b) out.reverse();
			return single ? out.slice(0, 1) : out;
		};
		const draw = () => {
			const x = ctx2d(view);
			x.clearRect(0, 0, view.width, view.height);
			x.drawImage(sheet, 0, 0, view.width, view.height);
			const lo = Math.min(st.a, st.b), hi = Math.max(st.a, st.b);
			for (let r = 0; r < st.rows; r++)
				for (let c = 0; c < st.cols; c++) {
					const i = r * st.cols + c;
					const px = c * cw() * scale, py = r * chh() * scale;
					if (i >= lo && i <= hi) {
						x.fillStyle = 'rgba(226,75,26,.24)';
						x.fillRect(px, py, cw() * scale, chh() * scale);
					}
				}
			x.strokeStyle = 'rgba(255,255,255,.18)';
			x.lineWidth = 1;
			for (let c = 0; c <= st.cols; c++) {
				x.beginPath();
				x.moveTo(c * cw() * scale + 0.5, 0);
				x.lineTo(c * cw() * scale + 0.5, view.height);
				x.stroke();
			}
			for (let r = 0; r <= st.rows; r++) {
				x.beginPath();
				x.moveTo(0, r * chh() * scale + 0.5);
				x.lineTo(view.width, r * chh() * scale + 0.5);
				x.stroke();
			}
			const sel = (i, color) => {
				x.strokeStyle = color;
				x.lineWidth = 2;
				x.strokeRect((i % st.cols) * cw() * scale + 1, Math.floor(i / st.cols) * chh() * scale + 1, cw() * scale - 2, chh() * scale - 2);
			};
			sel(st.a, '#6ee7b7');
			if (st.b !== st.a) sel(st.b, '#fbbf24');
			const n = chosen().length;
			info.textContent = single ? 'Click the frame to use.' :
				`${n} frame${n === 1 ? '' : 's'} · ${cw()}×${chh()} px each. ${st.picking ? 'Now click the last frame.' : 'Click a first frame, then a last one.'}`;
			fpsVal.textContent = `${st.fps} fps`;
		};
		view.addEventListener('click', (e) => {
			const r = view.getBoundingClientRect();
			const c = Math.floor(((e.clientX - r.left) / r.width) * sheet.width / cw());
			const ro = Math.floor(((e.clientY - r.top) / r.height) * sheet.height / chh());
			const i = clamp(ro, 0, st.rows - 1) * st.cols + clamp(c, 0, st.cols - 1);
			if (single || (!st.picking && !e.shiftKey)) {
				st.a = st.b = i;
				st.picking = !single;
			} else {
				st.b = i;
				st.picking = false;
			}
			if (!st.picking) detectPx();
			draw();
		});
		const regrid = () => {
			st.cols = clamp(+colsIn.value || 1, 1, 128);
			st.rows = clamp(+rowsIn.value || 1, 1, 128);
			defaultSel();
			detectPx();
			draw();
		};
		colsIn.addEventListener('input', regrid);
		rowsIn.addEventListener('input', regrid);
		fpsIn.addEventListener('input', () => {
			st.fps = +fpsIn.value;
			draw();
		});
		skipIn.addEventListener('change', () => {
			st.skip = skipIn.checked;
			draw();
		});
		const t0 = performance.now();
		modalTick = () => {
			const x = ctx2d(prev);
			x.clearRect(0, 0, 150, 150);
			const fr = chosen();
			if (!fr.length) return;
			const i = Math.floor(((performance.now() - t0) / 1000) * st.fps) % fr.length;
			const z = Math.max(1, Math.floor(Math.min(140 / cw(), 140 / chh())));
			x.drawImage(sheet, fr[i][0], fr[i][1], cw(), chh(), (150 - cw() * z) / 2, 145 - chh() * z, cw() * z, chh() * z);
		};
		const done = (v) => {
			closeModal();
			resolve(v);
		};
		const use = () => {
			const fr = chosen();
			if (!fr.length) return toast('Those frames are empty.', true);
			const k = st.px || 1;
			const cells = fr.map(([x, y]) => {
				const c = canvas(Math.round(cw() / k), Math.round(chh() / k));
				ctx2d(c).drawImage(sheet, x, y, cw(), chh(), 0, 0, c.width, c.height);
				return c;
			});
			const t = tightStrip(cells);
			if (!t) return toast('Those frames are empty.', true);
			done({ strip: t.strip, frames: t.n, fps: st.fps, src: { sheet, cols: st.cols, rows: st.rows } });
		};
		detectPx();
		openModal(
			h('h2', { text: title }),
			h('p', { class: 'sub', text: single ? 'Pick one frame.' : 'Check the grid matches the frames, then pick the ones to use.' }),
			h('div', { class: 'import-grid' },
				h('div', {}, h('div', { class: 'sheetview' }, view), info),
				h('div', { class: 'import-side' }, prev,
					h('div', { class: 'row' }, h('label', {}, 'Columns', colsIn), h('label', {}, 'Rows', rowsIn)),
					h('label', { title: 'Pixel art saved bigger than it was drawn is shrunk back, so it matches the rest' }, 'Pixel size', pxIn),
					single ? null : h('label', {}, h('span', {}, 'Speed ', fpsVal), fpsIn),
					h('label', { class: 'switch' }, skipIn, 'Skip empty frames'))),
			h('div', { class: 'actions' }, h('button', { text: 'Cancel', onclick: () => done(null) }),
				h('button', { class: 'primary', text: single ? 'Use this frame' : 'Use these frames', onclick: use })),
		);
		draw();
	});
}

// files dropped or picked for slot k
async function importInto(k, files) {
	if (!S.editable) return toast('Make your own copy first (the button at the top).', true);
	const isPose = POSES.some((p) => p.k === k);
	const label = [...ANIMS, ...POSES].find((a) => a.k === k).name;
	let got;
	try {
		got = await readImages(files);
	} catch (e) {
		return toast(e.message, true);
	}
	// one image holding several animations: the sheet dialog takes all of them at once
	if (got.sheet && !slotFromName(got.name) && looksLikeSheet(got.sheet)) return openSheet(got, k);
	const r = await frameDialog({ title: `${has(k) ? 'Replace' : 'Add'} ${label}`, src: got.sheet, frames: got.frames, fps: got.fps || S.pack.anims[k]?.fps, single: isPose });
	if (!r) return;
	applyStrip(k, r);
}
function applyStrip(k, r, quiet = false) {
	const isPose = POSES.some((p) => p.k === k);
	S.strips[k] = r.strip;
	S.srcs[k] = r.src;
	if (isPose) S.pack.poses[k] = { own: true, dx: 0, dy: 0 };
	else {
		const old = S.pack.anims[k] || {};
		const n = r.frames;
		const a = { frames: n, fps: r.fps, dx: 0, dy: 0, flip: !!old.flip };
		if (k === 'paw') {
			a.whoosh = Math.floor(n * 0.34);
			a.hit = Math.min(n - 1, a.whoosh + 1);
		}
		S.pack.anims[k] = a;
		if (S.pack.stands) delete S.pack.stands[k];
		// jump poses that pointed past the new frames go back to automatic
		for (const p of Object.values(S.pack.poses)) if (p.anim === k && p.frame >= n) p.frame = 0;
	}
	S.dirty.add(k);
	if (!r.src?.map) dropFromSource(k);
	if (quiet) return;
	select(k);
	changed();
	if (k === 'idle') fitZoom();
	if (!isPose && k === 'idle' && Object.keys(S.pack.anims).length === 1) toast('Looking good. Add the others the same way: anything missing borrows from another animation.');
}

async function changeFrames(k) {
	const s = S.srcs[k];
	if (s?.map && S.sheetMap) return openSheet(null, k);
	if (!s) return pick((f) => importInto(k, f));
	const isPose = POSES.some((p) => p.k === k);
	const r = await frameDialog({ title: `Change frames`, src: s.sheet, cols: s.cols, rows: s.rows, fps: S.pack.anims[k]?.fps, single: isPose });
	if (r) applyStrip(k, r);
}

// ------------------------------------------------------------------ a whole sprite sheet at once

// The sheet dialog: drop one image holding many animations, and every animation is a colored card
// across the top with its own preview. Click a card, then click its first and last frame on the
// sheet (or drag across them). Each slot has one color, on its card and on its frames.
const SLOT_COLORS = {
	idle: '#38bdf8', look: '#a78bfa', clean: '#f472b6', clean2: '#e879f9', walk: '#4ade80', run: '#facc15',
	sleep: '#818cf8', paw: '#fb923c', scared: '#ef4444', crouch: '#2dd4bf', rise: '#a3e635', fall: '#cbd5e1', land: '#d6a36a',
};
const SLOT_FPS = { idle: 6, look: 6, clean: 8, clean2: 8, walk: 10, run: 14, sleep: 4, paw: 10, scared: 12 };
const isPoseKey = (k) => POSES.some((p) => p.k === k);
const slotDef = (k) => [...ANIMS, ...POSES].find((a) => a.k === k);
// what an unknown sheet's rows most often are, top to bottom: only a first guess, the cards say so
const GUESS_ORDER = ['idle', 'walk', 'run', 'look', 'sleep', 'paw', 'scared', 'clean', 'clean2'];

// Sheets from popular asset packs, known by their layout (so a recolored or enlarged copy matches
// too): the grid and how many frames each row holds. A recipe is only the mapping, never the art,
// so it can ship whatever the art's licence says; the user brings the sheet.
// items: [slot, row, first column, last column, fps]
const RECIPES = [{
	name: 'Elthen’s 2D Pixel Art Cat',
	cell: 32, cols: 8, rows: 10, counts: [4, 4, 4, 4, 8, 8, 4, 6, 7, 8],
	items: [['idle', 0, 0, 3, 5], ['look', 1, 0, 3, 5], ['clean', 2, 0, 3, 7], ['clean2', 3, 0, 3, 7], ['walk', 4, 0, 7, 10],
		['run', 5, 0, 7, 14], ['sleep', 6, 0, 3, 3], ['paw', 7, 0, 5, 9], ['scared', 9, 0, 7, 12],
		['crouch', 8, 4, 4], ['rise', 8, 1, 1], ['fall', 8, 2, 2], ['land', 8, 5, 5]],
	paw: { whoosh: 2, hit: 3 },
	pack: { faces: 'right', sound_set: 'cat', describe: 'a small pixel-art cat' },
}];

// for each row: the columns that have a drawing
function rowsUsed(sheet, cols, rows) {
	const cw = Math.floor(sheet.width / cols), ch = Math.floor(sheet.height / rows);
	const out = [];
	for (let r = 0; r < rows; r++) {
		const cs = [];
		for (let c = 0; c < cols; c++) if (!cellEmpty(sheet, c * cw, r * ch, cw, ch)) cs.push(c);
		out.push(cs);
	}
	return out;
}
function matchRecipe(sheet) {
	for (const R of RECIPES) {
		const s = sheet.width / (R.cols * R.cell);
		if (!Number.isInteger(s) || s < 1 || s > 4 || sheet.height !== R.rows * R.cell * s) continue;
		const used = rowsUsed(sheet, R.cols, R.rows);
		if (used.every((cs, r) => cs.length === R.counts[r])) return { R, px: s };
	}
	return null;
}
function looksLikeSheet(sheet) {
	if (matchRecipe(sheet)) return true;
	const g = guessGrid(sheet);
	return g.rows > 1 && rowsUsed(sheet, g.cols, g.rows).filter((cs) => cs.length).length > 1;
}

// the first mapping for a sheet: a recipe's, or one card per row with a guessed name
function sheetGuess(sheet, cols, rows) {
	const m = !cols && matchRecipe(sheet);
	if (m) {
		const { R } = m;
		return {
			cols: R.cols, rows: R.rows, px: m.px, recipe: R,
			items: R.items.map(([k, r, c0, c1, fps]) => ({ k, a: r * R.cols + c0, b: r * R.cols + c1, fps: fps || SLOT_FPS[k] || 8 })),
		};
	}
	if (!cols) ({ cols, rows } = guessGrid(sheet));
	const items = [];
	rowsUsed(sheet, cols, rows).forEach((cs, r) => {
		if (!cs.length || items.length >= GUESS_ORDER.length) return;
		const k = GUESS_ORDER[items.length];
		items.push({ k, a: r * cols + cs[0], b: r * cols + cs[cs.length - 1], fps: SLOT_FPS[k], guessed: true });
	});
	return { cols, rows, px: 0, recipe: null, items };
}

async function sheetFromFiles(files) {
	if (!S.editable) return toast('Make your own copy first (the button at the top).', true);
	let got;
	try {
		got = await readImages(files);
	} catch (e) {
		return toast(e.message, true);
	}
	if (!got.sheet) return importInto(S.sel, files);   // an animated GIF or loose frames: one animation
	return openSheet(got, null);
}

// got: a freshly read sheet ({ sheet, name }), or null to reopen the one in use. focus: the slot to
// start on (added as an empty card if the sheet has none)
async function openSheet(got, focus) {
	if (!S.editable) return toast('Make your own copy first (the button at the top).', true);
	const before = got ? null : S.sheetMap;
	const m = await sheetDialog(got ? { sheet: got.sheet, name: got.name, focus } : { prev: S.sheetMap, focus });
	if (!m) return;
	if (m === 'one') {   // "it's all one animation": the one-animation dialog, for the slot it was dropped on
		const r = await frameDialog({ title: `${has(focus) ? 'Replace' : 'Add'} ${slotDef(focus).name}`, src: got.sheet, fps: S.pack.anims[focus]?.fps, single: isPoseKey(focus) });
		if (r) applyStrip(focus, r);
		return;
	}
	applySheet(m, before);
}

function sheetDialog({ sheet, name, focus, prev }) {
	return new Promise((resolve) => {
		const st = prev
			? { ...prev, items: prev.items.map(({ k, a, b, fps }) => ({ k, a, b, fps })) }
			: { sheet, name, skip: true, ...sheetGuess(sheet) };
		sheet = st.sheet;
		st.sel = 0;
		st.picking = false;
		const cw = () => Math.floor(sheet.width / st.cols), chh = () => Math.floor(sheet.height / st.rows);
		const empty = (i) => cellEmpty(sheet, (i % st.cols) * cw(), Math.floor(i / st.cols) * chh(), cw(), chh());
		// the cells an item uses, in order
		const cellsOf = (it) => {
			if (it.a < 0) return [];
			if (isPoseKey(it.k)) return [it.a];
			const lo = Math.min(it.a, it.b), hi = Math.max(it.a, it.b), out = [];
			for (let i = lo; i <= hi; i++) if (!st.skip || !empty(i)) out.push(i);
			return it.a > it.b ? out.reverse() : out;
		};
		const xy = (i) => [(i % st.cols) * cw(), Math.floor(i / st.cols) * chh()];
		const pxIn = h('select', {}, ...[1, 2, 3, 4, 5, 6, 8].map((k) => h('option', { value: k, text: k === 1 ? '1× (as drawn)' : `${k}× (shrink it)` })));
		pxIn.addEventListener('change', () => (st.px = +pxIn.value));
		const detectPx = () => {
			const it = st.items.find((x) => x.k === 'idle' && x.a >= 0) || st.items.find((x) => x.a >= 0);
			st.px = it ? pixelScale(sheet, cellsOf(it).map(xy), cw(), chh()) : 1;
			pxIn.value = String(st.px);
		};
		const focusOn = (k) => {
			if (!k) return;
			let i = st.items.findIndex((it) => it.k === k);
			if (i < 0) {
				st.items.push({ k, a: -1, b: -1, fps: S.pack.anims[k]?.fps || SLOT_FPS[k] || 8 });
				i = st.items.length - 1;
			}
			st.sel = i;
		};

		// the sheet, as big as fits
		const z = sheetScale(sheet, 1000, 360, 10, Math.min(cw(), chh()));
		const scale = z.scale;
		const view = sheetCanvas(sheet, z);
		const hint = h('div', { class: 'mhint' });
		const chips = h('div', { class: 'mchips' });
		const fpsBox = h('div', { class: 'mfps' });
		const colsIn = h('input', { type: 'number', min: 1, max: 128, value: st.cols });
		const rowsIn = h('input', { type: 'number', min: 1, max: 128, value: st.rows });
		const cellInfo = h('span', { class: 'muted' });
		const skipIn = h('input', { type: 'checkbox', checked: st.skip });
		const useBtn = h('button', { class: 'primary' });
		if (!st.px) detectPx();
		else pxIn.value = String(st.px);
		focusOn(focus);

		const names = (it) => {
			const taken = new Set(st.items.filter((x) => x !== it).map((x) => x.k));
			const opt = (d) => h('option', { value: d.k, text: d.name, disabled: taken.has(d.k) });
			const s = h('select', { class: 'mname', title: 'What this animation is' },
				h('optgroup', { label: 'Animations' }, ...ANIMS.map(opt)), h('optgroup', { label: 'Jump poses (one frame)' }, ...POSES.map(opt)));
			s.value = it.k;
			s.addEventListener('change', () => {
				it.k = s.value;
				it.guessed = false;
				if (isPoseKey(it.k) && it.a >= 0) it.b = it.a;
				if (!isPoseKey(it.k)) it.fps = it.fps || SLOT_FPS[it.k] || 8;
				refresh();
			});
			return s;
		};
		const selectItem = (i) => {
			if (st.sel === i) return;
			st.sel = i;
			st.picking = false;
			[...chips.querySelectorAll('.mchip')].forEach((c, j) => c.classList.toggle('on', j === i));
			renderSide();
			draw();
		};
		const renderChips = () => {
			const free = [...ANIMS, ...POSES].filter((d) => !st.items.some((it) => it.k === d.k));
			const add = h('select', { class: 'madd', 'aria-label': 'Add an animation', title: 'Add an animation' },
				h('option', { value: '', text: 'Add…' }),
				h('optgroup', { label: 'Animations' }, ...free.filter((d) => !isPoseKey(d.k)).map((d) => h('option', { value: d.k, text: d.name }))),
				h('optgroup', { label: 'Jump poses (one frame)' }, ...free.filter((d) => isPoseKey(d.k)).map((d) => h('option', { value: d.k, text: d.name }))));
			add.addEventListener('change', () => {
				if (!add.value) return;
				focusOn(add.value);
				st.picking = false;
				refresh();
			});
			chips.replaceChildren(...st.items.map((it, i) => {
				it.cv = canvas(72, 72);
				const n = cellsOf(it).length;
				return h('div', {
					class: `mchip${i === st.sel ? ' on' : ''}${it.a < 0 ? ' unset' : ''}`, style: `--c:${SLOT_COLORS[it.k]}`,
					onclick: () => selectItem(i),
				},
				it.cv, names(it),
				h('div', { class: 'mcount', text: it.a < 0 ? 'Click its frames' : isPoseKey(it.k) ? 'Pose' : `${n} frame${n === 1 ? '' : 's'}${it.guessed ? ' · a guess' : ''}` }),
				h('button', {
					class: 'mx', text: '×', title: 'Remove', 'aria-label': 'Remove',
					onclick: (e) => {
						e.stopPropagation();
						st.items.splice(i, 1);
						st.sel = clamp(st.sel > i ? st.sel - 1 : st.sel, 0, Math.max(0, st.items.length - 1));
						st.picking = false;
						refresh();
					},
				}));
			}), ...(free.length ? [h('label', { class: 'mchip mplus', title: 'Add an animation' }, h('span', { class: 'big', text: '+' }), add)] : []));
		};
		const tagOf = (it) => h('b', { text: slotDef(it.k).name, style: `color:${SLOT_COLORS[it.k]}` });
		const renderSide = () => {
			const it = st.items[st.sel];
			if (!it) hint.replaceChildren('Click ', h('b', { text: '+' }), ' to add an animation.');
			else if (isPoseKey(it.k)) hint.replaceChildren(tagOf(it), `: click the one frame to use (${slotDef(it.k).what.toLowerCase()}). A jump goes crouch → rise → fall → land.`);
			else if (st.picking) hint.replaceChildren(tagOf(it), ': now click its last frame.');
			else hint.replaceChildren(tagOf(it), ': click its first frame, then its last. Or drag across them.');
			fpsBox.replaceChildren();
			if (it && !isPoseKey(it.k)) {
				const [r, v] = slider(1, 24, 0.5, it.fps || 8, (x) => (it.fps = x), (x) => `${x} fps`);
				fpsBox.append(h('span', { class: 'muted', text: 'Speed' }), r, v);
			}
			cellInfo.textContent = `${cw()}×${chh()} px each`;
			const n = st.items.filter((x) => cellsOf(x).length).length;
			useBtn.textContent = n > 1 ? `Use these ${n}` : 'Use it';
			useBtn.disabled = !n;
		};
		const refresh = () => {
			renderChips();
			renderSide();
			draw();
		};

		const draw = () => {
			const x = ctx2d(view);
			x.clearRect(0, 0, view.width, view.height);
			x.drawImage(sheet, 0, 0, view.width, view.height);
			const sw = cw() * scale, sh = chh() * scale;
			x.strokeStyle = 'rgba(255,255,255,.14)';
			x.lineWidth = 1;
			for (let c = 0; c <= st.cols; c++) {
				x.beginPath();
				x.moveTo(Math.round(c * sw) + 0.5, 0);
				x.lineTo(Math.round(c * sw) + 0.5, view.height);
				x.stroke();
			}
			for (let r = 0; r <= st.rows; r++) {
				x.beginPath();
				x.moveTo(0, Math.round(r * sh) + 0.5);
				x.lineTo(view.width, Math.round(r * sh) + 0.5);
				x.stroke();
			}
			// the others faintly, the one being edited strongly and outlined, on top
			const order = st.items.map((_, i) => i).filter((i) => i !== st.sel);
			if (st.items[st.sel]) order.push(st.sel);
			for (const i of order) {
				const it = st.items[i], on = i === st.sel, col = SLOT_COLORS[it.k];
				const cells = cellsOf(it);
				x.fillStyle = col + (on ? '55' : '26');
				for (const c of cells) x.fillRect((c % st.cols) * sw, Math.floor(c / st.cols) * sh, sw, sh);
				if (on) {
					x.strokeStyle = col;
					x.lineWidth = 2;
					for (const c of cells) x.strokeRect((c % st.cols) * sw + 1, Math.floor(c / st.cols) * sh + 1, sw - 2, sh - 2);
				}
				// its name on its first frame
				if (cells.length && sw >= 30 && sh >= 18) {
					const c = cells[0], o = on ? 2 : 0, tx = (c % st.cols) * sw + o, ty = Math.floor(c / st.cols) * sh + o;
					x.font = '600 10px "Segoe UI", system-ui, sans-serif';
					const label = slotDef(it.k).name;
					x.fillStyle = col;
					x.fillRect(tx, ty, Math.min(x.measureText(label).width + 6, sw - 2 * o), 13);
					x.fillStyle = '#0a0a0a';
					x.fillText(label, tx + 3, ty + 10, sw - 6 - 2 * o);
				}
			}
			if (st.picking && st.items[st.sel]) {
				const it = st.items[st.sel];
				x.strokeStyle = '#fff';
				x.lineWidth = 2;
				x.setLineDash([4, 3]);
				x.strokeRect((it.a % st.cols) * sw + 1, Math.floor(it.a / st.cols) * sh + 1, sw - 2, sh - 2);
				x.setLineDash([]);
			}
		};

		// clicking the sheet: a pose takes one cell; an animation a first and a last (two clicks or a drag)
		const cellAt = (e) => {
			const r = view.getBoundingClientRect();
			const c = clamp(Math.floor(((e.clientX - r.left) / r.width) * st.cols), 0, st.cols - 1);
			const ro = clamp(Math.floor(((e.clientY - r.top) / r.height) * st.rows), 0, st.rows - 1);
			return ro * st.cols + c;
		};
		let drag = false;
		const after = () => {
			if (st.items.filter((x) => x.a >= 0).length <= 1) detectPx();
			refresh();
		};
		view.addEventListener('pointerdown', (e) => {
			const it = st.items[st.sel];
			if (!it) return toast('Click + to add an animation first.');
			const i = cellAt(e);
			it.guessed = false;
			if (isPoseKey(it.k)) it.a = it.b = i;
			else if (st.picking) {
				it.b = i;
				st.picking = false;
			} else {
				it.a = it.b = i;
				drag = true;
				view.setPointerCapture(e.pointerId);
			}
			after();
		});
		view.addEventListener('pointermove', (e) => {
			if (!drag) return;
			const it = st.items[st.sel], i = cellAt(e);
			if (i !== it.b) {
				it.b = i;
				draw();
			}
		});
		view.addEventListener('pointerup', () => {
			if (!drag) return;
			drag = false;
			const it = st.items[st.sel];
			st.picking = it.a === it.b;
			after();
		});

		// a different grid starts the guesses again
		const regrid = () => {
			Object.assign(st, sheetGuess(sheet, clamp(+colsIn.value || 1, 1, 128), clamp(+rowsIn.value || 1, 1, 128)));
			st.sel = 0;
			st.picking = false;
			detectPx();
			focusOn(focus);
			refresh();
		};
		colsIn.addEventListener('change', regrid);
		rowsIn.addEventListener('change', regrid);
		skipIn.addEventListener('change', () => {
			st.skip = skipIn.checked;
			refresh();
		});

		const t0 = performance.now();
		modalTick = () => {
			const t = (performance.now() - t0) / 1000;
			const z = Math.min(68 / cw(), 68 / chh()), zz = z >= 1 ? Math.floor(z) : z;
			for (const it of st.items) {
				if (!it.cv) continue;
				const x = ctx2d(it.cv);
				x.clearRect(0, 0, 72, 72);
				const cells = cellsOf(it);
				if (!cells.length) continue;
				const [sx, sy] = xy(cells[Math.floor(t * (it.fps || 8)) % cells.length]);
				x.drawImage(sheet, sx, sy, cw(), chh(), Math.round((72 - cw() * zz) / 2), Math.round(70 - chh() * zz), cw() * zz, chh() * zz);
			}
		};
		const done = (v) => {
			closeModal();
			resolve(v);
		};
		const use = () => {
			const ready = st.items.filter((it) => cellsOf(it).length);
			if (!ready.length) return toast('Pick some frames first.', true);
			if (!has('idle') && !ready.some((it) => it.k === 'idle')) {
				focusOn('idle');
				refresh();
				return toast('Every character needs Idle. Click its frames, then Use.', true);
			}
			done({
				sheet, name: st.name, cols: st.cols, rows: st.rows, px: st.px || 1, skip: st.skip, recipe: st.recipe,
				items: ready.map(({ k, a, b, fps }) => ({ k, a, b, fps, cells: cellsOf({ k, a, b }) })),
			});
		};
		useBtn.addEventListener('click', use);

		openModal(
			h('h2', { text: prev ? 'Your sprite sheet' : 'Add a sprite sheet' }),
			st.recipe
				? h('div', { class: 'mrecipe' }, h('b', { text: `Recognised: ${st.recipe.name}.` }), ' Every animation is set up already. Check the previews, then click Use.')
				: h('p', { class: 'sub', text: 'Each card is one animation. Click a card, then click its frames on the sheet. The names are guesses: if a preview does something else, pick the right name under it.' }),
			chips,
			h('div', { class: 'mbar' }, hint, fpsBox),
			h('div', { class: 'sheetview msheet' }, view),
			h('div', { class: 'mgrid' },
				h('label', {}, 'Grid', colsIn, '×', rowsIn), cellInfo,
				h('label', { title: 'Pixel art saved bigger than it was drawn is shrunk back, so it matches the rest' }, 'Pixel size', pxIn),
				h('label', { class: 'switch' }, skipIn, 'Skip empty frames')),
			h('div', { class: 'actions' },
				focus && !prev ? h('button', { class: 'ghost', text: 'It’s all one animation', title: `Use frames from this image for ${slotDef(focus).name} only`, onclick: () => done('one') }) : null,
				h('div', { class: 'grow' }),
				h('button', { text: 'Cancel', onclick: () => done(null) }), useBtn),
		);
		$('#edDialog').className = 'dialog wide';
		refresh();
	});
}

// The sheet and which cells each slot came from are kept with the character (source/sheet.png, and
// `source` in pack.json), so Edit sheet works next time too.
function syncSource() {
	const m = S.sheetMap;
	if (!m || !m.items.length) {
		delete S.pack.source;
		S.sheetMap = null;
		return;
	}
	S.pack.source = {
		name: m.name || '', cols: m.cols, rows: m.rows, px: m.px, skip: m.skip, recipe: m.recipe?.name || null,
		items: m.items.map(({ k, a, b, fps }) => ({ k, a, b, fps })),
	};
}
// slot k was replaced some other way, or removed: the sheet no longer speaks for it
function dropFromSource(k) {
	const m = S.sheetMap;
	if (!m || !m.items.some((it) => it.k === k)) return;
	m.items = m.items.filter((it) => it.k !== k);
	syncSource();
	renderSlots();
}
async function saveSource(id, always = false) {
	if (!S.sheetMap || !S.pack.source || !(always || S.sourceDirty)) return;   // (a new or imported character has none)
	S.sourceDirty = false;
	await put(id, 'source/sheet.png', await blobOf(S.sheetMap.sheet));
}
async function loadSource(id) {
	const src = S.pack.source;
	if (!src) return;
	try {
		const sheet = await imageFrom(await (await api('file', { id, path: 'source/sheet.png' })).blob());
		const cw = Math.floor(sheet.width / src.cols), ch = Math.floor(sheet.height / src.rows);
		const items = src.items.filter((it) => slotDef(it.k) && it.a >= 0 && it.a < src.cols * src.rows).map((it) => {
			const cells = [];
			if (isPoseKey(it.k)) cells.push(it.a);
			else {
				for (let i = Math.min(it.a, it.b); i <= Math.max(it.a, it.b); i++)
					if (src.skip === false || !cellEmpty(sheet, (i % src.cols) * cw, Math.floor(i / src.cols) * ch, cw, ch)) cells.push(i);
				if (it.a > it.b) cells.reverse();
			}
			return { ...it, cells };
		});
		S.sheetMap = {
			sheet, name: src.name, cols: src.cols, rows: src.rows, px: src.px || 1, skip: src.skip !== false,
			recipe: RECIPES.find((R) => R.name === src.recipe) || null, items,
		};
		for (const it of items) if (S.strips[it.k] || S.pack.poses[it.k]) S.srcs[it.k] = { map: true };
	} catch {
		delete S.pack.source;   // (its sheet is gone: an imported character, or deleted by hand)
	}
}

// the sheet dialog's result into the character: one strip per animation, and the jump poses as
// frames of those (or drawings of their own when they're elsewhere on the sheet)
function applySheet(m, before) {
	const cw = Math.floor(m.sheet.width / m.cols), ch = Math.floor(m.sheet.height / m.rows), px = m.px || 1;
	const cut = (i) => {
		const c = canvas(Math.round(cw / px), Math.round(ch / px));
		ctx2d(c).drawImage(m.sheet, (i % m.cols) * cw, Math.floor(i / m.cols) * ch, cw, ch, 0, 0, c.width, c.height);
		return c;
	};
	const center = Math.round(cw / px) / 2;   // frames keep the sheet's own alignment
	const hadIdle = has('idle');
	const animAt = {};   // cell -> { anim, frame }
	let n = 0;
	for (const it of m.items) {
		if (isPoseKey(it.k)) continue;
		const t = tightStrip(it.cells.map(cut), center);
		if (!t) continue;
		applyStrip(it.k, { strip: t.strip, frames: t.n, fps: it.fps, src: { map: true } }, true);
		it.cells.forEach((c, f) => (animAt[c] ??= { anim: it.k, frame: f }));
		n++;
	}
	for (const it of m.items) {
		if (!isPoseKey(it.k)) continue;
		const hit = animAt[it.cells[0]];
		if (hit) {
			if (S.strips[it.k]) S.dirty.add(it.k);   // its old drawing goes
			delete S.strips[it.k];
			S.srcs[it.k] = { map: true };
			S.pack.poses[it.k] = { ...hit };
		} else {
			const t = tightStrip([cut(it.cells[0])], center);
			if (!t) continue;
			applyStrip(it.k, { strip: t.strip, frames: 1, src: { map: true } }, true);
		}
		n++;
	}
	// cards removed since the sheet was last used
	for (const it of before?.items || []) {
		if (m.items.some((x) => x.k === it.k) || !S.srcs[it.k]?.map) continue;
		delete S.strips[it.k];
		delete S.srcs[it.k];
		delete S.pack.anims[it.k];
		delete S.pack.poses[it.k];
		for (const p of Object.values(S.pack.poses)) if (p.anim === it.k) p.frame = 0;
		S.dirty.add(it.k);
	}
	const R = m.recipe;
	if (R) {
		if (R.paw && S.pack.anims.paw) Object.assign(S.pack.anims.paw, R.paw);
		if (R.pack.faces) S.pack.faces = R.pack.faces;
		if (R.pack.describe && !S.pack.describe) S.pack.describe = R.pack.describe;
		const ownSounds = Object.values(S.pack.sounds).some((v) => Array.isArray(v) && v.length);
		if (R.pack.sound_set && soundSet() === 'default' && !ownSounds) S.pack.sound_set = R.pack.sound_set;
	}
	S.sheetMap = m;
	S.sourceDirty = true;
	syncSource();
	changed();
	renderAll();
	select(has(S.sel) || S.pack.poses[S.sel] ? S.sel : 'idle');
	if (!hadIdle) fitZoom();
	toast(R ? `${R.name}: ${n} animations and poses, all set. Try ▶ Hear it, or play one in OBS.`
		: `Added ${n} animation${n === 1 ? '' : 's'}. Anything missing borrows from another.`);
}

function removeFrame(k, i) {
	const g = geom(k);
	if (g.n <= 1) return;
	const c = canvas(g.fw * (g.n - 1), g.fh), x = ctx2d(c);
	for (let j = 0, o = 0; j < g.n; j++) if (j !== i) x.drawImage(S.strips[k], j * g.fw, 0, g.fw, g.fh, o++ * g.fw, 0, g.fw, g.fh);
	S.strips[k] = c;
	const a = S.pack.anims[k];
	a.frames = g.n - 1;
	if (k === 'paw') {
		a.whoosh = clamp(a.whoosh - (i < a.whoosh ? 1 : 0), 0, a.frames - 1);
		a.hit = clamp(a.hit - (i < a.hit ? 1 : 0), 0, a.frames - 1);
	}
	S.scrub = -1;
	S.dirty.add(k);
	changed();
	renderInspector();
}

function removeSlot(k) {
	dropFromSource(k);
	delete S.strips[k];
	delete S.srcs[k];
	if (S.pack.anims[k]) delete S.pack.anims[k];
	if (S.pack.poses[k]) delete S.pack.poses[k];
	for (const [pk, p] of Object.entries(S.pack.poses)) if (p.anim === k) delete S.pack.poses[pk];
	S.dirty.add(k);
	changed();
	renderAll();
}

function pick(fn, accept = 'image/*,.gif,.png,.webp') {
	const inp = $('#filePick');
	inp.accept = accept;
	inp.value = '';
	inp.onchange = () => inp.files.length && fn([...inp.files]);
	inp.click();
}

// ------------------------------------------------------------------ audio

let AC = null;
const ac = () => (AC ||= new AudioContext());
const decodeAudio = (ab) => ac().decodeAudioData(ab.slice(0));
async function to48k(buf) {
	const n = Math.max(1, Math.ceil(buf.duration * 48000));
	const o = new OfflineAudioContext(2, n, 48000);
	const s = o.createBufferSource();
	s.buffer = buf;
	s.connect(o.destination);
	s.start();
	return o.startRendering();
}
function chans(buf) {
	return [buf.getChannelData(0), buf.numberOfChannels > 1 ? buf.getChannelData(1) : buf.getChannelData(0)];
}
// BS.1770 loudness (K-weighted, gated) at 48 kHz
function lufs(ch) {
	const kw = (x) => {
		const out = new Float32Array(x.length);
		const st = [[1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585],
			[1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621]];
		let src = x;
		for (const [b0, b1, b2, a1, a2] of st) {
			let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
			for (let i = 0; i < src.length; i++) {
				const v = src[i];
				const y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
				x2 = x1; x1 = v; y2 = y1; y1 = y;
				out[i] = y;
			}
			src = out;
		}
		return out;
	};
	const f = ch.map(kw);
	const len = f[0].length, B = 19200, step = 4800;
	const blocks = [];
	const ms = (a, b) => {
		let s = 0;
		for (const c of f) for (let i = a; i < b; i++) s += c[i] * c[i];
		return s / Math.max(1, b - a);
	};
	if (len < B) blocks.push(ms(0, len));
	else for (let a = 0; a + B <= len; a += step) blocks.push(ms(a, a + B));
	const L = (z) => -0.691 + 10 * Math.log10(z || 1e-12);
	let g = blocks.filter((z) => L(z) > -70);
	if (!g.length) return -70;
	const rel = L(g.reduce((a, b) => a + b, 0) / g.length) - 10;
	g = g.filter((z) => L(z) > rel);
	return g.length ? L(g.reduce((a, b) => a + b, 0) / g.length) : -70;
}
function peakOf(ch, a = 0, b = ch[0].length) {
	let p = 0;
	for (const c of ch) for (let i = a; i < b; i++) p = Math.max(p, Math.abs(c[i]));
	return p;
}
function autoTrim(ch) {
	const len = ch[0].length, p = peakOf(ch), th = Math.max(p * 0.02, 1e-4);
	let a = 0, b = len;
	const loud = (i) => Math.abs(ch[0][i]) > th || Math.abs(ch[1][i]) > th;
	while (a < len && !loud(a)) a++;
	while (b > a && !loud(b - 1)) b--;
	a = Math.max(0, a - 480);           // 10 ms before
	b = Math.min(len, b + 2400);        // 50 ms after
	return a < b ? [a, b] : [0, len];
}
// the finished sound: cut, faded at the ends, at the role's loudness (plus the user's nudge)
function render(ch, a, b, target, nudgeDb) {
	const n = b - a;
	const out = [new Float32Array(n), new Float32Array(n)];
	for (let c = 0; c < 2; c++) out[c].set(ch[c].subarray(a, b));
	const fi = Math.min(240, n >> 2), fo = Math.min(960, n >> 2);
	for (let c = 0; c < 2; c++) {
		for (let i = 0; i < fi; i++) out[c][i] *= i / fi;
		for (let i = 0; i < fo; i++) out[c][n - 1 - i] *= i / fo;
	}
	const now = lufs(out);
	let gain = Math.pow(10, (target + nudgeDb - now) / 20);
	const pk = peakOf(out) * gain;
	let limited = false;
	if (pk > 0.89) {
		gain *= 0.89 / pk;
		limited = true;
	}
	for (const c of out) for (let i = 0; i < n; i++) c[i] *= gain;
	return { out, limited, loud: now + 20 * Math.log10(gain) };
}
function encodeWav(x) {
	const ch = x instanceof AudioBuffer ? chans(x) : x;
	const n = ch[0].length, data = n * 4;
	const buf = new ArrayBuffer(44 + data), v = new DataView(buf);
	const w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
	w(0, 'RIFF'); v.setUint32(4, 36 + data, true); w(8, 'WAVE'); w(12, 'fmt ');
	v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true); v.setUint32(24, 48000, true);
	v.setUint32(28, 48000 * 4, true); v.setUint16(32, 4, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, data, true);
	let o = 44;
	for (let i = 0; i < n; i++)
		for (let c = 0; c < 2; c++) {
			v.setInt16(o, clamp(Math.round(ch[c][i] * 32767), -32768, 32767), true);
			o += 2;
		}
	return new Blob([buf], { type: 'audio/wav' });
}

let playing = null;
function stopSound() {
	if (playing) {
		try { playing.src.stop(); } catch { /* done */ }
		playing.done?.();
	}
	playing = null;
}
function playBuffer(buf, offset = 0, dur, gain = 1, done) {
	stopSound();
	const c = ac();
	if (c.state === 'suspended') c.resume();
	const src = c.createBufferSource();
	src.buffer = buf;
	const g = c.createGain();
	g.gain.value = gain;
	src.connect(g).connect(c.destination);
	const me = { src, done, t0: c.currentTime, offset };
	src.onended = () => {
		if (playing === me) {
			playing = null;
			done?.();
		}
	};
	src.start(0, offset, dur);
	playing = me;
	return me;
}
async function soundBlob(role, f) {
	if (typeof f === 'string') return (await api('file', { id: S.id, path: `sounds/${role}/${f}` })).blob();
	return (await api('sound', { id: S.id, role: f.from.role, i: f.from.i })).blob();
}
const soundName = (f) => (typeof f === 'string' ? f : f.name).replace(/\.wav$/i, '');

// The sound dialog: trim it and hear how it will sound. `ab` is the file's bytes.
async function soundDialog(role, ab, name, replacing) {
	let buf;
	try {
		buf = await to48k(await decodeAudio(ab));
	} catch {
		return toast('That file isn’t a sound this browser can read.', true);
	}
	const ch = chans(buf), len = ch[0].length;
	const target = isVoice(role) ? LUFS_VOICE : LUFS_FOLEY;
	let [a, b] = replacing ? [0, len] : autoTrim(ch);
	let nudge = 0;
	return new Promise((resolve) => {
		const wave = canvas(720, 120);
		const box = h('div', { class: 'wave' }, wave);
		const shadeL = h('div', { class: 'shade', style: 'left:0' }), shadeR = h('div', { class: 'shade', style: 'right:0' });
		const hA = h('div', { class: 'h' }), hB = h('div', { class: 'h' }), head = h('div', { class: 'head', hidden: true });
		box.append(shadeL, shadeR, hA, hB, head);
		const nameIn = h('input', { value: fileSlug(name), maxlength: 40 });
		const vol = h('input', { type: 'range', min: -9, max: 9, step: 1, value: 0 });
		const volVal = h('span', { class: 'val' });
		const meter = h('div', { class: 'meter' });
		const playBtn = h('button', { text: '▶  Play' });
		const draw = () => {
			const x = ctx2d(wave), W = wave.width, H = wave.height;
			const gain = 0.95 / Math.max(1e-4, peakOf(ch));
			x.clearRect(0, 0, W, H);
			x.fillStyle = '#6ee7b7';
			for (let px = 0; px < W; px++) {
				const s = Math.floor((px / W) * len), e = Math.max(s + 1, Math.floor(((px + 1) / W) * len));
				let mn = 1, mx = -1;
				for (let i = s; i < e; i += Math.max(1, (e - s) >> 6)) {
					const v = (ch[0][i] + ch[1][i]) / 2;
					if (v < mn) mn = v;
					if (v > mx) mx = v;
				}
				const y1 = H / 2 - mx * gain * (H / 2 - 4), y2 = H / 2 - mn * gain * (H / 2 - 4);
				x.fillRect(px, y1, 1, Math.max(1, y2 - y1));
			}
			const pa = (a / len) * 100, pb = (b / len) * 100;
			hA.style.left = pa + '%';
			hB.style.left = pb + '%';
			shadeL.style.width = pa + '%';
			shadeR.style.width = 100 - pb + '%';
			const r = render(ch, a, b, target, nudge);
			volVal.textContent = nudge === 0 ? 'usual' : (nudge > 0 ? '+' : '') + nudge + ' dB';
			meter.replaceChildren(
				h('span', {}, 'Length ', h('b', { text: ((b - a) / 48000).toFixed(2) + ' s' })),
				h('span', {}, 'Loudness ', h('b', { text: Math.round(r.loud) + ' LUFS' })),
				r.limited ? h('span', { class: 'danger', text: 'Kept from clipping: it can’t get louder' }) : h('span', { text: isVoice(role) ? 'Sits under your voice' : 'A soft body sound' }),
			);
		};
		const drag = (which) => (e) => {
			e.preventDefault();
			const r = box.getBoundingClientRect();
			const move = (ev) => {
				const v = Math.round(clamp((ev.clientX - r.left) / r.width, 0, 1) * len);
				if (which === 'a') a = clamp(v, 0, b - 480);
				else b = clamp(v, a + 480, len);
				draw();
			};
			const up = () => {
				window.removeEventListener('pointermove', move);
				window.removeEventListener('pointerup', up);
			};
			window.addEventListener('pointermove', move);
			window.addEventListener('pointerup', up);
		};
		hA.addEventListener('pointerdown', drag('a'));
		hB.addEventListener('pointerdown', drag('b'));
		vol.addEventListener('input', () => {
			nudge = +vol.value;
			draw();
		});
		const play = () => {
			if (playing) {
				stopSound();
				return;
			}
			const r = render(ch, a, b, target, nudge);
			const pb = ac().createBuffer(2, r.out[0].length, 48000);
			pb.copyToChannel(r.out[0], 0);
			pb.copyToChannel(r.out[1], 1);
			playBtn.textContent = '■  Stop';
			const me = playBuffer(pb, 0, undefined, 1, () => {
				playBtn.textContent = '▶  Play';
				head.hidden = true;
			});
			head.hidden = false;
			modalTick = () => {
				if (playing !== me) return;
				const t = (ac().currentTime - me.t0) * 48000 + a;
				head.style.left = (t / len) * 100 + '%';
			};
		};
		playBtn.addEventListener('click', play);
		const done = (v) => {
			stopSound();
			closeModal();
			resolve(v);
		};
		const keep = () => {
			const r = render(ch, a, b, target, nudge);
			done({ blob: encodeWav(r.out), name: fileSlug(nameIn.value) });
		};
		const label = ROLES.find((r) => r.k === role).name;
		openModal(
			h('h2', { text: `${replacing ? 'Change' : 'Add'} a “${label}” sound` }),
			h('p', { class: 'sub', text: 'Drag the green handles to trim it. It’s set to the same loudness as the other sounds for you.' }),
			box, meter,
			h('div', { class: 'row', style: 'margin-top:14px;gap:10px 16px;flex-wrap:wrap' }, playBtn,
				h('label', { class: 'grow', style: 'display:flex;align-items:center;gap:10px;color:var(--muted)' }, 'Volume', vol, volVal),
				h('button', { class: 'ghost small', text: 'Trim silence', onclick: () => { [a, b] = autoTrim(ch); draw(); } })),
			h('label', { class: 'form', style: 'padding:0;margin-top:14px' }, 'Name', nameIn),
			h('div', { class: 'actions' }, h('button', { text: 'Cancel', onclick: () => done(null) }),
				h('button', { class: 'primary', text: replacing ? 'Save' : 'Add sound', onclick: keep })),
		);
		draw();
		setTimeout(play, 60);
	});
}

async function addSounds(role, files) {
	if (!S.editable) return toast('Make your own copy first (the button at the top).', true);
	const auds = files.filter((f) => /^audio\//.test(f.type) || /\.(wav|mp3|ogg|oga|flac|m4a|aac|opus|webm)$/i.test(f.name));
	if (!auds.length) return toast('That isn’t a sound file.', true);
	if (auds.length === 1) {
		const r = await soundDialog(role, await auds[0].arrayBuffer(), auds[0].name, false);
		if (r) await storeSound(role, r.blob, r.name, null);
		return;
	}
	// several at once: trimmed and levelled without asking
	const target = isVoice(role) ? LUFS_VOICE : LUFS_FOLEY;
	let n = 0;
	for (const f of auds) {
		try {
			const buf = await to48k(await decodeAudio(await f.arrayBuffer()));
			const ch = chans(buf);
			const [a, b] = autoTrim(ch);
			await storeSound(role, encodeWav(render(ch, a, b, target, 0).out), fileSlug(f.name), null, true);
			n++;
		} catch { /* skip it */ }
	}
	toast(`Added ${n} sound${n === 1 ? '' : 's'}.`);
	renderSounds();
}
async function storeSound(role, blob, name, replacing, quiet) {
	if (!(await ownFirst())) return;   // (a sound goes straight into the character's folder)
	const list = Array.isArray(S.pack.sounds[role]) ? S.pack.sounds[role].filter((f) => typeof f === 'string') : [];
	const others = list.filter((f) => f !== replacing).map((f) => f.replace(/\.wav$/, ''));
	const file = uniqueName(others, name) + '.wav';
	try {
		await put(S.id, `sounds/${role}/${file}`, blob);
		if (replacing && replacing !== file) await post('del', { id: S.id, path: `sounds/${role}/${replacing}` });
	} catch (e) {
		return toast("Couldn't save the sound: " + e.message, true);
	}
	const next = replacing ? list.map((f) => (f === replacing ? file : f)) : [...list, file];
	S.pack.sounds[role] = [...new Set(next)];
	changed(false);
	if (!quiet) renderSounds();
}
async function removeSound(role, f) {
	const list = S.pack.sounds[role];
	S.pack.sounds[role] = list.filter((x) => x !== f);
	if (!S.pack.sounds[role].length) delete S.pack.sounds[role];
	// a built-in character's sound (an object) isn't a file of ours: dropping it from the list is enough,
	// and the change saves the character as your own copy without it
	if (typeof f === 'string') {
		try {
			await post('del', { id: S.id, path: `sounds/${role}/${f}` });
		} catch { /* gone already */ }
	}
	changed(false);
	renderSounds();
}

// ------------------------------------------------------------------ zip (store-only out, store/deflate in)

const CRC = (() => {
	const t = new Uint32Array(256);
	for (let n = 0; n < 256; n++) {
		let c = n;
		for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		t[n] = c >>> 0;
	}
	return t;
})();
function crc32(d) {
	let c = 0xffffffff;
	for (let i = 0; i < d.length; i++) c = CRC[(c ^ d[i]) & 0xff] ^ (c >>> 8);
	return (c ^ 0xffffffff) >>> 0;
}
function zip(files) {
	const enc = new TextEncoder(), parts = [], central = [];
	let off = 0;
	for (const f of files) {
		const name = enc.encode(f.name), crc = crc32(f.data);
		const lh = new DataView(new ArrayBuffer(30));
		lh.setUint32(0, 0x04034b50, true); lh.setUint16(4, 20, true); lh.setUint16(6, 0x800, true);
		lh.setUint16(10, 0, true); lh.setUint16(12, 0x21, true);
		lh.setUint32(14, crc, true); lh.setUint32(18, f.data.length, true); lh.setUint32(22, f.data.length, true);
		lh.setUint16(26, name.length, true);
		parts.push(lh.buffer, name, f.data);
		const ch = new DataView(new ArrayBuffer(46));
		ch.setUint32(0, 0x02014b50, true); ch.setUint16(4, 20, true); ch.setUint16(6, 20, true); ch.setUint16(8, 0x800, true);
		ch.setUint16(14, 0x21, true);
		ch.setUint32(16, crc, true); ch.setUint32(20, f.data.length, true); ch.setUint32(24, f.data.length, true);
		ch.setUint16(28, name.length, true); ch.setUint32(42, off, true);
		central.push(ch.buffer, name);
		off += 30 + name.length + f.data.length;
	}
	const size = central.reduce((s, p) => s + (p.byteLength ?? p.length), 0);
	const end = new DataView(new ArrayBuffer(22));
	end.setUint32(0, 0x06054b50, true); end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
	end.setUint32(12, size, true); end.setUint32(16, off, true);
	return new Blob([...parts, ...central, end.buffer], { type: 'application/zip' });
}
async function unzip(ab) {
	const v = new DataView(ab), u8 = new Uint8Array(ab), dec = new TextDecoder();
	let e = ab.byteLength - 22;
	while (e >= 0 && v.getUint32(e, true) !== 0x06054b50) e--;
	if (e < 0) throw new Error('that isn’t a .zip file');
	const n = v.getUint16(e + 10, true);
	let p = v.getUint32(e + 16, true);
	const out = [];
	for (let i = 0; i < n; i++) {
		const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true);
		const nl = v.getUint16(p + 28, true), xl = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true);
		const lo = v.getUint32(p + 42, true);
		const name = dec.decode(u8.subarray(p + 46, p + 46 + nl));
		const start = lo + 30 + v.getUint16(lo + 26, true) + v.getUint16(lo + 28, true);
		const raw = u8.subarray(start, start + csize);
		let data;
		if (method === 0) data = raw.slice();
		else if (method === 8) data = new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).arrayBuffer());
		if (data && !name.endsWith('/')) out.push({ name: name.replace(/\\/g, '/'), data });
		p += 46 + nl + xl + cl;
	}
	return out;
}

// ------------------------------------------------------------------ drawing: the stage and thumbnails

const BGS = ['#111111', 'checker', '#3b6e4f', '#d9dbe0'];
function paintBg(x, W, H, which) {
	const bg = BGS[which];
	if (bg === 'checker') {
		for (let y = 0; y < H; y += 16)
			for (let xx = 0; xx < W; xx += 16) {
				x.fillStyle = (xx / 16 + y / 16) % 2 ? '#1a1a1a' : '#121212';
				x.fillRect(xx, y, 16, 16);
			}
	} else {
		x.fillStyle = bg;
		x.fillRect(0, 0, W, H);
	}
}

// what the selected slot shows at time t: [strip, frame], or null
function frameNow(k, t) {
	if (POSES.some((p) => p.k === k)) {
		const f = poseFrame(k);
		return f ? [f.k, f.i] : null;
	}
	const r = resolveAnim(k);
	if (!r) return null;
	const g = geom(r);
	if (k === S.sel && S.scrub >= 0 && r === k) return [r, Math.min(S.scrub, g.n - 1)];
	const fps = S.pack.anims[r].fps || 8;
	return [r, Math.floor(t * fps) % g.n];
}

function drawStage(t) {
	const c = $('#stage'), x = ctx2d(c), W = c.width, H = c.height;
	paintBg(x, W, H, S.bg);
	const ground = H - 64;
	const light = S.bg === 3;
	x.fillStyle = light ? 'rgba(0,0,0,.12)' : 'rgba(255,255,255,.06)';
	x.fillRect(0, ground, W, H - ground);
	$('#stageEmpty').hidden = !S.pack || has('idle');
	if (!S.pack || !has('idle')) return guideLabels(W, H, []);
	const { cw, ch } = cellSize();
	const m = bodyNow(), b = m.body, z = S.zoom;
	const cx = W / 2;
	// shadow
	x.fillStyle = 'rgba(0,0,0,.34)';
	x.fillRect(cx - b.shadow * z, ground - z, b.shadow * 2 * z, z);
	x.fillStyle = 'rgba(0,0,0,.2)';
	x.fillRect(cx - (b.shadow * 2 / 3) * z, ground, (b.shadow * 4 / 3) * z, z * 0.5);
	const ox = cx - (cw * z) / 2, oy = ground - (ch - m.feet) * z;
	x.save();
	if (S.turn) {
		x.translate(W, 0);
		x.scale(-1, 1);
	}
	// a reaction (or the call) with nothing drawn and no stand-in never borrows: show nothing, like its tile
	const def = ANIMS.find((a) => a.k === S.sel);
	const off = !S.show && def && (def.group === 'reaction' || S.sel === 'call') && !hasOrStand(S.sel);
	const f = off ? null : frameNow(S.show?.k || S.sel, t);
	if (S.ghost && !S.show && f && f[0] !== 'idle' && S.tab === 'anims') {
		x.globalAlpha = 0.28;
		drawPet(x, 'idle', 0, ox, oy, z, cw, ch);
		x.globalAlpha = 1;
	}
	if (f) drawPet(x, f[0], f[1], ox, oy, z, cw, ch);
	x.restore();
	if (S.guides) drawGuides(x, cx, ground, z, m, cw, ch);
	else guideLabels(W, H, []);
	const borrowed = !POSES.some((p) => p.k === S.sel) && f && f[0] !== S.sel && !standIn(S.sel);
	const nm = [...ANIMS, ...POSES].find((a) => a.k === S.sel)?.name;
	const by = f && [...ANIMS, ...POSES].find((a) => a.k === f[0])?.name;
	if (S.show) $('#playing').textContent = '\u266A ' + VOICE.find((v) => v.k === S.show.role).name;
	else if (off) $('#playing').textContent = S.sel === 'call' ? `${nm} (sound only)` : `${nm} (off: no stand-in)`;
	else $('#playing').textContent = borrowed ? `${nm} (borrows ${by})` : nm || '';
}

// guide labels are HTML over the canvas: canvas text blurs when the stage is scaled down
function guideLabels(W, H, list) {
	const box = $('#stageLabels');
	const key = JSON.stringify(list);
	if (box.dataset.key === key) return;
	box.dataset.key = key;
	box.replaceChildren(...list.map(([text, color, px, py, mid]) => h('span', {
		class: mid ? 'mid' : '', text,
		style: `color:${color};left:${(px / W) * 100}%;top:${(py / H) * 100}%`,
	})));
}

function drawGuides(x, cx, ground, z, m, cw, ch) {
	const b = m.body, dir = (S.pack.faces === 'left') !== S.turn ? -1 : 1;
	const W = x.canvas.width, H = x.canvas.height, labels = [];
	x.save();
	x.lineWidth = 1;
	const hline = (y, color, label, dash) => {
		x.strokeStyle = color;
		x.setLineDash(dash ? [4, 4] : []);
		x.beginPath();
		x.moveTo(20, Math.round(y) + 0.5);
		x.lineTo(W - 20, Math.round(y) + 0.5);
		x.stroke();
		labels.push([label, color, 20, Math.round(y) - 2]);
	};
	hline(ground, '#6ee7b7', 'floor');
	hline(ground - b.top * z, '#fbbf24', 'top of head: hearts and “!” go above');
	hline(ground - b.top_asleep * z, '#a78bfa', 'asleep', true);
	x.setLineDash([3, 3]);
	x.strokeStyle = 'rgba(255,255,255,.4)';
	x.strokeRect(cx - b.half * z, ground - b.top * z, b.half * 2 * z, b.top * z);
	x.setLineDash([]);
	// head and paw
	x.fillStyle = '#fbbf24';
	x.fillRect(cx + dir * b.head * z - 3, ground - b.top * z - 3, 6, 6);
	x.fillStyle = '#f87171';
	x.beginPath();
	const px = cx + dir * b.paw[0] * z, py = ground - b.paw[1] * z;
	x.arc(px, py, 4, 0, Math.PI * 2);
	x.fill();
	labels.push(['swat', '#f87171', px + (dir > 0 ? 8 : -46), py, true]);
	// the cell
	x.strokeStyle = 'rgba(255,255,255,.12)';
	x.strokeRect(cx - (cw * z) / 2, ground - (ch - m.feet) * z, cw * z, ch * z);
	x.restore();
	guideLabels(W, H, labels);
}

function drawThumb(c, k, t) {
	const x = ctx2d(c), W = c.width, H = c.height;
	x.clearRect(0, 0, W, H);
	if (!S.pack || !has('idle') && !has(k)) return;
	const def = ANIMS.find((a) => a.k === k);
	if (def && (def.group === 'reaction' || k === 'call') && !hasOrStand(k)) return;   // (never borrows: nothing to show)
	const f = frameNow(k, t);
	if (!f) return;
	const { cw, ch } = cellSize();
	const m = measure();
	const z = Math.max(1, Math.floor(Math.min((W - 8) / cw, (H - 10) / (m.body.top + 3))));
	const own = POSES.some((p) => p.k === k) ? S.pack.poses[k] : hasOrStand(k);
	x.globalAlpha = own ? 1 : 0.32;
	drawPet(x, f[0], f[1], (W - cw * z) / 2, H - 6 - (ch - m.feet) * z, z, cw, ch);
	x.globalAlpha = 1;
}

function loop(now) {
	const t = now / 1000;
	if (active) {
		if (S.pack) {
			drawStage(t);
			if (S.tab === 'anims') for (const c of document.querySelectorAll('canvas[data-thumb]')) drawThumb(c, c.dataset.thumb, t);
		}
		if (modalTick) modalTick();
		fillRanges();
	}
	requestAnimationFrame(loop);
}
// the settings page's sliders fill up to their value from --pct: keep it in step with every slider
// here, however its value got set
function fillRanges() {
	for (const r of document.querySelectorAll('.ed input[type=range]')) {
		if (r._pctOf === r.value) continue;
		r._pctOf = r.value;
		const min = +r.min || 0, max = r.max === '' ? 100 : +r.max;
		r.style.setProperty('--pct', (max > min ? clamp((r.value - min) / (max - min), 0, 1) * 100 : 0).toFixed(1) + '%');
	}
}
function paint() {
	renderSlots();
}

// ------------------------------------------------------------------ the page

// a zoom where the pet is a good size on the stage
function fitZoom() {
	version++;
	if (!S.pack || !has('idle')) return;
	const m = measure();
	S.zoom = clamp(Math.floor(130 / Math.max(8, m.body.top)), 1, 10);
}

function select(k) {
	S.sel = k;
	S.scrub = -1;
	renderSlots();
	renderInspector();
}

function renderAll() {
	version++;
	$('#edApp').classList.toggle('readonly', !S.editable);
	renderSlots();
	renderInspector();
	renderSounds();
	renderDetails();
	renderColors();
}

function slotState(k) {
	if (POSES.some((p) => p.k === k)) {
		const p = S.pack.poses[k];
		if (p && p.own && S.strips[k]) return { own: true, text: 'Its own drawing' };
		if (p && p.anim && has(p.anim)) return { own: true, text: `${ANIMS.find((a) => a.k === p.anim).name}, frame ${(p.frame | 0) + 1}` };
		return { own: false, text: 'Automatic' };
	}
	if (has(k)) {
		const a = S.pack.anims[k];
		return { own: true, text: `${geom(k).n} frame${geom(k).n === 1 ? '' : 's'} · ${round1(a.fps)} fps` };
	}
	const def = ANIMS.find((a) => a.k === k);
	if (def.req) return { own: false, text: 'Needed: drop one here' };
	const name = (x) => ANIMS.find((a) => a.k === x).name;
	if (standIn(k)) return { own: true, stand: true, text: `${name(standIn(k))} stands in` };
	const off = NEEDED_BY[k];
	const offTxt = off && (off.length === 1 ? `${off[0]} off` : `${off.length} reactions off`);
	if (def.group === 'reaction') return { own: false, need: true, text: off.length === 1 ? `Off: ${off[0]}` : `Off: ${off.join(', ')}` };
	if (k === 'call') return { own: false, text: 'None: sound only' };
	const r = resolveAnim(k);
	const by = r ? `Borrows ${name(r)}` : 'Empty';
	return { own: false, need: !!off, text: off ? `${by} · ${offTxt}` : by };
}

function slotCard(def) {
	const k = def.k;
	const st = slotState(k);
	const isPose = POSES.some((p) => p.k === k);
	const el = h('button', {
		class: `slot${S.sel === k ? ' sel' : ''}${st.own ? '' : ' empty'}${def.req ? ' req' : ''}${st.need ? ' need' : ''}${st.stand ? ' stand' : ''}`,
		title: `${def.what}${NEEDED_BY[k] ? `
Needed for ${NEEDED_BY[k].join(', ')}` : ''}
${st.text}`,
		onclick: () => select(k),
	},
	h('canvas', { width: isPose ? 120 : 160, height: isPose ? 72 : 92, 'data-thumb': k }),
	!st.own && (!has('idle') || def.group === 'reaction' || k === 'call') ? h('div', { class: 'plus', text: '+ Drop here' }) : null,
	h('div', { class: 'meta' }, h('div', { class: 'name', text: def.name }), isPose ? null : h('div', { class: 'what', text: def.what }), h('div', { class: 'state', text: st.text })));
	dropTarget(el, (files) => importInto(k, files));
	return el;
}

function renderSlots() {
	if (!S.pack) return;
	const bar = $('#sheetBar');
	bar.hidden = !S.editable;
	bar.replaceChildren(...[
		h('div', { class: 'grow' }, h('b', { text: 'One image with every animation?' }), ' ', h('span', { class: 'muted', text: 'Add them all at once.' })),
		S.sheetMap ? h('button', { text: 'Edit sheet', title: S.sheetMap.name, onclick: () => openSheet(null, S.sel) }) : null,
		h('button', { class: S.sheetMap ? 'ghost' : 'primary', text: S.sheetMap ? 'Another sheet…' : 'Add a sprite sheet…', onclick: () => pick(sheetFromFiles) })].filter(Boolean));
	$('#slots').replaceChildren(...ANIMS.filter((a) => !a.group).map(slotCard));
	$('#slotsReact').replaceChildren(...ANIMS.filter((a) => a.group === 'reaction').map(slotCard));
	$('#poses').replaceChildren(...POSES.map(slotCard));
	const badge = $('#navAnims');   // (in the view's side menu: how many of its animations it has)
	badge.hidden = false;
	badge.textContent = `${ANIMS.filter((a) => has(a.k)).length}/${ANIMS.length}`;
}

function field(label, input, val) {
	return h('div', { class: 'field' }, h('span', { text: label }), input, val || h('span'));
}
function slider(min, max, step, value, on, fmt) {
	const v = h('span', { class: 'val', text: fmt(value) });
	const r = h('input', { type: 'range', min, max, step, value });
	r.addEventListener('input', () => {
		v.textContent = fmt(+r.value);
		on(+r.value);
	});
	return [r, v];
}
function nudger(m) {
	const val = h('span', { class: 'val' });
	const show = () => (val.textContent = `${m.dx | 0}, ${m.dy | 0}`);
	const step = (dx, dy) => () => {
		m.dx = (m.dx | 0) + dx;
		m.dy = (m.dy | 0) + dy;
		show();
		changed();
	};
	show();
	return h('div', { class: 'nudge' },
		h('button', { text: '←', title: 'Move left', onclick: step(-1, 0) }),
		h('button', { text: '→', title: 'Move right', onclick: step(1, 0) }),
		h('button', { text: '↑', title: 'Move up', onclick: step(0, 1) }),
		h('button', { text: '↓', title: 'Move down', onclick: step(0, -1) }),
		val,
		h('button', { class: 'ghost small', style: 'width:auto', text: 'Reset', onclick: () => { m.dx = m.dy = 0; show(); changed(); } }));
}

const showBtn = (k) => h('button', { class: 'ghost', text: 'Show file',
	title: `Open its folder with ${k}.png selected: its frames side by side, to edit in any art program`, onclick: () => showFile(k) });
function renderInspector() {
	const box = $('#inspector');
	if (!S.pack) return box.replaceChildren();
	const k = S.sel;
	const isPose = POSES.some((p) => p.k === k);
	const def = [...ANIMS, ...POSES].find((a) => a.k === k);
	const ro = !S.editable;
	const parts = [];

	if (isPose) {
		const p = S.pack.poses[k] || {};
		const own = p.own && S.strips[k];
		const opts = [h('option', { value: '', text: 'Automatic' })];
		for (const a of ANIMS) if (has(a.k)) opts.push(h('option', { value: a.k, text: a.name }));
		const animSel = h('select', { disabled: ro }, ...opts);
		animSel.value = own ? '' : p.anim && has(p.anim) ? p.anim : '';
		const frameSel = h('select', { disabled: ro || !animSel.value });
		const fillFrames = () => {
			frameSel.replaceChildren();
			if (!animSel.value) return;
			for (let i = 0; i < geom(animSel.value).n; i++) frameSel.append(h('option', { value: i, text: `Frame ${i + 1}` }));
			frameSel.value = String(clamp(p.frame | 0, 0, geom(animSel.value).n - 1));
		};
		fillFrames();
		animSel.addEventListener('change', () => {
			if (animSel.value) S.pack.poses[k] = { anim: animSel.value, frame: 0 };
			else delete S.pack.poses[k];
			if (S.strips[k]) {
				delete S.strips[k];
				S.dirty.add(k);
			}
			changed();
			renderInspector();
		});
		frameSel.addEventListener('change', () => {
			S.pack.poses[k].frame = +frameSel.value;
			changed();
		});
		parts.push(h('div', { class: 'insp-head' }, h('h3', { text: def.name }), h('span', { class: 'muted', text: def.what }), h('div', { class: 'grow' }),
			own && !ro ? showBtn(k) : null,
			ro || !S.sheetMap ? null : h('button', { text: 'Pick it from your sheet', onclick: () => openSheet(null, k) }),
			ro ? null : h('button', { text: own ? 'Replace…' : 'Use its own image…', onclick: () => pick((f) => importInto(k, f)) }),
			own && !ro ? h('button', { class: 'ghost danger', text: 'Remove', onclick: () => removeSlot(k) }) : null));
		if (!own) {
			parts.push(field('Frame from', animSel, null), animSel.value ? field('', frameSel) : h('p', { class: 'hint', text: `Automatic uses ${POSES.find((q) => q.k === k).fb === 'idle' ? 'the first idle frame' : 'the ' + POSES.find((q) => q.k === k).fb + ' pose'}.` }));
		} else if (!ro) {
			parts.push(field('Position', nudger(S.pack.poses[k])));
			parts.push(field('Mirror', h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: !!p.flip, onchange: (e) => { p.flip = e.target.checked; changed(); } }), 'Faces the other way')));
		}
		return box.replaceChildren(...parts);
	}

	if (!has(k)) {
		const r = resolveAnim(k);
		const by = r && ANIMS.find((a) => a.k === r).name;
		const need = NEEDED_BY[k], stand = standIn(k);
		const why = def.req ? 'Every character needs this one. Everything else can borrow from it.'
			: stand ? `${ANIMS.find((a) => a.k === stand).name} stands in for it${need ? `, so ${need.join(', ')} can play` : ''}. Add one of its own any time.`
			: def.group === 'reaction' ? `Without one, ${need.join(', ')} stay${need.length === 1 ? 's' : ''} off: a borrowed animation would look wrong.`
			: by ? `Until you add one, it borrows ${by}${need ? `, and ${need.join(', ')} stay${need.length === 1 ? 's' : ''} off` : ''}.` : 'Add idle first.';
		// another animation in its place: the reactions that need this one come on, and the tile says which it is
		const standSel = def.req ? null : h('select', {
			disabled: ro,
			onchange: (e) => {
				S.pack.stands = S.pack.stands || {};
				if (e.target.value) S.pack.stands[k] = e.target.value;
				else delete S.pack.stands[k];
				changed(false);
				renderSlots();
				renderInspector();
			},
		}, h('option', { value: '', text: def.group === 'reaction' ? 'None (reactions stay off)' : 'None (borrows automatically)' }),
		...ANIMS.filter((a) => a.k !== k && has(a.k)).map((a) => h('option', { value: a.k, text: a.name, selected: stand === a.k })));
		parts.push(h('div', { class: 'empty-insp' },
			h('div', { class: 'big', html: `<b>${def.name}</b>: ${escapeHtml(def.what.toLowerCase())}` }),
			h('div', { class: 'muted', text: why }),
			standSel ? h('label', { class: 'stand-row' }, h('span', { text: 'Stand-in' }), standSel) : null,
			ro ? null : h('div', { class: 'row' },
				S.sheetMap ? h('button', { class: 'primary', text: 'Pick it from your sheet', onclick: () => openSheet(null, k) }) : null,
				h('button', { class: S.sheetMap ? '' : 'primary', text: 'Choose an image…', onclick: () => pick((f) => importInto(k, f)) }),
				h('span', { class: 'muted small', text: 'or drop one on its tile' }))));
		return box.replaceChildren(...parts);
	}

	const a = S.pack.anims[k], g = geom(k);
	parts.push(h('div', { class: 'insp-head' }, h('h3', { text: def.name }), h('span', { class: 'muted', text: def.what }), h('div', { class: 'grow' }),
		ro ? null : showBtn(k),
		ro ? null : h('button', { text: 'Replace…', onclick: () => pick((f) => importInto(k, f)) }),
		ro || !S.srcs[k] ? null : h('button', { class: 'ghost', text: 'Change frames', onclick: () => changeFrames(k) }),
		ro || def.req ? null : h('button', { class: 'ghost danger', text: 'Remove', onclick: () => removeSlot(k) })));
	const [fr, fv] = slider(1, 24, 0.5, a.fps || 8, (v) => { a.fps = v; changed(false); }, (v) => `${v} fps`);
	fr.disabled = ro;
	parts.push(field('Speed', fr, fv));
	if (!ro) {
		parts.push(field('Position', nudger(a)));
		parts.push(field('', h('div', { class: 'row', style: 'gap:18px' },
			h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: !!a.flip, onchange: (e) => { a.flip = e.target.checked; changed(); } }), 'Mirror it'),
			k !== 'idle' ? h('label', { class: 'switch' }, h('input', { type: 'checkbox', checked: S.ghost, onchange: (e) => (S.ghost = e.target.checked) }), 'Show idle behind it') : null)));
	}
	if (k === 'paw') {
		const opt = () => [...Array(g.n).keys()].map((i) => h('option', { value: i, text: `Frame ${i + 1}` }));
		const ws = h('select', { disabled: ro }, ...opt()), hs = h('select', { disabled: ro }, ...opt());
		ws.value = String(a.whoosh | 0);
		hs.value = String(a.hit | 0);
		ws.addEventListener('change', () => { a.whoosh = +ws.value; changed(false); renderFrames(); });
		hs.addEventListener('change', () => { a.hit = +hs.value; changed(false); renderFrames(); });
		parts.push(field('Swipe sound on', ws), field('Sparks on', hs));
	}
	const framesBox = h('div', { class: 'frames', id: 'framesBox' });
	parts.push(h('div', { class: 'field', style: 'align-items:start' }, h('span', { text: `Frames (${g.n})` }), framesBox, h('span')));
	box.replaceChildren(...parts);
	renderFrames();
}

function renderFrames() {
	const fb = $('#framesBox');
	if (!fb) return;
	const k = S.sel, g = geom(k), a = S.pack.anims[k];
	const { cw, ch } = cellSize();
	fb.replaceChildren(...[...Array(g.n).keys()].map((i) => {
		const c = canvas(52, 52), x = ctx2d(c);
		const fit = Math.min(48 / cw, 48 / ch), zz = fit >= 1 ? Math.floor(fit) : fit;
		drawPet(x, k, i, (52 - cw * zz) / 2, 50 - ch * zz, zz, cw, ch);
		const tags = [];
		if (k === 'paw' && i === (a.whoosh | 0)) tags.push(h('span', { class: 'tag', text: 'swipe' }));
		if (k === 'paw' && i === (a.hit | 0)) tags.push(h('span', { class: 'tag hit', text: 'sparks', style: i === (a.whoosh | 0) ? 'bottom:12px' : '' }));
		const b = h('button', {
			class: 'frame' + (S.scrub === i ? ' sel' : ''), title: S.editable ? 'Click to hold this frame. Right-click to remove it.' : 'Click to hold this frame',
			onclick: () => { S.scrub = S.scrub === i ? -1 : i; renderFrames(); },
			oncontextmenu: (e) => {
				e.preventDefault();
				if (S.editable && g.n > 1) removeFrame(k, i);
			},
		}, c, h('span', { class: 'n', text: i + 1 }), ...tags);
		return b;
	}));
}

function renderSounds() {
	if (!S.pack) return;
	const row = (def) => {
		const v = S.pack.sounds[def.k];
		const files = Array.isArray(v) ? v : [];
		const shared = typeof v === 'string' ? v.slice(1) : '';
		const right = h('div', { class: 'files' });
		const playBtn = (blob) => {
			const btn = h('button', { class: 'play', text: '▶', title: 'Play' });
			btn.addEventListener('click', async () => {
				if (btn.classList.contains('on')) return stopSound();
				try {
					const buf = await decodeAudio(await (await blob()).arrayBuffer());
					document.querySelectorAll('#editorView .sfile .play.on').forEach((b) => { b.classList.remove('on'); b.textContent = '▶'; });
					btn.classList.add('on');
					btn.textContent = '■';
					playBuffer(buf, 0, undefined, 1, () => { btn.classList.remove('on'); btn.textContent = '▶'; });
				} catch (e) {
					toast("Couldn't play it: " + e.message, true);
				}
			});
			return btn;
		};
		for (const f of files) {
			const nm = soundName(f);
			const btn = playBtn(() => soundBlob(def.k, f));
			const name = h('button', { text: nm, title: S.editable ? 'Trim or change its volume' : '', disabled: !S.editable });
			name.addEventListener('click', async () => {
				let g = f;
				if (typeof g !== 'string') {
					// a built-in character's sound: make the own copy first, then edit that copy's file
					const i = files.indexOf(f);
					if (!(await ownFirst())) return;
					g = S.pack.sounds[def.k]?.[i];
					renderSounds();
					if (typeof g !== 'string') return;
				}
				const ab = await (await soundBlob(def.k, g)).arrayBuffer();
				const r = await soundDialog(def.k, ab, soundName(g), true);
				if (r) await storeSound(def.k, r.blob, r.name, g);
			});
			right.append(h('span', { class: 'sfile' }, btn, name,
				S.editable ? h('button', { class: 'x', text: '×', title: 'Remove', onclick: () => removeSound(def.k, f) }) : null));
		}
		if (S.editable) right.append(h('button', { class: 'small', text: '+ Add', onclick: () => pick((fs) => addSounds(def.k, fs), 'audio/*') }));
		if (!files.length) {
			// nothing of its own: the plugin's default sound, silence, or another role's sounds
			const [dset, dflt] = builtin(def.k);
			const sel = h('select', { disabled: !S.editable },
				dflt.length ? h('option', { value: '', text: `Built-in: ${setName(dset)}` }) : null,
				h('option', { value: dflt.length ? '-' : '', text: 'Silent' }),
				...ROLES.filter((r) => r.k !== def.k && Array.isArray(S.pack.sounds[r.k]) && S.pack.sounds[r.k].length)
					.map((r) => h('option', { value: r.k, text: `Same as ${r.name}` })));
			sel.value = shared || (v === false && dflt.length ? '-' : '');
			sel.addEventListener('change', () => {
				if (sel.value === '-') S.pack.sounds[def.k] = false;
				else if (sel.value) S.pack.sounds[def.k] = '@' + sel.value;
				else delete S.pack.sounds[def.k];
				changed(false);
				renderSounds();
			});
			right.append(sel);
			if (sel.value === '' && dflt.length) {
				right.append(h('span', { class: 'sfile dflt', title: `OBSPETS's built-in ${setName(dset).toLowerCase()} sounds` },
					...dflt.map((_, i) => playBtn(() => api('default-sound', { set: dset, role: def.k, i }).then((r) => r.blob())))));
			}
		}
		const el = h('div', { class: 'role' }, h('div', {}, h('div', { class: 'title', text: def.name }), h('div', { class: 'what', text: def.what })), right);
		dropTarget(el, (fs) => addSounds(def.k, fs));
		return el;
	};
	// which built-in sounds fill the roles it has none of its own for
	const sets = [...new Set([...Object.keys(S.defaults), soundSet()])].filter((k) => k !== 'none');
	const setSel = h('select', { disabled: !S.editable },
		...sets.map((k) => h('option', { value: k, text: setName(k) })), h('option', { value: 'none', text: 'None (silent)' }));
	setSel.value = soundSet();
	setSel.addEventListener('change', () => {
		S.pack.sound_set = setSel.value;
		changed(false);
		renderSounds();
	});
	$('#soundset').replaceChildren(h('div', {}, h('div', { class: 'title', text: 'Built-in sounds' }),
		h('div', { class: 'what', text: 'What plays for a role with no sounds of its own' })), h('div', { class: 'files' }, setSel));
	$('#voice').replaceChildren(...VOICE.map(row));
	$('#foley').replaceChildren(...FOLEY.map(row));
}

function renderDetails() {
	if (!S.pack) return;
	const p = S.pack, ro = !S.editable;
	const bind = (id, key) => {
		const el = $(id);
		el.value = p[key] || '';
		el.disabled = ro;
		el.oninput = () => {
			p[key] = el.value;
			if (key === 'name') {
				const o = [...$('#pack').options].find((x) => x.value === S.id);
				if (o) o.textContent = el.value;
			}
			changed(false);
		};
	};
	bind('#f-name', 'name');
	bind('#f-describe', 'describe');
	bind('#f-pronouns', 'pronouns');
	bind('#f-personality', 'personality');
	for (const b of $('#f-faces').children) {
		b.classList.toggle('on', (p.faces || 'right') === b.dataset.v);
		b.disabled = ro;
		b.onclick = () => {
			p.faces = b.dataset.v;
			changed(false);
			renderDetails();
		};
	}
	const auto = p.body.auto !== false;
	$('#f-auto').checked = auto;
	$('#f-auto').disabled = ro;
	$('#f-auto').onchange = (e) => {
		if (e.target.checked) p.body = { auto: true };
		else p.body = { auto: false, ...measure().body };
		changed(false);
		renderDetails();
	};
	const m = bodyNow();
	const b = m.body;
	const bf = $('#bodyFields');
	bf.classList.toggle('off', auto || ro);
	const max = Math.max(m.cw, m.ch);
	const fld = (label, get, set) => {
		const [r, v] = slider(0, max, 1, get(), (val) => { set(val); changed(false); }, (val) => `${val} px`);
		return field(label, r, v);
	};
	const setB = (key) => (v) => (p.body[key] = v);
	bf.replaceChildren(
		fld('Half its width', () => b.half, setB('half')), fld('Top of its head', () => b.top, setB('top')),
		fld('…while asleep', () => b.top_asleep, setB('top_asleep')), fld('Head in front by', () => b.head, setB('head')),
		fld('Swat reaches', () => b.paw[0], (v) => (p.body.paw = [v, (p.body.paw || b.paw)[1]])),
		fld('Swat height', () => b.paw[1], (v) => (p.body.paw = [(p.body.paw || b.paw)[0], v])),
		fld('Shadow (half)', () => b.shadow, setB('shadow')));
	const sp = speedNow(m.ch);
	const spf = (label, key) => {
		const [r, v] = slider(1, Math.max(200, sp.def[key] * 3), 1, sp[key], (val) => {
			p.speed = { ...(p.speed || {}), [key]: val };
			changed(false);
		}, (val) => `${val} px/s`);
		r.disabled = ro;
		return field(label, r, v);
	};
	$('#speedFields').replaceChildren(spf('Walking', 'walk'), spf('Running', 'run'));
	$('#speedReset').disabled = ro;
	$('#speedReset').onclick = () => {
		p.speed = null;
		changed(false);
		renderDetails();
	};
	$('#cellInfo').textContent = has('idle')
		? `Every frame sits in a ${m.cw} × ${m.ch} px box. Motion is tuned for a 32 px tall pet and scales with this height.`
		: '';
}

function glyphCanvas(name, color, color2) {
	const rows = GLYPHS[name];
	const c = canvas(40, 40), x = ctx2d(c);
	const w = Math.max(...rows.map((r) => r.length)), hh = rows.length;
	const z = Math.floor(Math.min(32 / w, 32 / hh));
	const ox = Math.floor((40 - w * z) / 2), oy = Math.floor((40 - hh * z) / 2);
	rows.forEach((r, y) => [...r].forEach((ch, xx) => {
		if (ch !== '#') return;
		x.fillStyle = color2 && name === 'heart' && y === 1 && xx === 1 ? color2 : color;
		x.fillRect(ox + xx * z, oy + y * z, z, z);
	}));
	return c;
}
// One row per mark. A heart is one color plus the shine pixel on it; music notes alternate between two.
const COLOR_ROWS = [
	{ name: 'Hearts', g: 'heart', keys: [['heart', 'Color'], ['glint', 'Shine']] },
	{ name: 'Music notes', g: 'note', keys: [['note', 'Color'], ['note2', 'Every other note']] },
	{ name: 'Alert “!”', g: 'bang', keys: [['bang']] },
	{ name: 'Anger mark', g: 'anger', keys: [['anger']] },
	{ name: 'Question “?”', g: 'what', keys: [['mark']] },
	{ name: 'Thinking “…”', g: 'dots', keys: [['dots']] },
	{ name: 'Sparks', g: 'spark', keys: [['spark']] },
	{ name: "Sleep z's", g: 'z', keys: [['z']] },
];
function renderColors() {
	if (!S.pack) return;
	const p = S.pack;
	const col = (k) => p.colors[k] || COLORS.find((c) => c.k === k).def;
	renderLook();
	$('#colors').replaceChildren(...COLOR_ROWS.map((r) => {
		const icon = h('div', { class: 'glyph' });
		const draw = () => icon.replaceChildren(r.g === 'heart' ? glyphCanvas('heart', col('heart'), col('glint')) : glyphCanvas(r.g, col(r.keys[0][0])));
		draw();
		const swatches = r.keys.map(([k, label]) => {
			const inp = h('input', { type: 'color', value: col(k), disabled: !S.editable, title: label || r.name, 'aria-label': `${r.name}${label ? ': ' + label : ''}` });
			// (only the preview redraws: rebuilding the row would cut off the color picker that's open)
			inp.addEventListener('input', () => {
				p.colors[k] = inp.value;
				changed(false);
				draw();
			});
			return h('label', { class: 'swatch' }, inp, label ? h('span', { text: label }) : null);
		});
		return h('div', { class: 'color' }, icon, h('div', { class: 'lbl', text: r.name }), h('div', { class: 'swatches' }, ...swatches));
	}));
}

// The pet's own colors (the look): what the Pet does to its drawings' colors, live on the Pet.
// The marks keep theirs (renderColors).
const TINTS = [['Cream', '#f6e3bf'], ['Golden', '#e8b25e'], ['Ginger', '#de8a4a'], ['Brown', '#8c5c3c'], ['Grey', '#a3a3ad'], ['Black', '#3b3b42'], ['Pink', '#f2abc4'], ['Blue', '#8fb6f2']];
function renderLook() {
	if (!S.pack) return;
	const p = S.pack, ro = !S.editable;
	const redraws = [];
	const redraw = () => redraws.forEach((f) => f());
	const petIcon = (now) => {
		const c = canvas(56, 56);
		redraws.push(() => {
			const x = ctx2d(c);
			x.clearRect(0, 0, 56, 56);
			if (!has('idle')) return;
			const { cw, ch } = cellSize();
			const z = Math.max(1, Math.floor(Math.min(56 / cw, 56 / ch)));
			if (now) drawPet(x, 'idle', 0, (56 - cw * z) / 2, 56 - ch * z, z, cw, ch);
			else drawCell(x, 'idle', 0, (56 - cw * z) / 2, 56 - ch * z, z, cw, ch);
		});
		return c;
	};
	const resetAll = h('button', { class: 'ghost small', text: 'Reset all', onclick: () => {
		Object.assign(p, lookDefaults());
		changed(false);
		renderLook();
	} });
	const rowResets = [];
	const touch = () => {
		changed(false);
		redraw();
		resetAll.disabled = ro || !lookAny(lookNow());
		rowResets.forEach((f) => f());
	};
	const head = h('div', { class: 'color lookhead' },
		h('figure', {}, petIcon(false), h('figcaption', { class: 'muted small', text: 'As drawn' })),
		h('span', { class: 'muted', text: '→' }),
		h('figure', {}, petIcon(true), h('figcaption', { class: 'muted small', text: 'On stream' })),
		h('div', { class: 'lbl muted small', text: 'Changes apply in this order, top to bottom. The preview on the left shows them too.' }),
		resetAll);

	// a slider row; the pack keeps value / scale (hue in degrees, the others as fractions)
	const sliderRow = (name, hint, key, min, max, def, scale, fmt) => {
		const [r, v] = slider(min, max, 1, Math.round(lookNow()[key] * scale), (n) => {
			p[key] = n / scale;
			touch();
		}, fmt);
		r.disabled = ro;
		r.setAttribute('aria-label', name);
		const set = (n) => {
			if (ro) return;
			r.value = n;
			r.dispatchEvent(new Event('input'));
		};
		r.addEventListener('dblclick', () => set(def));
		const reset = h('button', { class: 'ghost small', text: 'Reset', onclick: () => set(def) });
		const upd = () => (reset.disabled = ro || +r.value === def);
		rowResets.push(upd);
		upd();
		return h('div', { class: 'color hue' },
			h('div', { class: 'lbl' }, h('div', { text: name }), h('div', { class: 'muted small', text: hint })),
			h('div', { class: 'swatches' }, r, v, reset));
	};
	const pct = (v) => v + '%';
	const signed = (u) => (v) => (v > 0 ? '+' : '') + v + u;

	// tint: a color multiplied in, with how much; quick picks for common coats
	const L0 = lookNow();
	const tintIn = h('input', { type: 'color', value: L0.tint, disabled: ro, 'aria-label': 'Tint color' });
	const [amt, amtV] = slider(0, 100, 1, Math.round(L0.tint_amount * 100), (n) => {
		p.tint_amount = n / 100;
		touch();
	}, pct);
	amt.disabled = ro;
	amt.setAttribute('aria-label', 'Tint amount');
	const setTint = (hex) => {
		p.tint = hex;
		tintIn.value = hex;
		if (!(lookNow().tint_amount > 0)) {
			amt.value = 100;
			amt.dispatchEvent(new Event('input'));
		} else touch();
	};
	tintIn.addEventListener('input', () => setTint(tintIn.value));
	const tintReset = h('button', { class: 'ghost small', text: 'Reset', onclick: () => {
		if (ro) return;
		amt.value = 0;
		amt.dispatchEvent(new Event('input'));
	} });
	rowResets.push(() => (tintReset.disabled = ro || +amt.value === 0));
	const tintRow = h('div', { class: 'color hue tint' },
		h('div', { class: 'lbl' }, h('div', { text: 'Tint' }),
			h('div', { class: 'muted small', text: 'Colors a white or grey pet: white takes the tint, shading stays.' }),
			h('div', { class: 'presets' }, ...TINTS.map(([nm, hex]) => h('button', { class: 'dot', title: nm, 'aria-label': `Tint: ${nm}`, disabled: ro, style: `background:${hex}`, onclick: () => setTint(hex) })))),
		h('div', { class: 'swatches' }, h('label', { class: 'swatch' }, tintIn), amt, amtV, tintReset));

	// swap colors: every color the drawings use, most pixels first
	const MAX = 32;
	const cols = drawnColors();
	const palette = h('div', { class: 'swatches wrap' });
	p.recolor = p.recolor && typeof p.recolor === 'object' ? p.recolor : {};
	for (const { hex, n } of cols.slice(0, MAX)) {
		const inp = h('input', { type: 'color', value: lookNow().recolor[hex] || hex, disabled: ro, title: `Drawn as ${hex} (${n} px). Right-click to put it back.`, 'aria-label': `Color ${hex}` });
		const lab = h('label', { class: 'swatch pal' + (lookNow().recolor[hex] ? ' changed' : '') }, inp);
		const setTo = (v) => {
			if (v.toLowerCase() === hex) delete p.recolor[hex];
			else p.recolor[hex] = v.toLowerCase();
			lab.classList.toggle('changed', !!p.recolor[hex]);
			touch();
		};
		inp.addEventListener('input', () => setTo(inp.value));
		lab.addEventListener('contextmenu', (e) => {
			e.preventDefault();
			if (ro) return;
			inp.value = hex;
			setTo(hex);
		});
		palette.append(lab);
	}
	const more = cols.length - MAX;
	const palRow = h('div', { class: 'color hue align-top' },
		h('div', { class: 'lbl' }, h('div', { text: 'Swap colors' }),
			h('div', { class: 'muted small', text: cols.length ? 'Each color in the drawings, most used first. Pick a new one for any of them; right-click one to put it back.' : 'Add an idle animation first.' }),
			more > 0 ? h('div', { class: 'muted small', text: `And ${more} more (soft edges and shading): the sliders change those.` }) : null),
		palette);

	$('#look').replaceChildren(head, palRow,
		sliderRow('Hue shift', 'Turns every color around the color wheel. Black, grey and white stay.', 'hue', -180, 180, 0, 1, signed('°')),
		sliderRow('Saturation', 'Less: greyer. More: more vivid.', 'saturation', 0, 200, 100, 100, pct),
		sliderRow('Contrast', 'How far apart the light and dark parts are.', 'contrast', 0, 200, 100, 100, pct),
		sliderRow('Lightness', 'Toward black or toward white.', 'lightness', -100, 100, 0, 100, signed('%')),
		tintRow);
	resetAll.disabled = ro || !lookAny(lookNow());
	redraw();
}

// ------------------------------------------------------------------ drag and drop

let dragDepth = 0;
function dropTarget(el, fn) {
	el.addEventListener('dragover', (e) => {
		e.preventDefault();
		e.stopPropagation();
		el.classList.add('drop');
	});
	el.addEventListener('dragleave', () => el.classList.remove('drop'));
	el.addEventListener('drop', (e) => {
		e.preventDefault();
		e.stopPropagation();
		el.classList.remove('drop');
		dragDepth = 0;
		$('#dropHint').hidden = true;
		const files = [...(e.dataTransfer?.files || [])];
		if (files.length) fn(files);
	});
}
function slotFromName(name) {
	const n = name.toLowerCase().replace(/\.[a-z0-9]+$/, '');
	const words = n.split(/[^a-z0-9]+/).filter(Boolean);
	for (const [k, ws] of Object.entries(SLOT_WORDS)) if (ws.some((w) => words.includes(w) || n === w)) return k;
	return null;
}
async function dropPage(files) {
	const zips = files.filter((f) => /\.(zip|obspet)$/i.test(f.name));
	if (zips.length) return importZip(zips[0]);
	if (S.tab === 'sounds') return toast('Drop sounds on the row they belong to.');
	if (!S.editable) return toast('Make your own copy first (the button at the top).', true);
	// files named after their slots go there, one after another; anything else to the selected slot
	const named = files.map((f) => [slotFromName(f.name), f]);
	if (files.length > 1 && named.every(([k]) => k) && new Set(named.map(([k]) => k)).size === files.length) {
		for (const [k, f] of named) await importInto(k, [f]);
		return;
	}
	const k = files.length === 1 && named[0][0] ? named[0][0] : S.sel;
	if (k !== S.sel) select(k);
	await importInto(k, files);
}

// ------------------------------------------------------------------ start

function setTab(t) {
	S.tab = t;
	document.querySelectorAll('#edNav button').forEach((b) => b.classList.toggle('on', b.dataset.tab === t));
	for (const id of ['anims', 'sounds', 'details', 'colors']) $('#tab-' + id).hidden = id !== t;
	if (t === 'details') renderDetails();
}

async function tryIt() {
	await save();
	try {
		const r = await (await post('try', { id: S.id, what: $('#trySel').value })).json();
		if (!r.pets) toast(S.pets ? 'None of your Pets shows this character. Click “Show on my Pet” first.' : 'Add a Pet source in OBS first.', true);
	} catch (e) {
		toast("Couldn't reach OBS: " + e.message, true);
	}
}

async function useIt() {
	await save();
	try {
		await post('use', { id: S.id });
		setTimeout(refreshPacks, 500);
		toast(`${S.pack.name} is on your Pet now.`);
	} catch (e) {
		toast("Couldn't reach OBS: " + e.message, true);
	}
}

function wire() {
	$('#pack').addEventListener('change', (e) => openPack(e.target.value));
	$('#new').addEventListener('click', newPack);
	$('#gallery').addEventListener('click', openGallery);
	$('#hear').addEventListener('click', hearOpen);
	$('#use').addEventListener('click', useIt);
	$('#more').addEventListener('click', (e) => {
		e.stopPropagation();
		$('#menu').hidden = !$('#menu').hidden;
		$('#menu [data-act=remove]').disabled = !S.packs.find((p) => p.id === S.id)?.user;
		$('#menu [data-act=export]').disabled = !has('idle');
	});
	document.addEventListener('click', () => ($('#menu').hidden = true));
	$('#menu').addEventListener('click', (e) => {
		const act = e.target.dataset.act;
		$('#menu').hidden = true;
		if (act === 'copy') makeCopy();
		else if (act === 'export') exportPack();
		else if (act === 'import') pick((f) => importZip(f[0]), '.zip,.obspet');
		else if (act === 'remove') removePack();
	});
	document.querySelectorAll('#edNav button').forEach((b) => b.addEventListener('click', () => setTab(b.dataset.tab)));
	$('#turn').addEventListener('click', (e) => {
		S.turn = !S.turn;
		e.currentTarget.classList.toggle('on', S.turn);
	});
	$('#guides').addEventListener('click', (e) => {
		S.guides = !S.guides;
		e.currentTarget.classList.toggle('on', S.guides);
	});
	$('#bg').addEventListener('click', () => (S.bg = (S.bg + 1) % BGS.length));
	$('#zoomIn').addEventListener('click', () => (S.zoom = Math.min(12, S.zoom + 1)));
	$('#zoomOut').addEventListener('click', () => (S.zoom = Math.max(1, S.zoom - 1)));
	$('#trySel').replaceChildren(...TESTS.map(([g, items]) => h('optgroup', { label: g }, ...items.map(([v, t]) => h('option', { value: v, text: t })))));
	$('#tryGo').addEventListener('click', tryIt);
	document.addEventListener('keydown', (e) => {
		if (e.key !== 'Escape' || $('#edModal').hidden) return;
		e.stopImmediatePropagation();
		const btns = [...document.querySelectorAll('#edDialog .actions button')];
		(btns.find((b) => /^(Cancel|Close)$/.test(b.textContent)) || btns[0])?.click();
	});
	// the page as a drop target: files named after a slot go there
	window.addEventListener('dragenter', (e) => {
		if (!active || ![...(e.dataTransfer?.types || [])].includes('Files')) return;
		dragDepth++;
		if ($('#edModal').hidden) $('#dropHint').hidden = false;
	});
	window.addEventListener('dragleave', () => {
		if (--dragDepth <= 0) {
			dragDepth = 0;
			$('#dropHint').hidden = true;
		}
	});
	window.addEventListener('dragover', (e) => active && e.preventDefault());
	window.addEventListener('drop', (e) => {
		if (!active) return;
		e.preventDefault();
		dragDepth = 0;
		$('#dropHint').hidden = true;
		const files = [...(e.dataTransfer?.files || [])];
		if (files.length && $('#edModal').hidden) dropPage(files);
	});
	window.addEventListener('focus', () => active && booted && refreshPacks().catch(() => {}));
	window.addEventListener('beforeunload', (e) => {
		if (S.dirtyMeta || S.dirty.size || saving) {
			save();
			e.preventDefault();
		}
	});
}

// ------------------------------------------------------------------ the view

// The settings page shows this view with show() and leaves it with hide(). Nothing loads until the
// view is first shown; while it's hidden the stage stops drawing and the page's drops, focus and
// hash are the settings page's again.
let active = false, booted = false;
function setHash(id) {
	if (active) history.replaceState(null, '', '#character:' + encodeURIComponent(id));
}

async function boot(want) {
	try {
		await refreshPacks();
		S.defaults = await apiJson('defaults').catch(() => ({}));
	} catch (e) {
		booted = false;
		toast("Couldn't reach OBS. Is it still open? (" + e.message + ')', true);
		return;
	}
	const first = S.packs.find((p) => p.id === want) || S.packs.find((p) => p.on_stream) || S.packs.find((p) => p.user) || S.packs[0];
	if (first) await openPack(first.id);
	else await newPack();
}

window.OBSPET_EDITOR = {
	// shows character `id` (or the one open last, or the one on stream)
	async show(id = '') {
		active = true;
		if (!booted) {
			booted = true;
			return boot(id);
		}
		if (id && id !== S.id && S.packs.some((p) => p.id === id)) {
			await refreshPacks().catch(() => {});
			return openPack(id);
		}
		if (S.id) setHash(S.id);
		refreshPacks().catch(() => {});
	},
	// saves what's waiting, and goes quiet
	async hide() {
		active = false;
		if (!$('#edModal').hidden) {   // (left with the Back button: the open dialog goes as if cancelled)
			const btns = [...document.querySelectorAll('#edDialog .actions button')];
			const cancel = btns.find((b) => /^(Cancel|Close)$/.test(b.textContent));
			if (cancel) cancel.click();
			else closeModal();
		}
		$('#menu').hidden = true;
		$('#dropHint').hidden = true;
		dragDepth = 0;
		stopHearing();
		stopSound();
		clearTimeout(saveTimer);
		await save();
	},
	// a new character, from its name (the settings page's character picker offers it)
	new: () => newPack(),
	// the Update card (/update/update.js, through settings.js): save before OBS closes for an update,
	// and don't reload into the new version over unsaved work
	async beforeUpdate() {
		clearTimeout(saveTimer);
		await save();
		if (S.dirty.size || S.dirtyMeta || S.dirtySheet) throw new Error('the character has unsaved changes');
	},
	canReload: () => !(S.dirty.size || S.dirtyMeta || S.dirtySheet || saving),
};

wire();
requestAnimationFrame(loop);
window.dispatchEvent(new Event('obspet-editor-ready'));
