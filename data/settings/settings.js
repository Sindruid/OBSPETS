// OBSPETS Settings page. Served by the plugin on 127.0.0.1 (src/settings.cpp & chat-server.cpp);
// every API request carries the per-run key from this page's URL (?k=...).
'use strict';

const KEY = new URLSearchParams(location.search).get('k') || '';
// ?s=automod (sindruid.com's "Auto mod settings" button): open a pet on that page
const OPEN_SECTION = new URLSearchParams(location.search).get('s') || '';
const NO_SOURCE = '(none)';
const MAX_PETS = 6;

const REACTS = [
	{ id: 'follow', label: 'Follows', group: 'Alerts' },
	{ id: 'sub', label: 'Subs', group: 'Alerts' },
	{ id: 'gift', label: 'Gifted subs', group: 'Alerts' },
	{ id: 'bits', label: 'Bits & Kicks', group: 'Alerts' },
	{ id: 'raid', label: 'Raids', group: 'Alerts' },
	{ id: 'pet', label: 'Channel points', group: 'Alerts' },
	{ id: 'happy', label: 'Happy hop', group: 'Chat' },
	{ id: 'love', label: 'Hearts', group: 'Chat' },
	{ id: 'greet', label: 'Says hi', group: 'Chat' },
	{ id: 'curious', label: 'Curious', group: 'Chat' },
	{ id: 'show_off', label: 'Shows off', group: 'Chat' },
	{ id: 'play', label: 'Plays', group: 'Chat' },
	{ id: 'zoomies', label: 'Zoomies', group: 'Chat' },
	{ id: 'vibe', label: 'Bobs to music', group: 'Chat' },
	{ id: 'beg', label: 'Begs', group: 'Chat' },
	{ id: 'sleepy', label: 'Naps', group: 'Chat' },
	{ id: 'hop_on', label: 'Hops onto chat', group: 'Chat' },
	{ id: 'hop_off', label: 'Hops off chat', group: 'Chat' },
	{ id: 'swat_chat', label: 'Swats at chat', group: 'Chat' },
	{ id: 'judge', label: 'Unimpressed', group: 'Chat' },
	{ id: 'sulk', label: 'Sulks', group: 'Chat' },
	{ id: 'startled', label: 'Startled', group: 'Chat' },
	{ id: 'scared', label: 'Scared', group: 'Chat' },
	{ id: 'angry', label: 'Angry', group: 'Chat' },
	{ id: 'yank', label: 'Rips a message out', group: 'Chat' },
];

const QUICK_TESTS = [
	['follow', 'Follow'],
	['sub', 'Sub'],
	['gift:5', '5 Gifts'],
	['bits:100', '100 Bits'],
	['raid:8', 'Raid'],
	['pet', 'Pet'],
	['mood:yank', 'Yank chat'],
	['mood:automod', 'Auto mod'],
];

const MORE_TESTS = [
	['Big alerts', [
		['gift:20', '20 Gifts'],
		['bits:1000', '1,000 Bits'],
		['raid:60', 'Big raid (60)'],
	]],
	['Chat moods', [
		['mood:happy', 'Happy hop'],
		['mood:love', 'Hearts'],
		['mood:greet', 'Says hi'],
		['mood:curious', 'Curious'],
		['mood:show_off', 'Shows off'],
		['mood:play', 'Plays'],
		['mood:zoomies', 'Zoomies'],
		['mood:vibe', 'Bobs to music'],
		['mood:beg', 'Begs'],
		['mood:sleepy', 'Naps'],
		['mood:hop_on', 'Hops on chat'],
		['mood:hop_off', 'Hops off chat'],
		['mood:swat_chat', 'Swats chat'],
		['mood:judge', 'Unimpressed'],
		['mood:sulk', 'Sulks'],
		['mood:startled', 'Startled'],
		['mood:scared', 'Scared'],
		['mood:angry', 'Angry'],
	]],
];

const PET_SECTIONS = [
	{ id: 'pets', label: 'Pets' },
	{ id: 'scene', label: 'Chat & Scene' },
	{ id: 'reactions', label: 'Reactions & AI' },
	{ id: 'automod', label: 'Auto mod' },
	{ id: 'try', label: 'Test Chat' },
	{ id: 'habits', label: 'Sound & Habits' },
	{ id: 'label', label: 'Name Label' },
	{ id: 'custom', label: 'Character Folders' },
	{ id: 'account', label: 'Account' },
];

const CHAT_SECTIONS = [
	{ id: 'channels', label: 'Channels & Layout' },
	{ id: 'text', label: 'Text & Names' },
	{ id: 'background', label: 'Background' },
	{ id: 'emotes', label: 'Emotes & Highlights' },
	{ id: 'filters', label: 'Filters & CSS' },
	{ id: 'account', label: 'Account' },
];

const CSS_CLASSES = [
	'#list', '.msg', '.head', '.name', '.colon', '.text', '.badge', '.pronouns',
	'.emote', '.big', '.mention', '.highlight', '.role', '.first', '.reply-to', '.tag', '.action',
];

const CHAT_SAMPLES = [
	{
		user: 'inkwell', color: '#4ea1ff', pronouns: 'he/him',
		badges: [{ t: 'M', bg: '#00ad03' }],
		role: true,
		parts: [{ t: 'text', v: 'welcome in everyone! grab a cozy seat ' }, { t: 'emote', v: 'HI' }],
	},
	{
		user: 'milo_draws', color: '#ff8a4c', pronouns: 'they/them',
		first: true,
		parts: [{ t: 'text', v: 'first time catching you live, love the little pet!!' }],
	},
	{
		user: 'nova_k', color: '#c87cff', pronouns: 'she/her',
		badges: [{ t: 'S', bg: '#9146ff' }],
		reply: 'Replying to @milo_draws: first time catching you live...',
		mention: true,
		parts: [{ t: 'text', v: '@channel look how clean this chat looks ' }, { t: 'emote', v: 'GG' }],
	},
	{
		user: 'pixel_fox', color: '#37d67a',
		highlight: true,
		parts: [{ t: 'text', v: 'hydrated and ready for the boss fight' }],
	},
	{
		user: 'tea_time', color: '#e6c24a',
		big: true,
		parts: [{ t: 'emote', v: 'VIBE' }],
	},
];

// ------------------------------------------------------------------ DOM & helpers

const $ = (s, el = document) => el.querySelector(s);

function h(tag, props, ...kids) {
	const el = document.createElement(tag);
	for (const [k, v] of Object.entries(props || {})) {
		if (v == null || v === false) continue;
		if (k === 'class') el.className = v;
		else if (k === 'text') el.textContent = v;
		else if (k === 'html') el.innerHTML = v;
		else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
		else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
		else if (k in el && typeof v !== 'string') el[k] = v;
		else el.setAttribute(k, v === true ? '' : v);
	}
	for (const c of kids.flat()) {
		if (c != null && c !== false) el.append(c instanceof Node ? c : document.createTextNode(String(c)));
	}
	return el;
}

const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

// "45", "45 s", "2 min", "1 min 30 s", "1:30", "never" -> seconds (NaN if it doesn't read as a time).
function parseSeconds(t) {
	t = t.toLowerCase().trim();
	if (t === 'never' || t === 'off') return 0;
	let m = t.match(/^(\d+):(\d{1,2})$/);
	if (m) return Number(m[1]) * 60 + Number(m[2]);
	m = t.match(/^(?:(\d+(?:\.\d+)?)\s*m(?:in(?:utes?|s)?)?)?\s*(?:(\d+(?:\.\d+)?)\s*(?:s(?:ec(?:onds?|s)?)?)?)?$/);
	if (!m || (m[1] == null && m[2] == null)) return NaN;
	return Number(m[1] || 0) * 60 + Number(m[2] || 0);
}
const slotKey = (base, slot) => (slot ? `${base}${slot + 1}` : base);
const doKey = (id, slot) => slotKey(`do_${id}`, slot);

function setRangePct(input) {
	const min = Number(input.min || 0), max = Number(input.max || 100), val = Number(input.value || 0);
	const pct = max > min ? clamp(((val - min) / (max - min)) * 100, 0, 100) : 0;
	input.style.setProperty('--pct', pct.toFixed(1) + '%');
}

// OBS color format is 32-bit 0xAABBGGRR
function obsColorToParts(v) {
	const u = (Number(v) >>> 0);
	return {
		r: u & 0xff,
		g: (u >>> 8) & 0xff,
		b: (u >>> 16) & 0xff,
		a: (u >>> 24) & 0xff,
	};
}

function partsToHex(r, g, b) {
	return '#' + [r, g, b].map((x) => clamp(Math.round(x), 0, 255).toString(16).padStart(2, '0')).join('');
}

function obsColorToHex(v) {
	const { r, g, b } = obsColorToParts(v);
	return partsToHex(r, g, b);
}

function obsColorToRgba(v, useAlpha = true) {
	const { r, g, b, a } = obsColorToParts(v);
	return `rgba(${r},${g},${b},${useAlpha ? (a / 255).toFixed(3) : '1'})`;
}

function hexAlphaToObs(hex, alphaByte = 255) {
	const m = /^#?([0-9a-f]{6})$/i.exec((hex || '').trim());
	if (!m) return null;
	const n = parseInt(m[1], 16);
	const r = (n >>> 16) & 0xff, g = (n >>> 8) & 0xff, b = n & 0xff;
	const a = clamp(Math.round(alphaByte), 0, 255);
	return (((a << 24) | (b << 16) | (g << 8) | r) >>> 0);
}

let toastTimer = null;
function toast(msg, kind = '') {
	const el = $('#toast');
	el.textContent = msg;
	el.className = 'toast' + (kind ? ' ' + kind : '');
	el.hidden = false;
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => { el.hidden = true; }, kind === 'err' ? 6000 : 3000);
}

function setSaved(text, cls = '') {
	const el = $('#saved');
	el.textContent = text;
	el.className = 'saved' + (cls ? ' ' + cls : '');
}

// ------------------------------------------------------------------ API

async function api(what, params = {}, init = {}) {
	const q = new URLSearchParams({ ...params, k: KEY });
	const r = await fetch(`/settings/api/${what}?${q}`, init);
	if (!r.ok) {
		let why = r.statusText;
		try { why = (await r.json()).error || why; } catch { /* not json */ }
		throw new Error(why);
	}
	return r.json();
}

async function doAction(payload) {
	return api('action', {}, {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify(payload),
	});
}

// ------------------------------------------------------------------ sprite & look rendering

const HEX6 = /^#[0-9a-f]{6}$/i;
const packCache = new Map(); // id -> { loading, promise, info, canvas }

function applyLookToCanvas(img, info) {
	const c = document.createElement('canvas');
	c.width = img.naturalWidth || img.width || 1;
	c.height = img.naturalHeight || img.height || 1;
	const ctx = c.getContext('2d', { willReadFrequently: true });
	ctx.imageSmoothingEnabled = false;
	ctx.drawImage(img, 0, 0);

	const hue = clamp(Math.round(Number(info.hue) || 0), -180, 180);
	const sat = Number.isFinite(+info.saturation) ? clamp(+info.saturation, 0, 2) : 1;
	const con = Number.isFinite(+info.contrast) ? clamp(+info.contrast, 0, 2) : 1;
	const lig = Number.isFinite(+info.lightness) ? clamp(+info.lightness, -1, 1) : 0;
	const tintHex = HEX6.test(info.tint || '') ? info.tint.toLowerCase() : '#ffffff';
	const tintAmt = Number.isFinite(+info.tint_amount) ? clamp(+info.tint_amount, 0, 1) : 0;
	const swap = new Map();
	for (const [a, b] of Object.entries(info.recolor || {})) {
		if (HEX6.test(a) && HEX6.test(b) && a.toLowerCase() !== b.toLowerCase()) {
			swap.set(parseInt(a.slice(1), 16), parseInt(b.slice(1), 16));
		}
	}
	if (!hue && sat === 1 && con === 1 && !lig && tintAmt <= 0 && !swap.size) return c;

	const idata = ctx.getImageData(0, 0, c.width, c.height);
	const d = idata.data;
	const rad = (hue * Math.PI) / 180, ca = Math.cos(rad), sb = Math.sin(rad);
	const m = [
		[0.213 + 0.787 * ca - 0.213 * sb, 0.715 - 0.715 * ca - 0.715 * sb, 0.072 - 0.072 * ca + 0.928 * sb],
		[0.213 - 0.213 * ca + 0.143 * sb, 0.715 + 0.285 * ca + 0.140 * sb, 0.072 - 0.072 * ca - 0.283 * sb],
		[0.213 - 0.213 * ca - 0.787 * sb, 0.715 - 0.715 * ca + 0.715 * sb, 0.072 + 0.928 * ca + 0.072 * sb],
	];
	const tint = [1, 3, 5].map((i) => parseInt(tintHex.slice(i, i + 2), 16) / 255);
	const col = [0, 0, 0], out = [0, 0, 0];
	for (let i = 0; i < d.length; i += 4) {
		if (!d[i + 3]) continue;
		let key = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
		if (swap.size && swap.has(key)) key = swap.get(key);
		col[0] = (key >> 16) / 255;
		col[1] = ((key >> 8) & 255) / 255;
		col[2] = (key & 255) / 255;
		if (hue) {
			for (let k = 0; k < 3; k++) out[k] = m[k][0] * col[0] + m[k][1] * col[1] + m[k][2] * col[2];
			col[0] = out[0]; col[1] = out[1]; col[2] = out[2];
		}
		if (sat !== 1) {
			const y = 0.213 * col[0] + 0.715 * col[1] + 0.072 * col[2];
			for (let k = 0; k < 3; k++) col[k] = y + (col[k] - y) * sat;
		}
		for (let k = 0; k < 3; k++) {
			let v = clamp(col[k], 0, 1);
			v = clamp((v - 0.5) * con + 0.5, 0, 1);
			v = lig >= 0 ? v + (1 - v) * lig : v * (1 + lig);
			v = v * (1 - tintAmt) + v * tint[k] * tintAmt;
			col[k] = v;
		}
		d[i] = clamp(Math.round(col[0] * 255), 0, 255);
		d[i + 1] = clamp(Math.round(col[1] * 255), 0, 255);
		d[i + 2] = clamp(Math.round(col[2] * 255), 0, 255);
	}
	ctx.putImageData(idata, 0, 0);
	return c;
}

function loadPackAsset(id) {
	if (!id) return null;
	const existing = packCache.get(id);
	if (existing) return existing;
	const entry = { ready: false, failed: false, info: null, canvas: null };
	packCache.set(id, entry);
	const q = new URLSearchParams({ id, k: KEY, v: S.packGen || 0 });   // (v: new after a character was edited)
	fetch(`/editor/api/pack?${q}`)
		.then((r) => (r.ok ? r.json() : Promise.reject(new Error('pack'))))
		.then((info) => {
			if (info.error || !info.anims?.idle) throw new Error(info.error || 'bad pack');
			entry.info = info;
			return new Promise((resolve, reject) => {
				const img = new Image();
				img.onload = () => resolve(img);
				img.onerror = reject;
				img.src = `/editor/api/sheet?${q}`;
			});
		})
		.then((img) => {
			entry.canvas = applyLookToCanvas(img, entry.info);
			entry.ready = true;
		})
		.catch(() => {
			entry.failed = true;
		});
	return entry;
}

function drawPackSprite(ctx, id, cx, bottomY, maxW, maxH, t, animName = 'idle') {
	const entry = loadPackAsset(id);
	if (!entry || !entry.ready || !entry.info || !entry.canvas) return false;
	const info = entry.info;
	const anim = info.anims[animName] || info.anims.idle;
	if (!anim || !anim.cells?.length) return false;
	const fps = Math.max(1, Number(anim.fps) || 6);
	const fi = Math.floor(t * fps) % anim.cells.length;
	const [col, row] = anim.cells[fi];
	const [cw, ch] = info.cell || [32, 32];
	const scale = Math.max(1, Math.min(Math.floor(maxW / cw), Math.floor(maxH / ch)));
	const dw = cw * scale, dh = ch * scale;
	const feet = Number(info.feet) || 0;
	const dx = Math.round(cx - dw / 2);
	const dy = Math.round(bottomY - dh + feet * scale);
	ctx.save();
	ctx.imageSmoothingEnabled = false;
	if (info.faces === 'left') {
		ctx.translate(cx, 0);
		ctx.scale(-1, 1);
		ctx.drawImage(entry.canvas, col * cw, row * ch, cw, ch, -dw / 2, dy, dw, dh);
	} else {
		ctx.drawImage(entry.canvas, col * cw, row * ch, cw, ch, dx, dy, dw, dh);
	}
	ctx.restore();
	return true;
}

// Pet `slot`'s character and name, from the settings (with edits not saved yet). Not from the
// plugin's meta: that lags a moment behind a change, which left the stage, its name and its Edit
// link on the old character.
function slotPackId(src, slot) {
	const cfg = src.settings;
	if (slot === 0 && (cfg.pack_dir || '').trim() && src.meta?.pack_id) return src.meta.pack_id;   // (a folder of its own)
	const packs = S.data?.packs || [];
	const id = cfg[slotKey('pack', slot)];
	if (id && packs.some((p) => p.id === id)) return id;
	return (packs.find((p) => p.id === 'blob') || packs[0])?.id || '';   // (the plugin's default)
}
// {reaction: "Angry"}: the reactions this pet's character can't do, and the animation each needs
function slotCant(src, slot) {
	const cfg = src.settings;
	if (slot === 0 && (cfg.pack_dir || '').trim()) return src.meta?.pack_cant || {};
	return S.data?.packs?.find((p) => p.id === slotPackId(src, slot))?.cant || {};
}
// a reaction no pet on this source can do (its tests would do nothing)
function noPetCan(src, kind) {
	const n = clamp(Number(src.settings.pets) || 1, 1, MAX_PETS);
	for (let i = 0; i < n; i++) if (!slotCant(src, i)[kind]) return false;
	return true;
}
const needsText = (need) => `Needs ${/^[AEIOU]/.test(need) ? 'an' : 'a'} ${need} animation`;
function slotPackName(src, slot) {
	const id = slotPackId(src, slot);
	return S.data?.packs?.find((p) => p.id === id)?.name || src.meta?.pack_name || id || 'Pet';
}
function slotPetName(src, slot) {
	return (src.settings[slotKey('name', slot)] || '').trim() || slotPackName(src, slot);
}
// the Characters view (the character editor, /editor/editor.js) on character `packId`: a link within this page
const editorUrl = (packId) => (packId ? `#character:${encodeURIComponent(packId)}` : '#characters');

// ------------------------------------------------------------------ state

const S = {
	view: 'sources',     // 'sources' (this file) or 'characters' (the character editor, /editor/editor.js)
	data: null,
	fonts: [],
	selectedName: null,
	section: null,
	openReacts: new Set(),
	testMoreOpen: false,
	tryLog: new Map(),   // pet source name -> chat tester results, newest first
	tryDraft: new Map(), // pet source name -> { user, text } not sent yet
	tryBusy: false,
	stageAnimIdx: 0,
	chatBg: 'stream',
	pendingPatches: new Map(), // sourceName -> { patch, timer }
	savingCount: 0,
	pollTimer: null,
	savedTimer: null,
	// live canvases for rAF loop
	stageCanvas: null,
	thumbCanvases: [], // [{ canvas, packId }]
	galleryCanvases: [], // [{ canvas, packId }]
};

const STAGE_ANIMS = ['idle', 'walk', 'look', 'run', 'clean', 'sleep', 'paw'];

function currentSource() {
	if (!S.data?.sources?.length) return null;
	return S.data.sources.find((s) => s.name === S.selectedName) || S.data.sources[0];
}

function defaultSectionFor(src) {
	return src?.kind === 'chat' ? 'channels' : 'pets';
}

function validSectionsFor(src) {
	return src?.kind === 'chat' ? CHAT_SECTIONS : PET_SECTIONS;
}

function parseHash() {
	const raw = location.hash.slice(1);
	if (!raw) return null;
	const colon = raw.indexOf(':');
	if (colon > 0) {
		return { kind: raw.slice(0, colon), name: decodeURIComponent(raw.slice(colon + 1)) };
	}
	return { kind: '', name: decodeURIComponent(raw) };
}

function updateHash(src) {
	if (!src || S.view !== 'sources') return;
	const want = `#${src.kind}:${encodeURIComponent(src.name)}`;
	if (location.hash !== want) history.replaceState(null, '', want);
}

// ------------------------------------------------------------------ the two views

// The page has two views: Sources (each OBSPETS and OBSPETS Chat source's settings, this file) and
// Characters (the character editor, /editor/editor.js, a module that sets window.OBSPET_EDITOR).
// The hash says which: #pet:<name> / #chat:<name> for Sources, #characters or #character:<id>
// for Characters. Switching sets the hash, so the browser's Back button goes back to the other view.
const editorReady = new Promise((res) => {
	if (window.OBSPET_EDITOR) res();
	else window.addEventListener('obspet-editor-ready', res, { once: true });
});

function hashView() {
	const t = parseHash();
	if (t?.kind === 'character') return { view: 'characters', id: t.name };
	if (t && !t.kind && t.name === 'characters') return { view: 'characters', id: '' };
	return { view: 'sources', target: t };
}

function sourcesHash() {
	const src = currentSource();
	return src ? `#${src.kind}:${encodeURIComponent(src.name)}` : '#sources';
}

// the view switch in the header
function goView(view) {
	if (view === S.view) return;
	location.hash = view === 'characters' ? '#characters' : sourcesHash();   // (Characters opens on the character it showed last)
}

// a new character: Characters opens and asks for its name
async function newCharacter() {
	if (S.view !== 'characters') location.hash = '#characters';
	await editorReady;
	await showView('characters');
	window.OBSPET_EDITOR.new();
}

function route() {
	const want = hashView();
	if (want.view === 'characters') return showView('characters', want.id);
	showView('sources');
	const t = want.target;
	const hit = t && S.data?.sources.find((s) => s.name === t.name && (!t.kind || s.kind === t.kind));
	if (hit) selectSource(hit.name);
}

let viewBusy = Promise.resolve();
function showView(view, packId = '') {
	// (one switch at a time: leaving Characters saves first)
	return (viewBusy = viewBusy.then(() => applyView(view, packId)).catch(() => {}));
}

async function applyView(view, packId) {
	const chars = view === 'characters';
	const was = S.view;
	S.view = view;
	for (const b of document.querySelectorAll('#viewSwitch button')) b.classList.toggle('on', b.dataset.view === view);
	for (const sel of ['#sourceBar', '#saved', '#banners']) $(sel).hidden = chars;
	for (const sel of ['#editorView', '#edPicker', '#edActions', '#edSaved']) $(sel).hidden = !chars;
	document.title = chars ? 'Characters · OBSPETS' : 'OBSPETS Settings';
	if (chars) {
		$('#app').hidden = true;
		$('#emptyState').hidden = true;
		if (was !== view) window.scrollTo(0, 0);
		await editorReady;
		await window.OBSPET_EDITOR.show(packId);
		return;
	}
	if (was === view) return;
	window.scrollTo(0, 0);
	if (S.data) {
		renderTopBar();
		renderBanners();
		renderApp();
	}
	if (window.OBSPET_EDITOR) {
		// what changed in Characters shows here once it's saved: a character's look, its name, a new one
		await window.OBSPET_EDITOR.hide();
		packCache.clear();
		S.packGen = (S.packGen || 0) + 1;
		if (S.data) refreshState().catch(() => {});
	}
}

// ------------------------------------------------------------------ state fetch & patching

async function refreshState({ check = false, preserveFocus = false } = {}) {
	const data = await api('state', check ? { check: '1' } : {});
	// Preserve any in-flight unsent local edits so a background poll never snaps back a field
	for (const [name, entry] of S.pendingPatches.entries()) {
		const src = data.sources.find((s) => s.name === name);
		if (src) Object.assign(src.settings, entry.patch);
	}
	const prevAcct = S.data?.account;
	S.data = data;

	if (!S.selectedName || !data.sources.some((s) => s.name === S.selectedName)) {
		const hTarget = parseHash();
		const matched = hTarget && data.sources.find((s) => s.name === hTarget.name && (!hTarget.kind || s.kind === hTarget.kind));
		const wantPet = !matched && PET_SECTIONS.some((x) => x.id === OPEN_SECTION) && data.sources.find((s) => s.kind === 'pet');
		const first = matched || wantPet || data.sources[0] || null;
		S.selectedName = first ? first.name : null;
		S.section = wantPet ? OPEN_SECTION : first ? defaultSectionFor(first) : null;
	} else {
		const src = currentSource();
		const secs = validSectionsFor(src);
		if (!secs.some((x) => x.id === S.section)) S.section = defaultSectionFor(src);
	}

	syncPolling();

	// If preserveFocus is set and an input inside #content is currently focused, only update non-destructive chrome
	const activeEl = document.activeElement;
	const editingText = preserveFocus && activeEl && $('#content')?.contains(activeEl) &&
		(activeEl.tagName === 'INPUT' || activeEl.tagName === 'TEXTAREA' || activeEl.tagName === 'SELECT');

	renderTopBar();
	renderBanners();
	if (!editingText) {
		renderApp();
	} else if (prevAcct && JSON.stringify(prevAcct) !== JSON.stringify(data.account) && S.section === 'account') {
		renderApp();
	}
}

function syncPolling() {
	const needFast = !!(S.data?.account?.linking || S.data?.account?.reward === 'checking' || S.data?.account?.kick_reward === 'checking');
	if (needFast && !S.pollTimer) {
		S.pollTimer = setInterval(() => {
			refreshState({ preserveFocus: true }).catch(() => {});
		}, 1800);
	} else if (!needFast && S.pollTimer) {
		clearInterval(S.pollTimer);
		S.pollTimer = null;
	}
}

function patchSource(srcName, patch, { immediate = false, refreshAfter = false } = {}) {
	const src = S.data?.sources?.find((s) => s.name === srcName);
	if (src) Object.assign(src.settings, patch);

	// Live preview updates without full rerender
	if (src?.kind === 'chat') updateChatPreview();
	if (src?.kind === 'pet') updateLabelPreview();

	let entry = S.pendingPatches.get(srcName);
	if (!entry) {
		entry = { patch: {}, timer: null, refreshAfter: false };
		S.pendingPatches.set(srcName, entry);
	}
	Object.assign(entry.patch, patch);
	entry.refreshAfter = entry.refreshAfter || refreshAfter;
	if (entry.timer) clearTimeout(entry.timer);

	setSaved('Saving…', 'busy');
	const flush = () => flushPatch(srcName);
	if (immediate) flush();
	else entry.timer = setTimeout(flush, 320);
}

async function flushPatch(srcName) {
	const entry = S.pendingPatches.get(srcName);
	if (!entry) return;
	if (entry.timer) clearTimeout(entry.timer);
	S.pendingPatches.delete(srcName);
	const { patch, refreshAfter } = entry;

	S.savingCount++;
	try {
		const res = await api('update', { name: srcName }, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(patch),
		});
		if (res.renamed) {
			const src = S.data?.sources?.find((s) => s.name === srcName);
			if (src) src.settings.name = res.renamed;
		}
		S.savingCount = Math.max(0, S.savingCount - 1);
		if (S.savingCount === 0 && S.pendingPatches.size === 0) {
			setSaved('Saved', 'ok');
			clearTimeout(S.savedTimer);
			S.savedTimer = setTimeout(() => setSaved(''), 2200);
		}
		if (refreshAfter || res.renamed) {
			await refreshState({ preserveFocus: true });
		}
	} catch (err) {
		S.savingCount = Math.max(0, S.savingCount - 1);
		setSaved('Save failed', 'err');
		toast("Couldn't save: " + err.message, 'err');
	}
}

async function runAction(payload, { okToast = '', refresh = true } = {}) {
	try {
		const res = await doAction(payload);
		if (res.error) throw new Error(res.error);
		if (okToast) toast(okToast, 'ok');
		if (refresh) await refreshState();
		return res;
	} catch (err) {
		toast(err.message, 'err');
		return null;
	}
}

// ------------------------------------------------------------------ reusable controls

function makeRow(label, hint, ctrl, extraClass = '') {
	return h('div', { class: 'row-item' + (extraClass ? ' ' + extraClass : '') },
		h('div', {},
			h('div', { class: 'row-label', text: label }),
			hint ? h('div', { class: 'row-hint', text: hint }) : null,
		),
		h('div', { class: 'row-ctrl' }, ctrl),
	);
}

function makeToggleRow(label, hint, checked, onChange) {
	const input = h('input', {
		type: 'checkbox',
		checked: !!checked,
		onchange: () => onChange(input.checked),
	});
	const sw = h('label', { class: 'toggle' }, input, h('span', { class: 'toggle-track' }));
	return makeRow(label, hint, sw, 'toggle-row');
}

// `hint` can be a function of the value: it then updates as the slider moves.
// With `parse` (text -> number, or NaN), the value box is a text field: type a value and press
// Enter (or leave the box) to set it. Esc puts back the current value.
function makeSliderRow(label, hint, { value, min, max, step = 1, format, parse, onChange, immediate = false }) {
	const fmt = format || ((v) => String(v));
	const live = typeof hint === 'function';
	const setText = (t) => { if (parse) valEl.value = t; else valEl.textContent = t; };
	const show = (n) => {
		setText(fmt(n));
		if (live) row.querySelector('.row-hint').textContent = hint(n);
	};
	const commit = () => {
		let n = parse(valEl.value.trim());
		if (!Number.isFinite(n)) { show(Number(input.value)); return; }
		n = clamp(Math.round(n / step) * step, Number(min), Number(max));
		input.value = n;
		setRangePct(input);
		onChange(n, true);
		show(n);
	};
	const valEl = parse
		? h('input', {
			type: 'text', class: 'slider-val', value: fmt(value), spellcheck: false,
			onfocus: () => valEl.select(),
			onchange: commit,
			onkeydown: (e) => {
				if (e.key === 'Enter') valEl.blur();
				else if (e.key === 'Escape') { show(Number(input.value)); valEl.blur(); }
			},
		})
		: h('span', { class: 'slider-val', text: fmt(value) });
	const input = h('input', {
		type: 'range', min, max, step, value,
		oninput: () => {
			setRangePct(input);
			const n = Number(input.value);
			onChange(n, false);
			show(n);
		},
		onchange: () => {
			const n = Number(input.value);
			onChange(n, true);
			show(n);
		},
	});
	setRangePct(input);
	const row = makeRow(label, live ? hint(value) : hint, h('div', { class: 'slider-wrap' }, input, valEl));
	row.refresh = () => show(Number(input.value));
	return row;
}

// ------------------------------------------------------------------ how often the pet does things

// A model of decide() in src/pet.cpp: the same weights, with typical lengths for each action (a
// 1080p canvas at size 5), and chat at a normal pace. It turns the Naps, Energy and chat-visit
// sliders into times. Keep the weights in step with decide().
const ACT_SECS = { idle: 6.5, look: 3.75, clean: 4.2, sleep: 31.5, walk: 5.4, run: 4.2, paw: 3.2, hop: 3.4, leave: 6.5, visit: 8 };
const MOVES = new Set(['walk', 'run', 'paw', 'hop']);

function petRhythm({ naps = 1, energy = 1, visit = 0.5, chat = true } = {}) {
	const z = Number(naps), e = Number(energy), v = chat ? Number(visit) : 0;
	let seed = 7;
	const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
	const floor = [['idle', 26], ['look', 16], ['clean', 12], ['walk', 22 * e], ['run', 4 * e], ['sleep', 9 * z], ['paw', 6 * e], ['hop', 5 * e], ['visit', 40 * v]];
	const perch = [['idle', 30], ['look', 18], ['clean', 12], ['sleep', 14 * z], ['walk', 12 * e], ['paw', 5 * e], ['leave', 9]];
	const sum = (acts) => acts.reduce((t, a) => t + a[1], 0);
	const total = { floor: sum(floor), perch: sum(perch) };
	let up = false, last = '', t = 0, upT = 0;
	const n = { sleep: 0, move: 0, run: 0, visit: 0 };
	for (let i = 0; i < 20000; i++) {
		const acts = up ? perch : floor;
		let r = rnd() * (up ? total.perch : total.floor), pick = 'idle';
		for (const [a, w] of acts) if (w > 0 && (r -= w) <= 0) { pick = a; break; }
		if (pick === last && pick !== 'idle') pick = 'idle';   // (no trick twice in a row)
		last = pick;
		t += ACT_SECS[pick];
		if (up) upT += ACT_SECS[pick];
		if (pick === 'sleep') n.sleep++;
		if (MOVES.has(pick)) n.move++;
		if (pick === 'run') n.run++;
		if (pick === 'visit') { n.visit++; up = true; }
		if (pick === 'leave') up = false;
	}
	const every = (k) => (n[k] ? t / n[k] : Infinity);
	return { nap: every('sleep'), move: every('move'), zoomies: every('run'), visit: every('visit'), stay: n.visit ? upT / n.visit : 0 };
}

// 40 -> "40 s", 95 -> "1½ min", 400 -> "7 min"
function spanText(sec) {
	if (sec < 55) return `${Math.max(5, Math.round(sec / 5) * 5)} s`;
	const m = sec / 60;
	if (m >= 10) return `${Math.round(m)} min`;
	const half = Math.max(1, Math.round(m * 2) / 2);
	return `${Math.floor(half)}${half % 1 ? '½' : ''} min`;
}
const everyText = (sec) => (Number.isFinite(sec) ? `every ~${spanText(sec)}` : 'Never');

function makeSegRow(label, hint, options, value, onChange) {
	const wrap = h('div', { class: 'seg', role: 'radiogroup' });
	const btns = options.map(([v, text]) => {
		const b = h('button', {
			type: 'button',
			class: v === value ? 'on' : '',
			text,
			onclick: () => {
				btns.forEach((x) => x.classList.toggle('on', x === b));
				onChange(v);
			},
		});
		return b;
	});
	wrap.append(...btns);
	return makeRow(label, hint, wrap);
}

function makeColorRow(label, hint, obsVal, { withAlpha = false, onChange }) {
	let parts = obsColorToParts(obsVal);
	const picker = h('input', { type: 'color', value: partsToHex(parts.r, parts.g, parts.b) });
	const hexIn = h('input', { type: 'text', class: 'color-hex', value: picker.value.toUpperCase(), maxlength: 7 });
	const emit = (imm) => {
		const v = hexAlphaToObs(picker.value, withAlpha ? parts.a : 255);
		if (v != null) onChange(v, imm);
	};
	picker.addEventListener('input', () => {
		hexIn.value = picker.value.toUpperCase();
		emit(false);
	});
	picker.addEventListener('change', () => emit(true));
	hexIn.addEventListener('change', () => {
		let s = hexIn.value.trim();
		if (!s.startsWith('#')) s = '#' + s;
		if (HEX6.test(s)) {
			picker.value = s.toLowerCase();
			hexIn.value = s.toUpperCase();
			emit(true);
		} else {
			hexIn.value = picker.value.toUpperCase();
		}
	});
	const kids = [picker, hexIn];
	if (withAlpha) {
		const pctEl = h('span', { text: Math.round((parts.a / 255) * 100) + '%' });
		const slider = h('input', {
			type: 'range', min: 0, max: 255, step: 1, value: parts.a,
			oninput: () => {
				setRangePct(slider);
				parts.a = Number(slider.value);
				pctEl.textContent = Math.round((parts.a / 255) * 100) + '%';
				emit(false);
			},
			onchange: () => emit(true),
		});
		setRangePct(slider);
		kids.push(h('div', { class: 'alpha-wrap' }, slider, pctEl));
	}
	return makeRow(label, hint, h('div', { class: 'color-wrap' }, ...kids));
}

// A font picker: opens on the whole list (each name in its own face, the current one picked and
// scrolled to); typing filters it. Not a <datalist>: that only shows fonts matching what's typed,
// so a saved font left one option.
function makeFontPicker(face, onPick) {
	let cur = face, open = false, items = [], hi = -1;
	const input = h('input', {
		type: 'text', class: 'font-face', value: cur, spellcheck: false, autocomplete: 'off',
		role: 'combobox', 'aria-expanded': 'false', 'aria-label': 'Font',
		style: { fontFamily: `"${cur}", "Segoe UI", sans-serif` },
	});
	const list = h('div', { class: 'font-pop', role: 'listbox', hidden: true });
	const wrap = h('div', { class: 'font-pick' }, input, h('span', { class: 'font-caret', 'aria-hidden': 'true' }), list);

	const fill = (q) => {
		const all = S.fonts.length ? S.fonts : [cur];
		const ql = q.trim().toLowerCase();
		const shown = ql ? all.filter((f) => f.toLowerCase().includes(ql)) : all;
		items = shown.map((f) => h('div', {
			class: 'font-opt' + (f === cur ? ' on' : ''), role: 'option', text: f,
			style: { fontFamily: `"${f}", "Segoe UI", sans-serif` },
			onmousedown: (e) => { e.preventDefault(); pick(f); },
		}));
		list.replaceChildren(...(items.length ? items : [h('div', { class: 'font-none', text: 'No installed font matches' })]));
		hi = ql ? (items.length ? 0 : -1) : shown.indexOf(cur);
		mark(true);
	};
	const mark = (center) => {
		items.forEach((el, i) => el.classList.toggle('hi', i === hi));
		const el = items[hi];
		if (el) {
			if (center) list.scrollTop = el.offsetTop - list.clientHeight / 2 + el.offsetHeight / 2;
			else el.scrollIntoView({ block: 'nearest' });
		}
	};
	const show = () => {
		if (open) return;
		open = true;
		// (300 px: the list's max-height) opens upward when there's no room below and more above
		const r = input.getBoundingClientRect();
		list.classList.toggle('up', window.innerHeight - r.bottom < 300 && r.top > window.innerHeight - r.bottom);
		list.hidden = false;
		input.setAttribute('aria-expanded', 'true');
		fill('');
		input.select();
	};
	const hide = () => {
		if (!open) return;
		open = false;
		list.hidden = true;
		input.setAttribute('aria-expanded', 'false');
	};
	const pick = (f) => {
		hide();
		input.value = f;
		input.style.fontFamily = `"${f}", "Segoe UI", sans-serif`;
		if (f !== cur) { cur = f; onPick(f); }
	};

	input.addEventListener('focus', show);
	input.addEventListener('mousedown', () => { if (document.activeElement === input) show(); });
	input.addEventListener('input', () => { if (!open) show(); fill(input.value); });
	input.addEventListener('blur', () => {
		// typed a name and left: take it if it's a font (any case), else put the current one back
		const typed = input.value.trim().toLowerCase();
		const match = S.fonts.find((f) => f.toLowerCase() === typed);
		hide();
		if (match) pick(match);
		else input.value = cur;
	});
	input.addEventListener('keydown', (e) => {
		if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
			e.preventDefault();
			if (!open) return show();
			if (!items.length) return;
			hi = clamp(hi + (e.key === 'ArrowDown' ? 1 : -1), 0, items.length - 1);
			mark(false);
		} else if (e.key === 'Enter') {
			e.preventDefault();
			if (open && items[hi]) pick(items[hi].textContent);
			input.blur();
		} else if (e.key === 'Escape') {
			e.stopPropagation();
			input.value = cur;
			hide();
			input.blur();
		}
	});
	return wrap;
}

function makeFontRow(label, hint, fontObj, onChange) {
	const cur = {
		face: fontObj?.face || 'Segoe UI',
		size: Number(fontObj?.size) || 24,
		flags: Number(fontObj?.flags) || 0,
		style: fontObj?.style || '',
	};
	const faceIn = makeFontPicker(cur.face, (f) => { cur.face = f; onChange({ ...cur }, true); });
	const sizeIn = h('input', {
		type: 'number',
		class: 'font-size',
		min: 8, max: 144, step: 1,
		value: cur.size,
		oninput: () => { cur.size = clamp(Number(sizeIn.value) || 24, 8, 144); onChange({ ...cur }, false); },
		onchange: () => { cur.size = clamp(Number(sizeIn.value) || 24, 8, 144); sizeIn.value = cur.size; onChange({ ...cur }, true); },
	});
	const boldBtn = h('button', {
		type: 'button',
		class: 'font-style-btn bold' + ((cur.flags & 1) ? ' on' : ''),
		title: 'Bold',
		text: 'B',
		onclick: () => {
			cur.flags ^= 1;
			boldBtn.classList.toggle('on', !!(cur.flags & 1));
			onChange({ ...cur }, true);
		},
	});
	const italBtn = h('button', {
		type: 'button',
		class: 'font-style-btn italic' + ((cur.flags & 2) ? ' on' : ''),
		title: 'Italic',
		text: 'I',
		onclick: () => {
			cur.flags ^= 2;
			italBtn.classList.toggle('on', !!(cur.flags & 2));
			onChange({ ...cur }, true);
		},
	});
	return makeRow(label, hint, h('div', { class: 'font-ctrl' }, faceIn, sizeIn, boldBtn, italBtn));
}

// ------------------------------------------------------------------ top bar & banners

function renderTopBar() {
	const bar = $('#sourceBar');
	const sources = S.data?.sources || [];
	const cur = currentSource();

	bar.replaceChildren();
	if (sources.length > 0 && sources.length <= 5) {
		for (const src of sources) {
			const on = cur && cur.name === src.name;
			bar.append(h('button', {
				type: 'button',
				class: 'src-tab' + (on ? ' on' : ''),
				onclick: () => selectSource(src.name),
			},
				src.active ? h('span', { class: 'live-dot', title: 'Visible in current scene' }) : null,
				h('span', { text: src.name }),
				h('span', { class: 'kind', text: src.kind === 'pet' ? 'Pet' : 'Chat' }),
			));
		}
	} else if (sources.length > 5) {
		const sel = h('select', {
			'aria-label': 'Source',
			onchange: () => selectSource(sel.value),
		}, ...sources.map((src) => h('option', {
			value: src.name,
			selected: cur && cur.name === src.name,
			text: `${src.name} (${src.kind === 'pet' ? 'Pet' : 'Chat'})`,
		})));
		bar.append(sel);
	}

	// Account pill
	const acct = S.data?.account;
	const pill = $('#acctPill');
	if (acct) {
		const missing = !!(acct.twitch_missing || acct.kick_missing);
		let cls = 'acct-pill';
		let label = 'Not signed in';
		if (acct.linking) {
			cls += ' linking';
			label = 'Linking ' + (acct.link_platform === 'kick' ? 'Kick…' : 'Twitch…');
		} else if (acct.signed_in) {
			cls += missing ? ' warn' : ' ok';
			const who = [];
			if (acct.login) who.push(acct.login);
			if (acct.kick_login) who.push(acct.kick_login);
			label = who.join(' · ') || 'Signed in';
		}
		pill.className = cls;
		$('.acct-label', pill).textContent = label;
		pill.onclick = () => {
			if (currentSource()) S.section = 'account';
			if (S.view !== 'sources') return goView('sources');
			if (currentSource()) renderApp();
			else $('#emptyState')?.scrollIntoView({ behavior: 'smooth' });
		};
	}
}

function renderBanners() {
	const wrap = $('#banners');
	wrap.replaceChildren();
	const acct = S.data?.account;
	const cur = currentSource();
	if (!acct) return;

	if (acct.linking) {
		const pf = acct.link_platform === 'kick' ? 'Kick' : 'Twitch';
		wrap.append(h('div', { class: 'banner' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: `Finish signing in with ${pf}` }),
				h('div', { class: 'banner-sub', text: acct.message || 'Enter this code on the sign-in page that opened in your browser.' }),
			),
			acct.code ? h('span', { class: 'link-code', text: acct.code }) : null,
			h('div', { class: 'banner-actions' },
				acct.code ? h('button', {
					type: 'button', class: 'small', text: 'Copy code',
					onclick: () => navigator.clipboard?.writeText(acct.code).then(() => toast('Code copied', 'ok')),
				}) : null,
				acct.link_url ? h('button', {
					type: 'button', class: 'primary small', text: 'Open sign-in page',
					onclick: () => runAction({ action: 'open_sign_in_page' }, { refresh: false }),
				}) : null,
				h('button', {
					type: 'button', class: 'ghost small', text: 'Cancel',
					onclick: () => runAction({ action: 'cancel_sign_in' }),
				}),
			),
		));
	}

	if (!acct.linking && acct.twitch_missing) {
		wrap.append(h('div', { class: 'banner warn' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: 'Twitch needs updated permissions' }),
				h('div', { class: 'banner-sub', text: acct.twitch_missing }),
			),
			h('div', { class: 'banner-actions' },
				h('button', {
					type: 'button', class: 'primary small', text: 'Re-authorize Twitch',
					onclick: () => runAction({ action: 'sign_in', platform: 'twitch' }),
				}),
			),
		));
	}

	if (!acct.linking && acct.kick_missing) {
		wrap.append(h('div', { class: 'banner warn' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: 'Kick needs updated permissions' }),
				h('div', { class: 'banner-sub', text: acct.kick_missing }),
			),
			h('div', { class: 'banner-actions' },
				h('button', {
					type: 'button', class: 'primary small', text: 'Re-authorize Kick',
					onclick: () => runAction({ action: 'sign_in', platform: 'kick' }),
				}),
			),
		));
	}

	if (cur?.kind === 'pet' && !cur.meta?.pack_ok) {
		wrap.append(h('div', { class: 'banner err' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: 'No character loaded' }),
				h('div', { class: 'banner-sub', text: cur.meta?.pack_err || 'Pick a character below or reload the pack.' }),
			),
			h('div', { class: 'banner-actions' },
				h('button', {
					type: 'button', class: 'small', text: 'Reload character',
					onclick: () => runAction({ action: 'reload_pack', source: cur.name }, { okToast: 'Character reloaded' }),
				}),
			),
		));
	}

	if (cur?.kind === 'pet' && cur.meta?.moved) {
		wrap.append(h('div', { class: 'banner warn' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: 'Pet source is moved or scaled in OBS' }),
				h('div', { class: 'banner-sub', text: 'OBSPETS works best fitted to the full canvas so it can walk edge-to-edge and hop onto chat.' }),
			),
			h('div', { class: 'banner-actions' },
				h('button', {
					type: 'button', class: 'primary small', text: 'Fit to screen',
					onclick: () => runAction({ action: 'fit_to_screen', source: cur.name }, { okToast: 'Fitted to screen' }),
				}),
			),
		));
	}

	if (cur?.kind === 'chat' && !cur.meta?.has_browser) {
		wrap.append(h('div', { class: 'banner err' },
			h('div', { class: 'banner-body' },
				h('div', { class: 'banner-title', text: "OBS's Browser source (obs-browser) isn't available" }),
				h('div', { class: 'banner-sub', text: 'OBSPETS Chat needs OBS Studio built with Browser source support to display messages.' }),
			),
		));
	}
}

function selectSource(name) {
	if (S.selectedName === name) return;
	S.selectedName = name;
	const src = currentSource();
	if (src) {
		updateHash(src);
		const secs = validSectionsFor(src);
		if (!secs.some((x) => x.id === S.section)) S.section = defaultSectionFor(src);
	}
	renderTopBar();
	renderBanners();
	renderApp();
}

// ------------------------------------------------------------------ main render

function renderApp() {
	if (S.view !== 'sources') return;   // (Characters is shown: its hash and its view stay)
	const src = currentSource();
	S.stageCanvas = null;
	S.thumbCanvases = [];

	if (!src) {
		$('#app').hidden = true;
		$('#emptyState').hidden = false;
		renderEmptyState();
		return;
	}

	$('#app').hidden = false;
	$('#emptyState').hidden = true;
	updateHash(src);

	if (src.kind === 'pet') {
		renderPetRail(src);
		renderPetContent(src);
	} else {
		renderChatRail(src);
		renderChatContent(src);
	}
}

function renderEmptyState() {
	const el = $('#emptyState');
	el.replaceChildren(
		h('div', { class: 'empty-box' },
			h('h2', { text: 'No OBSPETS sources in OBS yet' }),
			h('p', { text: 'Add a pet or OBSPETS Chat to your current scene right here, or sign in with Twitch or Kick.' }),
			h('div', { class: 'empty-actions' },
				h('button', {
					type: 'button', class: 'primary', text: '+ Add a pet to this scene',
					onclick: async () => {
						const r = await runAction({ action: 'add_pet' }, { okToast: 'Added OBSPETS to scene' });
						if (r?.name) selectSource(r.name);
					},
				}),
				h('button', {
					type: 'button', text: '+ Add OBSPETS Chat',
					onclick: async () => {
						const r = await runAction({ action: 'add_chat' }, { okToast: 'Added OBSPETS Chat to scene' });
						if (r?.name) selectSource(r.name);
					},
				}),
				h('a', { class: 'btn ghost', href: editorUrl(''), text: 'Make a character' }),
			),
		),
		h('div', { style: { marginTop: '20px', textAlign: 'left' } }, buildAccountCard(null)),
	);
}

// ------------------------------------------------------------------ PET RAIL

function renderPetRail(src) {
	const rail = $('#rail');
	const cfg = src.settings;
	const nPets = clamp(Number(cfg.pets) || 1, 1, MAX_PETS);

	// 1. Live animated Pet stage card
	const stageCanvas = h('canvas', {
		class: 'pet-stage-canvas',
		width: 340,
		height: 184,
		title: 'Click to cycle animation',
		onclick: () => {
			S.stageAnimIdx = (S.stageAnimIdx + 1) % STAGE_ANIMS.length;
			animLabel.textContent = animLabel.textContent.replace(/Preview: \w+$/, `Preview: ${STAGE_ANIMS[S.stageAnimIdx]}`);
		},
	});
	S.stageCanvas = stageCanvas;

	const petName = slotPetName(src, 0), packName = slotPackName(src, 0);
	const animLabel = h('div', {
		class: 'stage-sub',
		text: nPets > 1 ? `Preview: ${STAGE_ANIMS[S.stageAnimIdx]}` : `${petName.toLowerCase() === packName.toLowerCase() ? '' : packName + ' · '}Preview: ${STAGE_ANIMS[S.stageAnimIdx]}`,
	});

	const stageCard = h('div', { class: 'stage-card' },
		h('div', { class: 'pet-stage-wrap' }, stageCanvas),
		h('div', { class: 'stage-foot' },
			h('div', { class: 'stage-meta' },
				h('div', { class: 'stage-name', text: nPets > 1 ? `${nPets} pets` : petName }),
				animLabel,
			),
			nPets > 1 ? null : h('a', {
				class: 'btn small ghost',
				href: editorUrl(slotPackId(src, 0)),
				title: 'Open it in Characters',
				text: `Edit ${packName}`,
			}),
		),
	);

	// 2. Section navigation
	const acct = S.data.account;
	const nav = h('nav', { class: 'rail-nav' }, ...PET_SECTIONS.map((sec) => {
		let right = null;
		if (sec.id === 'pets') right = h('span', { class: 'nav-badge', text: String(nPets) });
		if (sec.id === 'automod' && acct.signed_in) {
			if (/auto mod/i.test(acct.twitch_missing + acct.kick_missing)) right = h('span', { class: 'nav-dot warn' });
			else if (automodRunning(src)) right = h('span', { class: 'nav-dot' });
		}
		if (sec.id === 'account') {
			if (acct.twitch_missing || acct.kick_missing) right = h('span', { class: 'nav-dot warn' });
			else if (acct.signed_in) right = h('span', { class: 'nav-dot' });
		}
		return h('button', {
			type: 'button',
			class: S.section === sec.id ? 'on' : '',
			onclick: () => { S.section = sec.id; renderPetRail(src); renderPetContent(src); },
		}, h('span', { text: sec.label }), right);
	}));

	// 3. Quick Test Pad
	const fireTest = (what, btn) => {
		btn.classList.add('fired');
		setTimeout(() => btn.classList.remove('fired'), 500);
		runAction({ action: 'test', source: src.name, what }, { refresh: false });
	};

	const moreWrap = h('div', { class: 'test-more', hidden: !S.testMoreOpen });
	for (const [grp, items] of MORE_TESTS) {
		moreWrap.append(
			h('div', { class: 'test-more-label', text: grp }),
			h('div', { class: 'test-chips' }, ...items.map(([what, label]) => {
				const kind = what.startsWith('mood:') ? what.slice(5) : '';
				const off = kind && noPetCan(src, kind);
				const b = h('button', {
					type: 'button', class: 'test-chip', text: label, disabled: !!off,
					title: off ? `${needsText(slotCant(src, 0)[kind])}: no pet here has one` : null,
					onclick: () => fireTest(what, b),
				});
				return b;
			})),
		);
	}

	const toggleMore = h('button', {
		type: 'button',
		class: 'ghost small',
		text: S.testMoreOpen ? 'Less ▴' : 'All reactions ▾',
		onclick: () => {
			S.testMoreOpen = !S.testMoreOpen;
			moreWrap.hidden = !S.testMoreOpen;
			toggleMore.textContent = S.testMoreOpen ? 'Less ▴' : 'All reactions ▾';
		},
	});

	const testPad = h('div', { class: 'test-pad' },
		h('div', { class: 'test-pad-head' },
			h('span', { class: 'test-pad-title', text: 'Try in OBS' }),
			toggleMore,
		),
		h('div', { class: 'test-grid' }, ...QUICK_TESTS.map(([what, label]) => {
			const b = h('button', {
				type: 'button', class: 'test-btn', text: label,
				onclick: () => fireTest(what, b),
			});
			return b;
		})),
		moreWrap,
	);

	rail.replaceChildren(stageCard, nav, testPad);
}

// ------------------------------------------------------------------ PET SECTIONS

function renderPetContent(src) {
	const box = $('#content');
	box.replaceChildren();
	switch (S.section) {
		case 'pets': box.append(...buildPetSlotsSection(src)); break;
		case 'scene': box.append(...buildPetSceneSection(src)); break;
		case 'reactions': box.append(...buildPetReactionsSection(src)); break;
		case 'automod': box.append(...buildPetAutomodSection(src)); break;
		case 'habits': box.append(...buildPetHabitsSection(src)); break;
		case 'label': box.append(...buildPetLabelSection(src)); break;
		case 'custom': box.append(...buildPetCustomSection(src)); break;
		case 'try': box.append(...buildTryChatSection(src)); return;   // (a tool, not settings: no reset)
		case 'account': box.append(...buildAccountSection(src)); break;
	}
	box.append(buildResetFooter(src));
}

function packTitle(id) {
	const p = S.data.packs.find((x) => x.id === id);
	return p ? p.name : id || 'Default';
}

function buildPetSlotsSection(src) {
	const cfg = src.settings;
	const nPets = clamp(Number(cfg.pets) || 1, 1, MAX_PETS);
	const out = [];

	out.push(h('div', { class: 'sec-head' },
		h('div', {},
			h('h2', { class: 'sec-title', text: 'Pets' }),
			h('p', { class: 'sec-sub', text: 'Up to 6. Each pet can handle different reactions.' }),
		),
		h('div', { class: 'seg', role: 'radiogroup', 'aria-label': 'How many pets' },
			...[1, 2, 3, 4, 5, 6].map((n) => h('button', {
				type: 'button',
				class: n === nPets ? 'on' : '',
				text: String(n),
				title: `${n} ${n === 1 ? 'pet' : 'pets'}`,
				onclick: () => {
					patchSource(src.name, { pets: n }, { immediate: true });
					renderPetRail(src);
					renderPetContent(src);
				},
			})),
		),
	));

	const slots = h('div', { class: 'pet-slots' });
	// every pet's chance, with pet `slot`'s as `v` (the one being dragged)
	const chanceTotal = (slot, v) => {
		let t = 0;
		for (let j = 0; j < nPets; j++) t += j === slot ? v : Number(cfg[slotKey('weight', j)]) || 5;
		return t || 1;
	};
	const chanceRows = [];
	for (let i = 0; i < nPets; i++) {
		const pKey = slotKey('pack', i);
		const nKey = slotKey('name', i);
		const sKey = slotKey('scale', i);
		const wKey = slotKey('weight', i);
		const packId = slotPackId(src, i);
		const usingCustomDir = i === 0 && !!(cfg.pack_dir || '').trim();

		const thumb = h('canvas', {
			class: 'pet-thumb',
			width: 76,
			height: 64,
			title: 'Browse all characters',
			onclick: () => openCharacterGallery(src, i),
		});
		S.thumbCanvases.push({ canvas: thumb, packId });

		const packSel = h('select', {
			disabled: usingCustomDir,
			onchange: () => {
				const nextId = packSel.value;
				patchSource(src.name, { [pKey]: nextId }, { immediate: true, refreshAfter: i === 0 });
				const entry = S.thumbCanvases.find((t) => t.canvas === thumb);
				if (entry) entry.packId = nextId;
				edBtn.href = editorUrl(nextId);
				nameInput.placeholder = packTitle(nextId);
				if (i === 0 || nPets > 1) renderPetRail(currentSource() || src);   // (the stage card names it)
			},
		}, ...S.data.packs.map((p) => h('option', {
			value: p.id,
			selected: p.id === packId,
			text: p.name + (p.user ? ' (yours)' : ''),
		})));

		const edBtn = h('a', {
			class: 'btn small ghost',
			href: editorUrl(packId),
			title: 'Open it in Characters',
			text: 'Edit',
		});

		const browseBtn = h('button', {
			type: 'button',
			class: 'small',
			disabled: usingCustomDir,
			text: 'Browse',
			onclick: () => openCharacterGallery(src, i),
		});

		const nameInput = h('input', {
			type: 'text',
			value: cfg[nKey] || '',
			placeholder: packTitle(packId),
			oninput: () => {
				patchSource(src.name, { [nKey]: nameInput.value });
				if (i === 0 && nPets === 1) renderPetRail(src);   // (the stage card names it)
			},
		});

		const body = h('div', { class: 'pet-card-body' },
			makeRow('Name', i === 0 ? 'Blank uses the character’s name. Fills {name} in the reward and label.' : 'Blank uses the character’s name.', nameInput),
			makeSliderRow('Size', 'Each pixel of the character drawn this many times bigger.', {
				value: Number(cfg[sKey]) || 5,
				min: 2, max: 12, step: 1,
				format: (v) => `${v}×`,
				onChange: (v, imm) => patchSource(src.name, { [sKey]: v }, { immediate: imm }),
			}),
		);

		if (nPets > 1) {
			const chanceRow = makeSliderRow('Chance', 'Its share of the random picks: who reacts to an alert, and who comes in next, when those are set to Random.', {
				value: Number(cfg[wKey]) || 5,
				min: 1, max: 10, step: 1,
				format: (v) => `${Math.round((100 * v) / chanceTotal(i, v))}% of picks`,
				onChange: (v, imm) => {
					patchSource(src.name, { [wKey]: v }, { immediate: imm });
					chanceRows.forEach((r) => r !== chanceRow && r.refresh());
				},
			});
			chanceRows.push(chanceRow);
			body.append(chanceRow);
		}

		// Per-pet reaction toggles
		const cant = slotCant(src, i);
		const enabledCount = () => REACTS.reduce((acc, r) => acc + (cfg[doKey(r.id, i)] !== false && !cant[r.id] ? 1 : 0), 0);
		const isOpen = S.openReacts.has(i);
		const countEl = h('span', { class: 'muted small', text: `${enabledCount()} of ${REACTS.length} reactions enabled` });
		const panel = h('div', { class: 'react-panel', hidden: !isOpen });

		const rebuildChips = () => {
			countEl.textContent = `${enabledCount()} of ${REACTS.length} reactions enabled`;
			panel.replaceChildren();
			for (const grp of ['Alerts', 'Chat']) {
				panel.append(h('div', { class: 'react-group-label', text: grp }));
				const chips = h('div', { class: 'react-chips' });
				for (const r of REACTS.filter((x) => x.group === grp)) {
					const k = doKey(r.id, i);
					if (cant[r.id]) {   // its character has no animation for it: off, and says what it needs
						chips.append(h('button', { type: 'button', class: 'rchip need', disabled: true, text: r.label,
							title: `${needsText(cant[r.id])}. Add one in Characters, or pick another animation to stand in for it.` }));
						continue;
					}
					const on = cfg[k] !== false;
					const b = h('button', {
						type: 'button',
						class: 'rchip' + (on ? ' on' : ''),
						text: r.label,
						onclick: () => {
							const next = cfg[k] === false;
							patchSource(src.name, { [k]: next }, { immediate: true });
							b.classList.toggle('on', next);
							countEl.textContent = `${enabledCount()} of ${REACTS.length} reactions enabled`;
						},
					});
					chips.append(b);
				}
				panel.append(chips);
				const lacking = REACTS.filter((r) => r.group === grp && cant[r.id]);
				if (lacking.length)
					panel.append(h('p', { class: 'react-need' },
						h('b', { text: 'Needs an animation: ' }),
						lacking.map((r) => `${r.label} (${cant[r.id]})`).join(', '),
						'. Add it in ', h('a', { href: editorUrl(slotPackId(src, i)), text: 'Characters' }),
						', or have another animation stand in.'));
			}
		};
		rebuildChips();

		const toggleBtn = h('button', {
			type: 'button', class: 'small ghost',
			text: isOpen ? 'Hide reactions ▴' : 'Reactions ▾',
			onclick: () => {
				if (S.openReacts.has(i)) S.openReacts.delete(i);
				else S.openReacts.add(i);
				const open = S.openReacts.has(i);
				panel.hidden = !open;
				toggleBtn.textContent = open ? 'Hide reactions ▴' : 'Reactions ▾';
			},
		});

		const setAll = (val) => {
			const patch = {};
			for (const r of REACTS) patch[doKey(r.id, i)] = val;
			patchSource(src.name, patch, { immediate: true });
			rebuildChips();
		};

		body.append(
			h('div', { class: 'react-summary' },
				countEl,
				h('div', { class: 'inline' },
					h('button', { type: 'button', class: 'small ghost', text: 'All on', onclick: () => setAll(true) }),
					h('button', { type: 'button', class: 'small ghost', text: 'All off', onclick: () => setAll(false) }),
					toggleBtn,
				),
			),
			panel,
		);

		slots.append(h('div', { class: 'pet-card' },
			h('div', { class: 'pet-card-top' },
				thumb,
				h('div', { style: { display: 'flex', flexDirection: 'column', gap: '6px', minWidth: 0 } },
					h('div', { class: 'pet-card-title' },
						h('span', { class: 'pet-slot-badge', text: `Pet ${i + 1}` }),
						usingCustomDir ? h('span', { class: 'muted small', text: 'Using custom character folder' }) : null,
					),
					h('div', { class: 'inline-stretch' }, packSel, browseBtn),
				),
				edBtn,
			),
			body,
		));
	}
	out.push(slots);

	if (nPets > 1) {
		const atOnceVal = clamp(Number(cfg.at_once) || MAX_PETS, 1, nPets);
		const multiCard = h('div', { class: 'card' },
			h('div', { class: 'card-hd' },
				h('h3', { text: 'Multiple pets' }),
				null,
			),
			h('div', { class: 'card-body' },
				makeSegRow('Who reacts', 'When an alert comes in and several pets are on screen.', [
					['turns', 'Take turns'],
					['random', 'Random'],
					['all', 'All of them'],
				], cfg.who || 'turns', (v) => patchSource(src.name, { who: v }, { immediate: true })),
				makeSliderRow('On screen at once', 'The rest wait off screen and swap in.', {
					value: atOnceVal,
					min: 1, max: nPets, step: 1,
					format: (v) => (v >= nPets ? `All (${nPets})` : `${v} of ${nPets}`),
					onChange: (v, imm) => {
						patchSource(src.name, { at_once: v >= nPets ? MAX_PETS : v }, { immediate: imm });
						if (imm) renderPetContent(src);
					},
				}),
				atOnceVal < nPets ? makeSliderRow('Swap every', 'Minutes before a pet walks off and another walks in. 0 = never.', {
					value: Number(cfg.swap_min) || 0,
					min: 0, max: 60, step: 1,
					format: (v) => (v === 0 ? 'Never' : `${v} min`),
					onChange: (v, imm) => patchSource(src.name, { swap_min: v }, { immediate: imm }),
				}) : null,
				atOnceVal < nPets ? makeToggleRow('Swap after an alert', 'Once it’s done reacting to an alert, the next pet walks in.',
					!!cfg.swap_alert, (v) => patchSource(src.name, { swap_alert: v }, { immediate: true })) : null,
				atOnceVal < nPets ? makeSegRow('Who comes in next', null, [
					['turns', 'In order'],
					['random', 'Random (by chance)'],
				], cfg.next_in || 'turns', (v) => patchSource(src.name, { next_in: v }, { immediate: true })) : null,
			),
		);
		out.push(multiCard);
	}

	return out;
}

function openCharacterGallery(src, slot) {
	const pKey = slotKey('pack', slot);
	const currentId = src.settings[pKey] || '';
	const modal = $('#modal');
	const dlg = $('#dialog');
	S.galleryCanvases = [];

	const grid = h('div', { class: 'gallery' });
	for (const p of S.data.packs) {
		const c = h('canvas', { width: 140, height: 84 });
		S.galleryCanvases.push({ canvas: c, packId: p.id });
		grid.append(h('button', {
			type: 'button',
			class: 'gcard' + (p.id === currentId ? ' on' : ''),
			onclick: () => {
				patchSource(src.name, { [pKey]: p.id }, { immediate: true, refreshAfter: slot === 0 });
				closeModal();
				renderPetRail(src);
				renderPetContent(src);
			},
		},
			c,
			h('span', { class: 'gname', text: p.name }),
			h('span', { class: 'gnote', text: (p.user ? 'Your character' : 'Built-in') + (p.nsounds ? ` · ${p.nsounds} sounds` : '') }),
		));
	}

	dlg.replaceChildren(
		h('h2', { text: `Choose a character for Pet ${slot + 1}` }),
		h('p', { class: 'sub', text: 'Click a character to use it right away, or make your own in Characters.' }),
		grid,
		h('div', { class: 'actions' },
			h('button', { type: 'button', class: 'ghost', text: '+ Make a new character', onclick: () => { closeModal(); newCharacter(); } }),
			h('div', { class: 'grow' }),
			h('button', { type: 'button', text: 'Close', onclick: closeModal }),
		),
	);
	modal.hidden = false;
}

function buildPetSceneSection(src) {
	const cfg = src.settings;
	const meta = src.meta || {};
	const pics = S.data.picture_sources || [];

	const makeSourceSelect = (curVal, autoName, onChange) => {
		const sel = h('select', { onchange: () => onChange(sel.value) },
			h('option', { value: '', selected: curVal === '', text: autoName ? `Auto (${autoName})` : 'Auto (find by name)' }),
			h('option', { value: NO_SOURCE, selected: curVal === NO_SOURCE, text: 'None' }),
			...pics.map((p) => h('option', {
				value: p.name,
				selected: curVal === p.name,
				text: p.name + (p.is_obspet_chat ? ' ★ OBSPETS Chat' : ''),
			})),
		);
		if (curVal && curVal !== NO_SOURCE && !pics.some((p) => p.name === curVal)) {
			sel.append(h('option', { value: curVal, selected: true, text: `${curVal} (not in scene)` }));
		}
		return sel;
	};

	// Chat status callout
	let chatInfo = null;
	if (meta.chat_kind === 'ours') {
		chatInfo = h('div', { class: 'info-box ok inline' },
			h('span', { class: 'grow', text: `Using “${meta.chat_name}”. The pet can sit on it and yank messages out.` }),
			h('button', {
				type: 'button', class: 'small', text: 'Chat settings',
				onclick: () => selectSource(meta.chat_name),
			}),
		);
	} else if (meta.chat_kind === 'other') {
		chatInfo = h('div', { class: 'info-box warn inline' },
			h('span', { class: 'grow', text: `Using “${meta.chat_name}”, which isn’t an OBSPETS Chat. With one, the pet sits on the exact top message.` }),
			h('button', {
				type: 'button', class: 'small primary', text: '+ Add OBSPETS Chat',
				onclick: async () => {
					const r = await runAction({ action: 'add_chat', source: src.name }, { okToast: 'Added OBSPETS Chat to scene' });
					if (r?.name) await refreshState();
				},
			}),
		);
	} else {
		chatInfo = h('div', { class: 'info-box inline' },
			h('span', { class: 'grow', text: 'No chat found. Add an OBSPETS Chat for the pet to sit on and swat.' }),
			h('button', {
				type: 'button', class: 'small primary', text: '+ Add OBSPETS Chat',
				onclick: async () => {
					const r = await runAction({ action: 'add_chat', source: src.name }, { okToast: 'Added OBSPETS Chat to scene' });
					if (r?.name) await refreshState();
				},
			}),
		);
	}

	const chatCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Chat overlay' }),
			null,
		),
		h('div', { class: 'card-body' },
			chatInfo,
			makeRow('Chat source', 'Auto picks OBSPETS Chat, or a source with “chat” in its name.',
				makeSourceSelect(cfg.perch || '', meta.auto_chat, (v) => patchSource(src.name, { perch: v }, { immediate: true, refreshAfter: true })),
			),
			meta.chat_kind === 'other' ? makeToggleRow('Sit on top of this chat', 'Finds the top message from the overlay’s pixels.',
				!!cfg.perch_box, (v) => patchSource(src.name, { perch_box: v }, { immediate: true })) : null,
			meta.chat_kind === 'other' ? makeToggleRow('Yank messages from this chat', null,
				!!cfg.other_yank, (v) => patchSource(src.name, { other_yank: v }, { immediate: true })) : null,
		),
	);

	// Avoid list pills
	const avoidItems = [...(meta.avoid_items || [])];
	// the ones it may also sit on top of ("sit_on", a list beside "avoid_list")
	const sitOn = new Set((Array.isArray(cfg.sit_on) ? cfg.sit_on : []).map((x) => x?.value).filter(Boolean));
	const pillsWrap = h('div', { class: 'pills' });
	const saveAvoid = () => {
		meta.avoid_items = [...avoidItems];
		for (const n of [...sitOn]) if (!avoidItems.includes(n)) sitOn.delete(n);
		patchSource(src.name, {
			avoid_list: avoidItems.map((value) => ({ value })), avoid: '',
			sit_on: [...sitOn].map((value) => ({ value })),
		}, { immediate: true });
		renderPills();
	};
	const renderPills = () => {
		pillsWrap.replaceChildren();
		if (!avoidItems.length) {
			pillsWrap.append(h('span', { class: 'muted small', text: 'None yet: the pet walks the whole width of the canvas.' }));
			return;
		}
		for (const item of avoidItems) {
			const sitBox = h('input', {
				type: 'checkbox', checked: sitOn.has(item),
				onchange: () => {
					if (sitBox.checked) sitOn.add(item);
					else sitOn.delete(item);
					saveAvoid();
				},
			});
			pillsWrap.append(h('span', { class: 'pill' },
				h('span', { text: item }),
				h('label', { class: 'pill-opt', title: 'Lets the pet jump up, hang out and nap on top of it' }, sitBox, 'Can sit on top'),
				h('button', {
					type: 'button', title: 'Remove', text: '×',
					onclick: () => {
						const idx = avoidItems.indexOf(item);
						if (idx >= 0) avoidItems.splice(idx, 1);
						saveAvoid();
					},
				}),
			));
		}
	};
	renderPills();

	const avoidSel = h('select', {},
		h('option', { value: '', text: 'Pick a source to keep off…' }),
		...pics.map((p) => h('option', { value: p.name, text: p.name })),
	);
	const addAvoidBtn = h('button', {
		type: 'button', text: 'Add',
		onclick: () => {
			const v = avoidSel.value.trim();
			if (!v) return;
			if (!avoidItems.includes(v)) {
				avoidItems.push(v);
				saveAvoid();
			}
			avoidSel.value = '';
		},
	});

	const sceneCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Scene layout' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeSliderRow('Floor height', 'Above the bottom of the canvas.', {
				value: Number(cfg.floor ?? 24),
				min: 0, max: 300, step: 2,
				format: (v) => `${v} px`,
				onChange: (v, imm) => patchSource(src.name, { floor: v }, { immediate: imm }),
			}),
			makeRow('Alert box', 'The pet turns toward it when an alert comes in. Auto picks a source with “alert” in its name.',
				makeSourceSelect(cfg.alert || '', meta.auto_alert, (v) => patchSource(src.name, { alert: v }, { immediate: true, refreshAfter: true })),
			),
			makeRow('Sources to keep off', 'The pet walks around these, like your webcam or goals. Tick “Can sit on top” to let it jump up there now and then to look around or nap. It hops down for alerts and chat.',
				h('div', { style: { width: '100%' } },
					h('div', { class: 'inline-stretch' }, avoidSel, addAvoidBtn),
					pillsWrap,
				), 'stack',
			),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Chat & Scene' }),
				null,
			),
			h('button', {
				type: 'button', class: 'small', text: 'Fit pet to screen',
				onclick: () => runAction({ action: 'fit_to_screen', source: src.name }, { okToast: 'Fitted to screen' }),
			}),
		),
		chatCard,
		sceneCard,
	];
}

const REWARD_WORDS = {
	there: 'reward is set up',
	made: 'reward created',
	missing: 'no reward with this title yet',
	checking: 'checking…',
	not_allowed: 'channel points need Affiliate or Partner',
	failed: 'couldn’t check the reward',
	unknown: 'not checked yet',
};
const rewardWords = (state, msg) => (state === 'failed' && msg) || REWARD_WORDS[state] || msg || state;

function buildPetReactionsSection(src) {
	const cfg = src.settings;
	const acct = S.data.account;
	const effName = slotPetName(src, 0);

	const rewardInput = h('input', {
		type: 'text',
		value: cfg.reward ?? 'Pet {name}',
		placeholder: 'Pet {name}',
		oninput: () => {
			rewardPreview.textContent = 'Live title: ' + (rewardInput.value || '').replaceAll('{name}', effName);
			patchSource(src.name, { reward: rewardInput.value });
		},
		onchange: () => patchSource(src.name, { reward: rewardInput.value }, { immediate: true, refreshAfter: true }),
	});
	const rewardPreview = h('div', {
		class: 'row-hint',
		text: 'Live title: ' + (cfg.reward ?? 'Pet {name}').replaceAll('{name}', effName),
	});

	// Inline reward status box
	let rewardStatusBox = null;
	if (acct.signed_in && (acct.login || acct.kick_login)) {
		const parts = [];
		const needsMake = acct.reward === 'missing' || acct.kick_reward === 'missing';
		if (acct.login) parts.push(`Twitch: ${rewardWords(acct.reward, acct.reward_msg)}`);
		if (acct.kick_login) parts.push(`Kick: ${rewardWords(acct.kick_reward, acct.kick_reward_msg)}`);
		const isOk = (!acct.login || acct.reward === 'there' || acct.reward === 'made') &&
			(!acct.kick_login || acct.kick_reward === 'there' || acct.kick_reward === 'made');
		rewardStatusBox = h('div', { class: 'info-box inline ' + (isOk ? 'ok' : needsMake ? 'warn' : '') },
			h('span', { class: 'grow', text: parts.join('  ·  ') }),
			needsMake ? h('button', {
				type: 'button', class: 'small primary', text: 'Create it',
				onclick: () => runAction({ action: 'make_reward', source: src.name }, { okToast: 'Creating the reward…' }),
			}) : null,
			h('button', {
				type: 'button', class: 'small ghost', text: 'Check again',
				title: 'Asks Twitch and Kick again whether the reward is there, say after you renamed or deleted it there.',
				onclick: () => runAction({ action: 'check_reward', source: src.name }),
			}),
		);
	}

	const alertsCard = h('div', { class: 'card' + (cfg.reactions === false ? ' is-off' : '') },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Alerts & channel points' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeToggleRow('Follows, subs, bits & raids', null,
				cfg.twitch_alerts !== false, (v) => patchSource(src.name, { twitch_alerts: v }, { immediate: true })),
			makeRow('Channel-point reward title', null,
				h('div', { style: { width: '100%' } }, rewardInput, rewardPreview),
			),
			rewardStatusBox,
		),
	);

	const ignoreInput = h('textarea', {
		rows: 2,
		value: cfg.ignore ?? S.data.default_bots ?? '',
		placeholder: 'nightbot, streamelements, fossabot…',
		oninput: () => patchSource(src.name, { ignore: ignoreInput.value }),
	});

	const aiCard = h('div', { class: 'card' + (cfg.reactions === false ? ' is-off' : '') },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Chat' }),
			acct.mood_note ? h('span', { class: 'card-desc', text: acct.mood_note }) : null,
		),
		h('div', { class: 'card-body' },
			makeToggleRow('React to chat', 'AI reads chat and picks a reaction.',
				cfg.moods !== false, (v) => patchSource(src.name, { moods: v }, { immediate: true })),
			makeRow('Ignore these chatters', 'Comma-separated, usually bots.',
				h('div', { style: { width: '100%' } },
					ignoreInput,
					h('div', { class: 'inline', style: { marginTop: '6px', justifyContent: 'flex-end' } },
						h('button', {
							type: 'button', class: 'small ghost', text: 'Restore default bots',
							onclick: () => {
								ignoreInput.value = S.data.default_bots || '';
								patchSource(src.name, { ignore: ignoreInput.value }, { immediate: true });
							},
						}),
					),
				), 'stack',
			),
			h('div', { class: 'info-box inline' },
				h('span', { class: 'grow', text: 'Usage and credit are on sindruid.com.' }),
				h('button', {
					type: 'button', class: 'small ghost', text: 'Test chat',
					onclick: () => { S.section = 'try'; renderPetRail(src); renderPetContent(src); },
				}),
				S.data.backend ? h('a', { class: 'btn small', href: S.data.backend + '/streamtools/obspets', target: '_blank', rel: 'noopener', text: 'sindruid.com ↗' }) : null,
			),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Reactions & AI' }),
				null,
			),
		),
		h('div', { class: 'card' }, h('div', { class: 'card-body' },
			makeToggleRow('Reactions', 'Off: the pet ignores alerts, channel points and chat.',
				cfg.reactions !== false, (v) => patchSource(src.name, { reactions: v }, { immediate: true, refreshAfter: true })),
		)),
		alertsCard,
		acct.signed_in ? buildAccountChatCard(acct) : null,
		aiCard,
	];
}

// The account's reactions to chat: they spend its credit, so sindruid.com keeps them, for every OBS
// and pet on the account (Hub::set_reacting). Credit and usage stay on sindruid.com.
// (the pet page's slider has the same stops: GAPS in streamtools/backend/src/pages.js, keep in step)
// Every second from 3 s (the backend's least) to a minute, then every 5 s to 2 min, every 15 s to 5 min.
const GAPS = [];
for (let g = 3; g <= 300; g += g < 60 ? 1 : g < 120 ? 5 : 15) GAPS.push(g);
function gapText(g) {   // 5 -> "5 s", 90 -> "1½ min", 75 -> "1 min 15 s"
	if (g < 60) return `${g} s`;
	const m = Math.floor(g / 60), s = g % 60;
	return s === 30 ? `${m}½ min` : `${m} min` + (s ? ` ${s} s` : '');
}
function buildAccountChatCard(acct) {
	const cur = { on: acct.react_on !== false, gap: Number(acct.ai_gap) || 5, daily_cap: Number(acct.daily_cap) || 0 };
	const stops = GAPS.includes(cur.gap) ? GAPS : GAPS.concat(cur.gap).sort((a, b) => a - b);
	let timer = 0;
	const save = (patch, now) => {
		Object.assign(cur, patch);
		clearTimeout(timer);
		const go = () => runAction({ action: 'set_reacting', ...cur }, { okToast: 'Saved' });
		if (now) go(); else timer = setTimeout(go, 600);
	};
	const cap = h('input', {
		type: 'number', min: 0, max: 20000, step: 10, value: cur.daily_cap, style: { width: '110px' },
		onchange: () => save({ daily_cap: Math.max(0, Math.min(20000, Math.round(Number(cap.value)) || 0)) }, true),
	});
	return h('div', { class: 'card' + (cur.on ? '' : ' is-off') },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Chat reactions for your account' }),
			h('span', { class: 'card-desc', text: 'Every OBS and pet on your account. These use credit.' }),
		),
		h('div', { class: 'card-body' },
			makeToggleRow('React to chat', 'Off: no reactions to chat and no Auto mod, on any OBS.',
				cur.on, (v) => save({ on: v }, true)),
			makeSliderRow('Time between reactions', (i) => `The AI looks at chat at most every ${gapText(stops[i])}, and the pet reacts at most that often (getting angry and ripping out a message don't wait). More time makes your credit last longer. The pet still reads every message.`, {
				value: Math.max(0, stops.indexOf(cur.gap)), min: 0, max: stops.length - 1, step: 1,
				format: (i) => gapText(stops[i]),
				onChange: (i, imm) => { if (imm) save({ gap: stops[i] }, false); },
			}),
			makeRow('Reactions a day, at most', 'Also caps how much credit a day can use. 0 stops them.', cap),
			h('div', { class: 'info-box inline' },
				h('span', { class: 'grow', text: 'Credit, usage and how long your credit lasts are on sindruid.com.' }),
				S.data.backend ? h('a', { class: 'btn small', href: S.data.backend + '/streamtools/obspets', target: '_blank', rel: 'noopener', text: 'sindruid.com ↗' }) : null,
			),
		),
	);
}

// ------------------------------------------------------------------ AUTO MOD

// Auto mod's categories (the backend's AUTOMOD in streamtools/backend/src/mood.js: keep in step)
const AUTOMOD_CATS = [
	{ id: 'bots', label: 'Spam bots', about: 'Selling followers, viewers or “promotion”.' },
	{ id: 'scams', label: 'Scams and phishing', about: 'Crypto, fake giveaways, “free” Nitro or skins, suspicious links.' },
	{ id: 'slurs', label: 'Slurs and hate speech', about: 'Slurs, and hate aimed at who someone is.' },
	{ id: 'threats', label: 'Threats and harassment', about: 'Threats, “kys”, doxxing, going after one person.' },
	{ id: 'sexual', label: 'Sexual messages', about: 'Explicit messages, and creepy comments about people.' },
	{ id: 'self_promo', label: 'Self-promotion', about: 'Viewers advertising their own channel or socials.' },
	{ id: 'flooding', label: 'Flooding', about: 'Walls of text, copypastas, ASCII art, emote walls.' },
];

// It deletes for real only with all of these: this source's switch, the account's, the AI on, a category
function automodRunning(src) {
	const a = S.data.account;
	return !!(a.signed_in && a.moods_server_on && a.automod_on && src.settings.automod !== false && (a.automod || []).length);
}

function buildPetAutomodSection(src) {
	const cfg = src.settings;
	const acct = S.data.account;
	const head = h('div', { class: 'sec-head' },
		h('div', {},
			h('h2', { class: 'sec-title', text: 'Auto mod' }),
			h('p', { class: 'sec-sub', text: 'Reads your Twitch and Kick chat and deletes what you pick below. It never touches you, your mods or your VIPs.' }),
		),
	);
	if (!acct.signed_in) {
		return [head, h('div', { class: 'card' }, h('div', { class: 'card-body' },
			h('div', { class: 'info-box' }, 'Auto mod runs on your Stream Tools account. Sign in on the Account page to turn it on.'),
			h('button', {
				type: 'button', class: 'primary', text: 'Go to Account',
				onclick: () => { S.section = 'account'; renderPetRail(src); renderPetContent(src); },
			}),
		))];
	}

	const cats = new Set(acct.automod || []);
	const on = acct.automod_on && cfg.automod !== false;
	const saveCats = () => runAction({ action: 'set_automod', on: !!acct.automod_on, cats: [...cats].map((value) => ({ value })) });

	// what stops it, if anything
	const notes = [];
	if (!acct.moods_server_on) notes.push(h('div', { class: 'info-box warn', text: 'React to chat is off for your account (Reactions & AI), so Auto mod is off too. Turn it back on there.' }));
	for (const [pf, miss] of [['Twitch', acct.twitch_missing], ['Kick', acct.kick_missing]]) {
		if (/auto mod/i.test(miss || '')) {
			notes.push(h('div', { class: 'info-box warn inline' },
				h('span', { class: 'grow', text: `${pf} hasn’t allowed OBSPETS to delete messages yet. Sign in with ${pf} again to allow it.` }),
				h('button', {
					type: 'button', class: 'small', text: 'Go to Account',
					onclick: () => { S.section = 'account'; renderPetRail(src); renderPetContent(src); },
				}),
			));
		}
	}
	if (on && !cats.size) notes.push(h('div', { class: 'info-box warn', text: 'Nothing is ticked below, so Auto mod has nothing to delete.' }));

	const mainCard = h('div', { class: 'card' },
		h('div', { class: 'card-body' },
			makeToggleRow('Auto mod', on
				? 'On. Each message is checked as it comes in, and a caught one is deleted within seconds.'
				: 'Off. Nothing is deleted.', on, (v) => {
				patchSource(src.name, { automod: v }, { immediate: true, refreshAfter: true });
				// (switched off for the account, on sindruid.com or another OBS: back on there too)
				if (v && !acct.automod_on) runAction({ action: 'set_automod', on: true, cats: [...cats].map((value) => ({ value })) });
			}),
			...notes,
		),
	);

	const catsCard = h('div', { class: 'card' + (on ? '' : ' is-off') },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'What it deletes' }),
			h('span', { class: 'card-desc', text: 'For every OBS on your account' }),
		),
		h('div', { class: 'card-body' },
			...AUTOMOD_CATS.map((c) => makeToggleRow(c.label, c.about, cats.has(c.id), (v) => {
				if (v) cats.add(c.id);
				else cats.delete(c.id);
				saveCats();
			})),
			h('div', { class: 'info-box', text: 'Rudeness, swearing, trolling, bad takes and links to clips or games stay. When it isn’t sure, it leaves the message.' }),
		),
	);

	const canRip = cfg.reactions !== false && cfg[doKey('yank', 0)] !== false;
	const show = cfg.automod_show !== false;
	const showNotes = {
		show: canRip
			? 'The message is deleted for everyone at once. On stream, the pet runs over, rips it out of the chat overlay and pounces on it, crossed out.'
			: 'The pet can’t do this now: turn on Reactions (Reactions & AI) and its “Rips a message out” reaction (Pets). Until then the message just disappears.',
		quiet: 'The message is deleted and drops out of the chat overlay. The pet doesn’t react.',
	};
	const showNote = h('div', { class: 'info-box' + (show && !canRip ? ' warn' : ''), text: showNotes[show ? 'show' : 'quiet'] });
	const streamCard = h('div', { class: 'card' + (on ? '' : ' is-off') },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'On stream' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeSegRow('When it deletes a message', null, [
				['show', 'Pet rips it out'],
				['quiet', 'It just disappears'],
			], show ? 'show' : 'quiet', (v) => {
				showNote.textContent = showNotes[v];
				showNote.classList.toggle('warn', v === 'show' && !canRip);
				patchSource(src.name, { automod_show: v === 'show' }, { immediate: true });
			}),
			showNote,
		),
	);

	const moreCard = h('div', { class: 'card' },
		h('div', { class: 'card-body' },
			h('div', { class: 'info-box inline' },
				h('span', { class: 'grow', text: 'Chatters on the ignore list (Reactions & AI) are never checked. Try a message in Test Chat to see what it would delete.' }),
				h('button', {
					type: 'button', class: 'small ghost', text: 'Test chat',
					onclick: () => { S.section = 'try'; renderPetRail(src); renderPetContent(src); },
				}),
			),
		),
	);

	return [head, mainCard, catsCard, streamCard, moreCard];
}

// ------------------------------------------------------------------ TEST CHAT

// What each chat reaction looks like on stream, in a few words (the backend's REACTIONS, short).
const TRY_LOOKS = {
	none: 'Lets it go. Most chat gets no reaction.',
	love: 'Purrs and glows, hearts float up.',
	happy: 'A happy little hop with hearts.',
	angry: 'Puffs up, stomps off and glares.',
	swat_chat: 'Runs to the chat and swipes at it.',
	yank: 'Rips the message out of chat and pounces on it.',
	greet: 'Runs over to the chat and hops hello.',
	judge: 'Stares at the chat, then turns away unimpressed.',
	scared: 'Flees from the chat and peeks back.',
	show_off: 'A big proud leap.',
	beg: 'Reaches at the chat with a paw, pleading.',
	hop_on: 'Jumps up onto the chat and sits there.',
	hop_off: 'Jumps down off the chat.',
	curious: 'A “?” over its head, creeps closer.',
	sleepy: 'Curls up for a nap.',
	zoomies: 'Dashes back and forth.',
	play: 'Swats at sparks and pounces.',
	sulk: 'Turns its back on chat with a “…”.',
	vibe: 'Bobs its head to the music.',
	startled: 'A big “!” and a jump.',
};

// One-click lines for the tester ({name}: the pet's name). The last two are what Auto mod is for.
const TRY_EXAMPLES = [
	['Hi', 'hi {name}!! missed you'],
	['Compliment', '{name} is the cutest thing on this stream'],
	['Question', 'wait what is {name} anyway?'],
	['Hype', 'LETS GOOOO THAT WAS INSANE'],
	['Rude', 'you are so bad at this game, just uninstall'],
	['Spam bot', 'best viewers and followers on streamboost dot shop'],
	['Scam', 'free nitro for everyone here: dlscord-gift.ru/claim'],
];

function tryLabel(kind) {
	if (kind === 'none') return 'No reaction';
	return REACTS.find((r) => r.id === kind)?.label || kind.replace(/_/g, ' ');
}

function buildTryChatSection(src) {
	const cfg = src.settings;
	const acct = S.data.account;
	const name = slotPetName(src, 0);
	if (!S.tryLog.has(src.name)) S.tryLog.set(src.name, []);
	if (!S.tryDraft.has(src.name)) S.tryDraft.set(src.name, { user: '', text: '' });
	const draft = S.tryDraft.get(src.name);
	if (S.tryPlay === undefined) {
		try { S.tryPlay = localStorage.getItem('obspets.tryPlay') !== '0'; } catch { S.tryPlay = true; }
	}

	const head = h('div', { class: 'sec-head' },
		h('div', {},
			h('h2', { class: 'sec-title', text: 'Test Chat' }),
			h('p', { class: 'sec-sub', text: `Type as a viewer to see how ${name} reacts and what Auto mod deletes. Nothing reaches your real chat.` }),
		),
	);

	if (!acct.signed_in) {
		return [head, h('div', { class: 'card' }, h('div', { class: 'card-pad try-signin' },
			h('div', {},
				h('div', { class: 'row-label', text: 'Sign in to test chat' }),
				h('div', { class: 'row-hint', text: 'The AI that reads chat runs on your Stream Tools account.' }),
			),
			h('button', {
				type: 'button', class: 'primary', text: 'Go to Account',
				onclick: () => { S.section = 'account'; renderPetRail(src); renderPetContent(src); },
			}),
		))];
	}

	// where the stream would differ from the answer here
	let note = null;
	if (cfg.reactions === false || cfg.moods === false) {
		note = h('div', { class: 'info-box warn', text: `Chat reactions are off for ${name}, so on stream it wouldn't act these out. Turn them on in Reactions & AI.` });
	}

	const userIn = h('input', {
		type: 'text', class: 'try-user', value: draft.user, placeholder: 'viewer', maxLength: 25,
		'aria-label': 'Viewer name', spellcheck: false,
		oninput: () => { draft.user = userIn.value; },
	});
	const textIn = h('input', {
		type: 'text', class: 'try-text', value: draft.text, maxLength: 300,
		placeholder: `Say something to ${name}…`, 'aria-label': 'Chat message',
		oninput: () => { draft.text = textIn.value; sendBtn.disabled = S.tryBusy || !textIn.value.trim(); },
	});
	const sendBtn = h('button', {
		type: 'submit', class: 'primary', text: S.tryBusy ? 'Asking…' : 'Send',
		disabled: S.tryBusy || !draft.text.trim(),
	});
	const send = (text) => {
		text = (text ?? textIn.value).trim();
		if (!text || S.tryBusy) return;
		draft.text = '';
		runTry(src, (userIn.value.trim() || 'viewer').replace(/^@/, ''), text);
	};
	const form = h('form', {
		class: 'try-compose',
		onsubmit: (e) => { e.preventDefault(); send(); },
	}, userIn, h('span', { class: 'try-colon', text: ':' }), textIn, sendBtn);

	const petName = name.toLowerCase();
	const examples = h('div', { class: 'try-examples' },
		h('span', { class: 'try-label', text: 'Try' }),
		...TRY_EXAMPLES.map(([label, text]) => h('button', {
			type: 'button', class: 'test-chip', text: label, title: text.replaceAll('{name}', petName),
			disabled: S.tryBusy,
			onclick: () => send(text.replaceAll('{name}', petName)),
		})),
	);

	const playIn = h('input', {
		type: 'checkbox', checked: S.tryPlay,
		onchange: () => {
			S.tryPlay = playIn.checked;
			try { localStorage.setItem('obspets.tryPlay', S.tryPlay ? '1' : '0'); } catch { /* (fine) */ }
		},
	});
	const foot = h('div', { class: 'try-foot' },
		h('label', { class: 'try-play' }, playIn, h('span', { text: 'Act it out in OBS' })),
		h('span', { class: 'faint small', text: 'Each try uses one AI request from your credit.' }),
	);

	const composeCard = h('div', { class: 'card' }, h('div', { class: 'card-pad try-card' }, form, examples, foot));

	const list = h('div', { class: 'try-list', 'aria-live': 'polite' });
	fillTryList(src, list);

	setTimeout(() => { if (textIn.isConnected && !S.tryBusy) textIn.focus(); }, 0);
	return [head, note, composeCard, list].filter(Boolean);
}

function fillTryList(src, list) {
	const log = S.tryLog.get(src.name) || [];
	if (!log.length) {
		list.replaceChildren(h('div', { class: 'try-none', text: 'Results show up here, newest first.' }));
		return;
	}
	const items = log.map((r) => {
		const msg = h('div', { class: 'try-msg' },
			h('span', { class: 'try-msg-user', text: r.user }),
			h('span', { class: 'try-msg-text', text: r.text }),
		);
		if (r.pending) {
			return h('div', { class: 'try-item' }, msg,
				h('div', { class: 'try-verdict' }, h('span', { class: 'try-spin' }), h('span', { class: 'muted', text: 'Asking the AI…' })));
		}
		if (r.error) return h('div', { class: 'try-item err' }, msg, h('div', { class: 'try-error', text: r.error }));
		return h('div', { class: 'try-item' + (r.kind === 'none' ? ' quiet' : '') + (r.delete ? ' del' : '') }, msg,
			h('div', { class: 'try-verdict' },
				h('span', { class: 'try-kind', text: tryLabel(r.kind) }),
				h('span', { class: 'try-look', text: TRY_LOOKS[r.kind] || '' }),
				h('span', { class: 'grow' }),
				r.played ? h('span', { class: 'try-tag', text: 'Played in OBS' }) : null,
				h('span', {
					class: 'try-mod' + (r.delete ? ' del' : ''),
					text: r.delete ? 'Auto mod: deletes it' : r.automod ? 'Auto mod: keeps it' : 'Auto mod: off',
					title: r.automod ? null : 'Auto mod is off, so the pet never deletes messages.',
				}),
			),
			r.delete ? h('div', { class: 'try-why', text: 'On stream it’s removed from your chat for everyone, and the pet rips it out of the overlay.' }) : null,
			r.why ? h('div', { class: 'try-why', text: `Why: ${r.why}` }) : null,
		);
	});
	list.replaceChildren(
		h('div', { class: 'try-list-head' },
			h('span', { class: 'try-label', text: 'Results' }),
			h('button', {
				type: 'button', class: 'ghost small', text: 'Clear', disabled: S.tryBusy,
				onclick: () => { log.length = 0; fillTryList(src, list); },
			}),
		),
		...items,
	);
}

async function runTry(src, user, text) {
	const log = S.tryLog.get(src.name);
	const entry = { user, text, pending: true };
	log.unshift(entry);
	if (log.length > 30) log.length = 30;
	S.tryBusy = true;
	const redraw = () => { if (S.selectedName === src.name && S.section === 'try') renderPetContent(currentSource() || src); };
	redraw();
	let res;
	try {
		res = await doAction({ action: 'try_chat', source: src.name, user, text, play: !!S.tryPlay });
	} catch (err) {
		res = { error: err.message || 'Couldn’t reach OBS.' };
	}
	entry.pending = false;
	if (!res || res.error || !res.ok) entry.error = res?.error || 'Something went wrong. Try again.';
	else Object.assign(entry, { kind: res.kind, why: res.why, delete: !!res.delete, played: !!res.played, automod: res.allow_delete !== false });
	S.tryBusy = false;
	redraw();
}

function buildPetHabitsSection(src) {
	const cfg = src.settings;

	// OBS 32+ stops the echo itself (Desktop Audio on the monitoring device gets the pet once), so
	// "Me & stream" is always safe there. "Only me" is gone in OBS 33; it stays only for a pet set to it.
	const noEcho = !!S.data.no_echo;
	const hearNotes = {
		both: noEcho
			? 'You hear it, and your stream gets it once. OBS stops any echo from your desktop audio by itself.'
			: 'Your stream, and you through OBS monitoring. If OBS also captures your desktop audio, pick Stream only to avoid an echo.',
		stream: noEcho ? 'Your stream and recordings only. You won’t hear it.' : 'Your stream and recordings only.',
		me: 'Only you, through OBS monitoring.',
		off: 'Silent.',
	};
	const hearNow = cfg.hear || 'both';
	const hearNoteEl = h('div', { class: 'info-box', text: hearNotes[hearNow] || hearNotes.both });

	const soundCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Sound' }),
			h('span', { class: 'card-desc', text: src.meta?.pack_has_sounds ? 'Volume: OBS Audio Mixer' : 'This character has no sounds' }),
		),
		h('div', { class: 'card-body' },
			makeSegRow('Who hears it', null, [
				['both', 'Me & stream'],
				['stream', 'Stream only'],
				...(!noEcho || hearNow === 'me' ? [['me', 'Only me']] : []),
				['off', 'Off'],
			], hearNow, (v) => {
				hearNoteEl.textContent = hearNotes[v] || '';
				patchSource(src.name, { hear: v }, { immediate: true });
			}),
			hearNoteEl,
		),
	);

	// Times, not multipliers. Each slider changes the other's times too (a pet that naps more has
	// less time left to roam), so moving one refreshes the other.
	const hasChat = !!src.meta?.chat_kind && src.meta.chat_kind !== 'none';
	const rhythm = (o) => petRhythm({ naps: cfg.naps ?? 1, energy: cfg.energy ?? 1, visit: cfg.visit ?? 0.5, chat: hasChat, ...o });
	const napsRow = makeSliderRow('Naps', (v) => (v <= 0
		? 'Never naps on its own. A chat reaction can still send it to sleep.'
		: `A 20–40 s nap about ${everyText(rhythm({ naps: v }).nap)}. More often when chat goes quiet, less when it’s busy.`), {
		value: Number(cfg.naps ?? 1),
		min: 0, max: 3, step: 0.1,
		format: (v) => everyText(rhythm({ naps: v }).nap),
		onChange: (v, imm) => { patchSource(src.name, { naps: v }, { immediate: imm }); energyRow.refresh(); chatRow?.refresh(); },
	});
	const energyRow = makeSliderRow('Energy', (v) => (v <= 0
		? 'Stays put. Alerts, channel points and chat still get it moving.'
		: `Walks, hops or swats about ${everyText(rhythm({ energy: v }).move)}. Zoomies about ${everyText(rhythm({ energy: v }).zoomies)}.`), {
		value: Number(cfg.energy ?? 1),
		min: 0, max: 3, step: 0.1,
		format: (v) => (v <= 0 ? 'Stays put' : everyText(rhythm({ energy: v }).move)),
		onChange: (v, imm) => { patchSource(src.name, { energy: v }, { immediate: imm }); napsRow.refresh(); chatRow?.refresh(); },
	});
	// Only with a chat in the scene: without one there's nothing to sit on.
	const chatRow = hasChat ? makeSliderRow('Sitting on chat', (v) => (v <= 0
		? 'Stays on the floor. Chat can still ask it up.'
		: `Hops onto the chat about ${everyText(rhythm({ visit: v }).visit)} and sits there for about ${spanText(rhythm({ visit: v }).stay)}.`), {
		value: Number(cfg.visit ?? 0.5),
		min: 0, max: 1, step: 0.05,
		format: (v) => (v <= 0 ? 'Never' : everyText(rhythm({ visit: v }).visit)),
		onChange: (v, imm) => { patchSource(src.name, { visit: v }, { immediate: imm }); napsRow.refresh(); energyRow.refresh(); },
	}) : null;

	const habitsCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Habits' }),
			null,
		),
		h('div', { class: 'card-body' },
			napsRow,
			energyRow,
			chatRow,
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Sound & Habits' }),
				null,
			),
		),
		soundCard,
		habitsCard,
	];
}

function updateLabelPreview() {
	const el = $('#labelPreviewText');
	const src = currentSource();
	if (!el || !src || src.kind !== 'pet') return;
	const cfg = src.settings;
	const eff = slotPetName(src, 0);
	const after = (cfg.label_after ?? ' pet {name}').replaceAll('{name}', eff);
	// like OBS: the viewer's name in their chat color (src/pet.cpp set_label), the rest in Text color
	const viewer = CHAT_SAMPLES[0];
	el.replaceChildren(h('span', { text: viewer.user, style: { color: viewer.color } }), after);
	const f = cfg.label_font || {};
	el.style.fontFamily = `"${f.face || 'Segoe UI'}", "Segoe UI", system-ui, sans-serif`;
	el.style.fontSize = clamp(Number(f.size) || 26, 12, 54) + 'px';
	el.style.fontWeight = (Number(f.flags) & 1) ? '700' : '600';
	el.style.fontStyle = (Number(f.flags) & 2) ? 'italic' : 'normal';
	el.style.color = obsColorToHex(cfg.label_color ?? 0xFFDEE4EC);
}

function buildPetLabelSection(src) {
	const cfg = src.settings;
	const previewText = h('span', { id: 'labelPreviewText', class: 'label-preview-text' });
	const afterInput = h('input', {
		type: 'text',
		value: cfg.label_after ?? ' pet {name}',
		placeholder: ' pet {name}',
		oninput: () => patchSource(src.name, { label_after: afterInput.value }),
	});

	const card = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Channel-point label' }),
			null,
		),
		h('div', { class: 'card-body' },
			h('div', { class: 'label-preview' }, previewText),
			makeRow('Text after viewer name', '{name} is the pet’s name. Blank shows just the viewer’s name.', afterInput),
			makeFontRow('Font', null, cfg.label_font, (f, imm) => {
				patchSource(src.name, { label_font: f }, { immediate: imm });
			}),
			makeColorRow('Text color', 'The viewer’s name uses their chat color.', cfg.label_color ?? 0xFFDEE4EC, {
				onChange: (v, imm) => patchSource(src.name, { label_color: v }, { immediate: imm }),
			}),
		),
	);

	queueMicrotask(updateLabelPreview);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Name Label' }),
				h('p', { class: 'sec-sub', text: 'Shown next to the pet when a viewer redeems the pet reward.' }),
			),
			h('button', {
				type: 'button', class: 'small', text: 'Try in OBS',
				onclick: () => runAction({ action: 'test', source: src.name, what: 'pet' }, { refresh: false }),
			}),
		),
		card,
	];
}

function buildPetCustomSection(src) {
	const cfg = src.settings;
	const meta = src.meta || {};
	const dirInput = h('input', {
		type: 'text',
		value: cfg.pack_dir || '',
		placeholder: 'Leave blank to use the character picked in Pets',
		oninput: () => patchSource(src.name, { pack_dir: dirInput.value }),
		onchange: () => patchSource(src.name, { pack_dir: dirInput.value.trim() }, { immediate: true, refreshAfter: true }),
	});

	const card = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Folders' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeRow('Your characters folder', 'One folder for each character you made or copied in Characters. If a name was already taken when it was made, the folder gets -2 on the end. The picker shows the character’s real name.',
				h('div', { class: 'inline' },
					h('button', {
						type: 'button', text: 'Open my characters folder',
						onclick: () => runAction({ action: 'open_packs_folder' }, { refresh: false }),
					}),
					h('a', {
						class: 'btn primary',
						href: editorUrl(''),
						text: 'Make or change characters',
					}),
				),
			),
			makeRow('Folder for Pet 1', 'A folder with a pack.lua. Replaces Pet 1’s character.',
				h('div', { class: 'inline-stretch' },
					dirInput,
					h('button', {
						type: 'button', text: 'Browse…',
						onclick: async () => {
							const r = await runAction({ action: 'pick_folder' }, { refresh: false });
							if (r && !r.cancelled && r.path) {
								dirInput.value = r.path;
								patchSource(src.name, { pack_dir: r.path }, { immediate: true, refreshAfter: true });
							}
						},
					}),
					cfg.pack_dir ? h('button', {
						type: 'button', class: 'ghost', text: 'Clear',
						onclick: () => {
							dirInput.value = '';
							patchSource(src.name, { pack_dir: '' }, { immediate: true, refreshAfter: true });
						},
					}) : null,
				), 'stack',
			),
			makeRow('Loaded character', 'Reload after editing its files by hand.',
				h('button', {
					type: 'button', text: 'Reload character',
					onclick: () => runAction({ action: 'reload_pack', source: src.name }, { okToast: 'Reloaded character' }),
				}),
			),
			h('div', { class: 'info-box ' + (meta.pack_ok ? 'ok' : 'err') },
				h('pre', { text: meta.pack_status || 'No status reported.' }),
			),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Character Folders' }),
				h('p', { class: 'sec-sub', text: 'Where your characters are kept, and a character folder from anywhere for Pet 1.' }),
			),
		),
		card,
	];
}

// ------------------------------------------------------------------ CHAT RAIL & LIVE PREVIEW

function strokeShadow(px, color, shadow) {
	const out = [];
	if (px > 0) {
		const n = Math.max(8, Math.round(px * 6));
		for (let i = 0; i < n; i++) {
			const a = (i / n) * Math.PI * 2;
			out.push(`${(Math.cos(a) * px).toFixed(2)}px ${(Math.sin(a) * px).toFixed(2)}px 0 ${color}`);
		}
	}
	if (shadow) out.push('0 2px 3px rgba(0,0,0,.85)', '0 0 10px rgba(0,0,0,.55)');
	return out.length ? out.join(', ') : 'none';
}

function rgbToHsl(r, g, b) {
	r /= 255; g /= 255; b /= 255;
	const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, d = mx - mn;
	let h0 = 0, s = 0;
	if (d) {
		s = d / (1 - Math.abs(2 * l - 1));
		h0 = (mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4) * 60;
	}
	return [(h0 + 360) % 360, s, l];
}

function readableNameColor(hex, cfg) {
	if (cfg.name_colors === 'one' || !hex) return obsColorToHex(cfg.name_color ?? 0xFFDBC1A6);
	if (cfg.readable === false) return hex;
	const n = parseInt(hex.slice(1), 16);
	const [hh, ss, ll] = rgbToHsl((n >> 16) & 255, (n >> 8) & 255, n & 255);
	const tp = obsColorToParts(cfg.text_color ?? 0xFFDEE4EC);
	const light = (0.2126 * tp.r + 0.7152 * tp.g + 0.0722 * tp.b) / 255 > 0.5;
	const L = light ? Math.max(ll, 0.62) : Math.min(ll, 0.4);
	return `hsl(${Math.round(hh)} ${Math.round(ss * 100)}% ${Math.round(L * 100)}%)`;
}

function updateChatPreview(animate = false) {
	const root = $('#chatPrevRoot');
	const src = currentSource();
	if (!root || !src || src.kind !== 'chat') return;
	const cfg = src.settings;

	root.className = [
		'chat-prev-root',
		cfg.order === 'down' ? 'down' : 'up',
		cfg.align === 'right' ? 'right' : 'left',
		'bg-' + (cfg.bg || 'none'),
		cfg.roles !== false ? 'marks' : '',
		cfg.name_line ? 'name-line' : '',
	].filter(Boolean).join(' ');

	const font = cfg.font || {};
	const scaleSize = clamp(Math.round((Number(font.size) || 24) * 0.62), 11, 24);
	const mp = obsColorToParts(cfg.mention_color ?? 0xFF3CB4F0);
	const stroke = strokeShadow(Number(cfg.outline) || 0, obsColorToHex(cfg.outline_color ?? 0xFF050303), cfg.shadow !== false);

	root.style.setProperty('--font', `"${(font.face || 'Segoe UI').replace(/["\\]/g, '')}", "Segoe UI", system-ui, sans-serif`);
	root.style.setProperty('--size', `${scaleSize}px`);
	root.style.setProperty('--weight', (Number(font.flags) & 1) ? '700' : '600');
	root.style.setProperty('--style', (Number(font.flags) & 2) ? 'italic' : 'normal');
	root.style.setProperty('--text', obsColorToHex(cfg.text_color ?? 0xFFDEE4EC));
	root.style.setProperty('--name', obsColorToHex(cfg.name_color ?? 0xFFDBC1A6));
	root.style.setProperty('--stroke', stroke);
	root.style.setProperty('--bg', obsColorToRgba(cfg.bg_color ?? 0xB0140F0F, true));
	root.style.setProperty('--radius', `${Number(cfg.radius ?? 8)}px`);
	root.style.setProperty('--pad', `${Math.round((Number(cfg.padding ?? 8)) * 0.8)}px`);
	root.style.setProperty('--gap', `${Math.round((Number(cfg.gap ?? 6)) * 0.8)}px`);
	root.style.setProperty('--emote', `${(1.45 * (Number(cfg.emote_scale) || 100) / 100).toFixed(2)}em`);
	root.style.setProperty('--role', obsColorToHex(cfg.role_color ?? 0xFF3C48E0));
	root.style.setProperty('--mention', obsColorToHex(cfg.mention_color ?? 0xFF3CB4F0));
	root.style.setProperty('--mention-soft', `rgba(${mp.r},${mp.g},${mp.b},0.24)`);

	const list = $('#list', root);
	list.replaceChildren();
	const rows = cfg.order === 'down' ? [...CHAT_SAMPLES].reverse() : CHAT_SAMPLES;
	rows.forEach((m, idx) => {
		const cls = ['msg'];
		if (m.role && cfg.roles !== false) cls.push('role');
		if (m.mention && cfg.mentions !== false) cls.push('mention');
		if (m.highlight && cfg.highlights !== false) cls.push('highlight');
		if (m.big && cfg.big_emotes !== false) cls.push('big');

		const msgEl = h('div', { class: cls.join(' ') });
		if (animate && cfg.anim && cfg.anim !== 'none') {
			const kf = cfg.anim === 'slide' ? (cfg.order === 'down' ? 'cp-slide-down' : 'cp-slide')
				: cfg.anim === 'fade' ? 'cp-fade' : 'cp-pop';
			msgEl.style.animation = `${kf} .32s ease-out ${(idx * 0.06).toFixed(2)}s both`;
		}

		if (m.reply && cfg.replies !== false) {
			msgEl.append(h('span', { class: 'reply-to', text: m.reply }));
		}
		const head = h('span', { class: 'head' });
		if (m.first && cfg.first !== false) {
			head.append(h('span', { class: 'tag', text: 'First' }));
		}
		if (cfg.badges !== false && m.badges) {
			for (const b of m.badges) {
				head.append(h('span', { class: 'badge-dot', style: { background: b.bg }, text: b.t }));
			}
		}
		if (cfg.pronouns && m.pronouns) {
			head.append(h('span', { class: 'pronouns', text: m.pronouns }));
		}
		head.append(h('span', { class: 'name', style: { color: readableNameColor(m.color, cfg) }, text: m.user }));
		if (cfg.colon) head.append(h('span', { class: 'colon', text: ':' }));

		const text = h('span', { class: 'text' });
		for (const p of m.parts) {
			if (p.t === 'emote') text.append(h('span', { class: 'emote-demo', text: p.v }));
			else text.append(document.createTextNode(p.v));
		}

		msgEl.append(head, text);
		list.append(msgEl);
	});
}

function renderChatRail(src) {
	const rail = $('#rail');
	const cfg = src.settings;

	const prevBox = h('div', { class: 'chat-preview-box bg-' + S.chatBg },
		h('div', { class: 'chat-preview-scale' },
			h('div', { id: 'chatPrevRoot', class: 'chat-prev-root' },
				h('div', { id: 'list' }),
			),
		),
	);

	const bgs = [['stream', 'Stream'], ['dark', 'Dark'], ['checker', 'Grid'], ['light', 'Light']];
	const bgBtn = h('button', {
		type: 'button', class: 'small ghost',
		text: 'BG: ' + (bgs.find(([k]) => k === S.chatBg)?.[1] || 'Stream'),
		onclick: () => {
			const i = (bgs.findIndex(([k]) => k === S.chatBg) + 1) % bgs.length;
			S.chatBg = bgs[i][0];
			prevBox.className = 'chat-preview-box bg-' + S.chatBg;
			bgBtn.textContent = 'BG: ' + bgs[i][1];
		},
	});

	const animBtn = h('button', {
		type: 'button', class: 'small ghost', text: '▶ Animate',
		onclick: () => updateChatPreview(true),
	});

	const testObsBtn = h('button', {
		type: 'button', class: 'small' + (cfg.test ? ' on' : ''),
		text: cfg.test ? 'Test in OBS: On' : 'Test in OBS',
		onclick: () => {
			const next = !cfg.test;
			patchSource(src.name, { test: next }, { immediate: true });
			renderChatRail(src);
			if (S.section === 'channels') renderChatContent(src);
		},
	});

	const stageCard = h('div', { class: 'stage-card' },
		prevBox,
		h('div', { class: 'stage-foot' },
			h('div', { class: 'stage-meta' },
				h('div', { class: 'stage-name', text: src.name }),
				h('div', { class: 'stage-sub', text: src.meta?.status || 'Live preview' }),
			),
			bgBtn,
			animBtn,
		),
		h('div', { class: 'stage-foot', style: { justifyContent: 'space-between' } },
			h('span', { class: 'muted small num', text: `${cfg.width || 450} × ${cfg.height || 700} px` }),
			testObsBtn,
		),
	);

	const acct = S.data.account;
	const nav = h('nav', { class: 'rail-nav' }, ...CHAT_SECTIONS.map((sec) => {
		let right = null;
		if (sec.id === 'account') {
			if (acct.twitch_missing || acct.kick_missing) right = h('span', { class: 'nav-dot warn' });
			else if (acct.signed_in) right = h('span', { class: 'nav-dot' });
		}
		return h('button', {
			type: 'button',
			class: S.section === sec.id ? 'on' : '',
			onclick: () => { S.section = sec.id; renderChatRail(src); renderChatContent(src); },
		}, h('span', { text: sec.label }), right);
	}));

	rail.replaceChildren(stageCard, nav);
	updateChatPreview();
}

// ------------------------------------------------------------------ CHAT SECTIONS

function renderChatContent(src) {
	const box = $('#content');
	box.replaceChildren();
	switch (S.section) {
		case 'channels': box.append(...buildChatChannelsSection(src)); break;
		case 'text': box.append(...buildChatTextSection(src)); break;
		case 'background': box.append(...buildChatBgSection(src)); break;
		case 'emotes': box.append(...buildChatEmotesSection(src)); break;
		case 'filters': box.append(...buildChatFiltersSection(src)); break;
		case 'account': box.append(...buildAccountSection(src)); break;
	}
	box.append(buildResetFooter(src));
}

function buildChatChannelsSection(src) {
	const cfg = src.settings;
	const acct = S.data.account;

	const twIn = h('input', {
		type: 'text',
		value: cfg.channel || '',
		placeholder: acct.login ? `Signed in as ${acct.login}` : 'channel name or twitch.tv/…',
		oninput: () => patchSource(src.name, { channel: twIn.value }),
	});
	const kkIn = h('input', {
		type: 'text',
		value: cfg.kick || '',
		placeholder: acct.kick_login ? `Signed in as ${acct.kick_login}` : 'channel name or kick.com/…',
		oninput: () => patchSource(src.name, { kick: kkIn.value }),
	});
	const wIn = h('input', {
		type: 'number', min: 50, max: 8192, value: Number(cfg.width) || 450,
		onchange: () => {
			const v = clamp(Number(wIn.value) || 450, 50, 8192);
			wIn.value = v;
			patchSource(src.name, { width: v }, { immediate: true });
			renderChatRail(src);
		},
	});
	const hIn = h('input', {
		type: 'number', min: 50, max: 8192, value: Number(cfg.height) || 700,
		onchange: () => {
			const v = clamp(Number(hIn.value) || 700, 50, 8192);
			hIn.value = v;
			patchSource(src.name, { height: v }, { immediate: true });
			renderChatRail(src);
		},
	});

	const chCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Channels' }),
			h('span', { class: 'card-desc', text: src.meta?.status || 'Leave blank to use your signed-in channel' }),
		),
		h('div', { class: 'card-body' },
			makeRow('Twitch channel', 'Blank follows the account you signed in with.', twIn),
			makeRow('Kick channel', 'Blank follows your signed-in Kick account.', kkIn),
			makeToggleRow('Show test messages in OBS', 'Sample lines in OBS, to position and size it.',
				!!cfg.test, (v) => { patchSource(src.name, { test: v }, { immediate: true }); renderChatRail(src); }),
			makeToggleRow('Load recent Twitch history', 'Shows the last few messages when OBS starts.',
				!!cfg.history, (v) => patchSource(src.name, { history: v }, { immediate: true })),
		),
	);

	const layoutCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Box & Flow' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeRow('Size (px)', null,
				h('div', { class: 'inline-stretch' }, wIn, h('span', { class: 'muted', text: '×' }), hIn),
			),
			makeSegRow('Direction', null, [
				['up', 'Bottom up'],
				['down', 'Top down'],
			], cfg.order || 'up', (v) => patchSource(src.name, { order: v }, { immediate: true })),
			makeSegRow('Alignment', null, [
				['left', 'Left'],
				['right', 'Right'],
			], cfg.align || 'left', (v) => patchSource(src.name, { align: v }, { immediate: true })),
			makeSegRow('Animation', null, [
				['slide', 'Slide'],
				['fade', 'Fade'],
				['pop', 'Pop'],
				['none', 'None'],
			], cfg.anim || 'slide', (v) => { patchSource(src.name, { anim: v }, { immediate: true }); updateChatPreview(true); }),
			makeSliderRow('Max messages', 'The oldest go when there are more.', {
				value: Number(cfg.max ?? 30), min: 1, max: 200, step: 1,
				format: (v) => String(v),
				parse: (t) => (/^\d+$/.test(t) ? Number(t) : NaN),
				onChange: (v, imm) => patchSource(src.name, { max: v }, { immediate: imm }),
			}),
			makeSliderRow('Fade out after', null, {
				value: Number(cfg.fade ?? 30), min: 0, max: 600, step: 1,
				format: (v) => (v === 0 ? 'Never' : v < 60 ? `${v} s` : `${Math.floor(v / 60)} min${v % 60 ? ` ${v % 60} s` : ''}`),
				parse: parseSeconds,
				onChange: (v, imm) => patchSource(src.name, { fade: v }, { immediate: imm }),
			}),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Channels & Layout' }),
				null,
			),
		),
		chCard,
		layoutCard,
	];
}

function buildChatTextSection(src) {
	const cfg = src.settings;

	const textCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Typography' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeFontRow('Font', null, cfg.font, (f, imm) => {
				patchSource(src.name, { font: f }, { immediate: imm });
			}),
			makeColorRow('Message color', null, cfg.text_color ?? 0xFFDEE4EC, {
				onChange: (v, imm) => patchSource(src.name, { text_color: v }, { immediate: imm }),
			}),
			makeSliderRow('Outline', null, {
				value: Number(cfg.outline ?? 3), min: 0, max: 8, step: 1,
				format: (v) => (v === 0 ? 'Off' : `${v} px`),
				onChange: (v, imm) => patchSource(src.name, { outline: v }, { immediate: imm }),
			}),
			makeColorRow('Outline color', null, cfg.outline_color ?? 0xFF050303, {
				onChange: (v, imm) => patchSource(src.name, { outline_color: v }, { immediate: imm }),
			}),
			makeToggleRow('Drop shadow', null,
				cfg.shadow !== false, (v) => patchSource(src.name, { shadow: v }, { immediate: true })),
		),
	);

	const namesCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Names & Badges' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeSegRow('Name colors', null, [
				['twitch', 'Chatter colors'],
				['one', 'One color'],
			], cfg.name_colors || 'twitch', (v) => patchSource(src.name, { name_colors: v }, { immediate: true })),
			makeColorRow('Name color', 'For “One color”, and chatters without a color.', cfg.name_color ?? 0xFFDBC1A6, {
				onChange: (v, imm) => patchSource(src.name, { name_color: v }, { immediate: imm }),
			}),
			makeToggleRow('Keep dark names readable', 'Brightens chatter colors too dark to read.',
				cfg.readable !== false, (v) => patchSource(src.name, { readable: v }, { immediate: true })),
			makeToggleRow('Badges', null,
				cfg.badges !== false, (v) => patchSource(src.name, { badges: v }, { immediate: true })),
			makeToggleRow('Pronouns', 'From alejo.io, on Twitch.',
				!!cfg.pronouns, (v) => patchSource(src.name, { pronouns: v }, { immediate: true })),
			makeToggleRow('Colon after name', null,
				!!cfg.colon, (v) => patchSource(src.name, { colon: v }, { immediate: true })),
			makeToggleRow('Name on its own line', null,
				!!cfg.name_line, (v) => patchSource(src.name, { name_line: v }, { immediate: true })),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Text & Names' }),
				null,
			),
		),
		textCard,
		namesCard,
	];
}

function buildChatBgSection(src) {
	const cfg = src.settings;

	const card = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Background & Spacing' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeSegRow('Style', null, [
				['none', 'None'],
				['box', 'Per message'],
				['panel', 'Whole panel'],
			], cfg.bg || 'none', (v) => patchSource(src.name, { bg: v }, { immediate: true })),
			makeColorRow('Color & opacity', null, cfg.bg_color ?? 0xB0140F0F, {
				withAlpha: true,
				onChange: (v, imm) => patchSource(src.name, { bg_color: v }, { immediate: imm }),
			}),
			makeSliderRow('Corner radius', null, {
				value: Number(cfg.radius ?? 8), min: 0, max: 32, step: 1,
				format: (v) => `${v} px`,
				onChange: (v, imm) => patchSource(src.name, { radius: v }, { immediate: imm }),
			}),
			makeSliderRow('Padding', null, {
				value: Number(cfg.padding ?? 8), min: 0, max: 32, step: 1,
				format: (v) => `${v} px`,
				onChange: (v, imm) => patchSource(src.name, { padding: v }, { immediate: imm }),
			}),
			makeSliderRow('Message gap', null, {
				value: Number(cfg.gap ?? 6), min: 0, max: 32, step: 1,
				format: (v) => `${v} px`,
				onChange: (v, imm) => patchSource(src.name, { gap: v }, { immediate: imm }),
			}),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Background' }),
				null,
			),
		),
		card,
	];
}

function buildChatEmotesSection(src) {
	const cfg = src.settings;

	const emotesCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Emotes' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeToggleRow('7TV emotes', null,
				cfg.emotes_7tv !== false, (v) => patchSource(src.name, { emotes_7tv: v }, { immediate: true })),
			makeToggleRow('BetterTTV emotes', null,
				cfg.emotes_bttv !== false, (v) => patchSource(src.name, { emotes_bttv: v }, { immediate: true })),
			makeToggleRow('FrankerFaceZ emotes', null,
				cfg.emotes_ffz !== false, (v) => patchSource(src.name, { emotes_ffz: v }, { immediate: true })),
			makeSliderRow('Emote size', 'Relative to the font size.', {
				value: Number(cfg.emote_scale ?? 100), min: 50, max: 200, step: 5,
				format: (v) => `${v}%`,
				onChange: (v, imm) => patchSource(src.name, { emote_scale: v }, { immediate: imm }),
			}),
			makeToggleRow('Bigger emote-only messages', null,
				cfg.big_emotes !== false, (v) => patchSource(src.name, { big_emotes: v }, { immediate: true })),
		),
	);

	const hlCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Highlights & Accents' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeToggleRow('Role stripe', 'Beside broadcaster, mod and VIP messages.',
				cfg.roles !== false, (v) => patchSource(src.name, { roles: v }, { immediate: true })),
			makeColorRow('Role stripe color', null, cfg.role_color ?? 0xFF3C48E0, {
				onChange: (v, imm) => patchSource(src.name, { role_color: v }, { immediate: imm }),
			}),
			makeToggleRow('Highlight mentions', 'Messages that @mention you.',
				cfg.mentions !== false, (v) => patchSource(src.name, { mentions: v }, { immediate: true })),
			makeColorRow('Mention color', 'Also used for the First tag.', cfg.mention_color ?? 0xFF3CB4F0, {
				onChange: (v, imm) => patchSource(src.name, { mention_color: v }, { immediate: imm }),
			}),
			makeToggleRow('Highlighted messages', 'Twitch’s “Highlight my message” reward.',
				cfg.highlights !== false, (v) => patchSource(src.name, { highlights: v }, { immediate: true })),
			makeToggleRow('First-time chatter tag', null,
				cfg.first !== false, (v) => patchSource(src.name, { first: v }, { immediate: true })),
			makeToggleRow('Show what replies reply to', null,
				cfg.replies !== false, (v) => patchSource(src.name, { replies: v }, { immediate: true })),
			makeToggleRow('Color /me messages', 'In the chatter’s name color.',
				cfg.me_color !== false, (v) => patchSource(src.name, { me_color: v }, { immediate: true })),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Emotes & Highlights' }),
				null,
			),
		),
		emotesCard,
		hlCard,
	];
}

function buildChatFiltersSection(src) {
	const cfg = src.settings;

	const usersIn = h('textarea', {
		rows: 2,
		value: cfg.hide_users ?? S.data.default_bots ?? '',
		placeholder: 'nightbot, streamelements, fossabot…',
		oninput: () => patchSource(src.name, { hide_users: usersIn.value }),
	});

	const wordsIn = h('textarea', {
		rows: 2,
		value: cfg.words || '',
		placeholder: 'word1, word2, phrase…',
		oninput: () => patchSource(src.name, { words: wordsIn.value }),
	});

	const filtersCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Moderation & Noise Filters' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeRow('Hide these users', 'Comma-separated, usually bots.',
				h('div', { style: { width: '100%' } },
					usersIn,
					h('div', { class: 'inline', style: { marginTop: '6px', justifyContent: 'flex-end' } },
						h('button', {
							type: 'button', class: 'small ghost', text: 'Restore default bots',
							onclick: () => {
								usersIn.value = S.data.default_bots || '';
								patchSource(src.name, { hide_users: usersIn.value }, { immediate: true });
							},
						}),
					),
				), 'stack',
			),
			makeToggleRow('Hide !commands', null,
				cfg.hide_commands !== false, (v) => patchSource(src.name, { hide_commands: v }, { immediate: true })),
			makeToggleRow('Hide messages with links', null,
				!!cfg.hide_links, (v) => patchSource(src.name, { hide_links: v }, { immediate: true })),
			makeRow('Blocked words', 'Comma-separated.', wordsIn, 'stack'),
			makeSegRow('When a blocked word appears', null, [
				['hide', 'Hide message'],
				['censor', 'Censor word'],
			], cfg.words_mode || 'hide', (v) => patchSource(src.name, { words_mode: v }, { immediate: true })),
		),
	);

	const cssIn = h('textarea', {
		class: 'css-editor',
		value: cfg.css || '',
		spellcheck: false,
		placeholder: '/* Example:\n.msg { letter-spacing: 0.01em; }\n.name { text-transform: uppercase; }\n*/',
		oninput: () => patchSource(src.name, { css: cssIn.value }),
	});

	const chips = h('div', { class: 'class-ref' }, ...CSS_CLASSES.map((sel) => h('code', {
		text: sel,
		title: 'Click to insert selector',
		onclick: () => {
			const ins = `${sel} {  }\n`;
			const pos = cssIn.selectionStart ?? cssIn.value.length;
			cssIn.value = cssIn.value.slice(0, pos) + ins + cssIn.value.slice(pos);
			cssIn.focus();
			cssIn.setSelectionRange(pos + sel.length + 3, pos + sel.length + 3);
			patchSource(src.name, { css: cssIn.value });
		},
	})));

	const cssCard = h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Custom CSS' }),
			null,
		),
		h('div', { class: 'card-body' },
			makeRow('Stylesheet', 'Click a class below to insert it.',
				h('div', { style: { width: '100%' } }, cssIn, chips), 'stack',
			),
		),
	);

	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Filters & CSS' }),
				null,
			),
		),
		filtersCard,
		cssCard,
	];
}

// ------------------------------------------------------------------ ACCOUNT SECTION (SHARED)

function buildAccountCard(src) {
	const acct = S.data.account;
	const twConnected = !!acct.login;
	const kkConnected = !!acct.kick_login;

	const twCard = h('div', { class: 'platform-card' + (twConnected ? ' connected' : '') },
		h('div', { class: 'platform-top' },
			h('div', { class: 'platform-name' }, h('span', { class: 'pdot twitch' }), 'Twitch'),
			h('span', { class: 'pstatus' + (acct.twitch_live ? ' live' : ''), text: twConnected ? (acct.twitch_live ? 'Linked' : 'Connecting…') : 'Not linked' }),
		),
		h('div', { class: 'platform-user' }, twConnected ? `@${acct.login}` : h('span', { class: 'muted', text: 'Alerts, channel points, badges & chat reactions.' })),
		acct.twitch_missing ? h('div', { class: 'small', style: { color: 'var(--warn)' }, text: acct.twitch_missing }) : null,
		h('div', { class: 'inline' },
			h('button', {
				type: 'button',
				class: twConnected ? (acct.twitch_missing ? 'primary small' : 'small') : 'primary small',
				disabled: acct.linking,
				text: twConnected ? (acct.twitch_missing ? 'Re-authorize Twitch' : 'Reconnect Twitch') : 'Sign in with Twitch',
				onclick: () => runAction({ action: 'sign_in', platform: 'twitch' }),
			}),
		),
	);

	const kkCard = h('div', { class: 'platform-card' + (kkConnected ? ' connected' : '') },
		h('div', { class: 'platform-top' },
			h('div', { class: 'platform-name' }, h('span', { class: 'pdot kick' }), 'Kick'),
			h('span', { class: 'pstatus' + (acct.kick_live ? ' live' : ''), text: kkConnected ? (acct.kick_live ? 'Linked' : 'Connecting…') : 'Not linked' }),
		),
		h('div', { class: 'platform-user' }, kkConnected ? `@${acct.kick_login}` : h('span', { class: 'muted', text: 'Alerts, Kicks, channel rewards & chat reactions.' })),
		acct.kick_missing ? h('div', { class: 'small', style: { color: 'var(--warn)' }, text: acct.kick_missing }) : null,
		h('div', { class: 'inline' },
			h('button', {
				type: 'button',
				class: kkConnected ? (acct.kick_missing ? 'primary small' : 'small') : 'primary small',
				disabled: acct.linking,
				text: kkConnected ? (acct.kick_missing ? 'Re-authorize Kick' : 'Reconnect Kick') : 'Sign in with Kick',
				onclick: () => runAction({ action: 'sign_in', platform: 'kick' }),
			}),
		),
	);

	const kids = [h('div', { class: 'platform-grid' }, twCard, kkCard)];

	if (acct.message && !acct.linking) {
		kids.push(h('div', { class: 'info-box', style: { marginTop: '12px' }, text: acct.message }));
	}

	if (acct.signed_in && src?.kind === 'pet') {
		const needsMake = acct.reward === 'missing' || acct.kick_reward === 'missing';
		const lines = [];
		if (acct.login) lines.push(`Twitch reward “${acct.reward_title || src.meta?.reward_now || 'Pet'}”: ${rewardWords(acct.reward, acct.reward_msg)}`);
		if (acct.kick_login) lines.push(`Kick: ${rewardWords(acct.kick_reward, acct.kick_reward_msg)}`);
		if (lines.length) {
			kids.push(h('div', { class: 'info-box inline', style: { marginTop: '12px' } },
				h('span', { class: 'grow', text: lines.join('  ·  ') }),
				needsMake ? h('button', {
					type: 'button', class: 'small primary', text: 'Create reward',
					onclick: () => runAction({ action: 'make_reward', source: src.name }, { okToast: 'Creating reward…' }),
				}) : null,
				h('button', {
					type: 'button', class: 'small ghost', text: 'Check again',
					title: 'Asks Twitch and Kick again whether the reward is there, say after you renamed or deleted it there.',
					onclick: () => runAction({ action: 'check_reward', source: src.name }),
				}),
			));
		}
	}

	const footActions = h('div', { class: 'inline', style: { marginTop: '14px', justifyContent: 'space-between' } },
		S.data.backend ? h('a', {
			class: 'btn small ghost',
			href: S.data.backend + '/streamtools/obspets',
			target: '_blank', rel: 'noopener',
			text: 'Manage on sindruid.com ↗',
		}) : h('span'),
		acct.signed_in ? h('button', {
			type: 'button', class: 'small danger', text: 'Sign out',
			onclick: () => confirmModal('Sign out of OBSPETS?', 'This disconnects Twitch and Kick in OBS. Your pet settings and custom characters are kept.', 'Sign out', () => {
				runAction({ action: 'sign_out' }, { okToast: 'Signed out' });
			}),
		}) : null,
	);
	kids.push(footActions);

	return h('div', { class: 'card' },
		h('div', { class: 'card-hd' },
			h('h3', { text: 'Connected accounts' }),
			h('span', { class: 'card-desc', text: `OBSPETS v${S.data.version || ''}` }),
		),
		h('div', { class: 'card-pad' }, ...kids),
	);
}

function buildAccountSection(src) {
	return [
		h('div', { class: 'sec-head' },
			h('div', {},
				h('h2', { class: 'sec-title', text: 'Account' }),
				h('p', { class: 'sec-sub', text: 'One sign-in for every pet and chat source.' }),
			),
		),
		buildAccountCard(src),
	];
}

// ------------------------------------------------------------------ reset footer & modal

function buildResetFooter(src) {
	return h('div', { class: 'danger-zone' },
		h('div', {},
			h('div', { class: 'row-label', text: `Reset “${src.name}”` }),
			h('div', { class: 'row-hint', text: src.kind === 'pet' ? 'Your account and characters aren’t affected.' : 'Your account isn’t affected.' }),
		),
		h('button', {
			type: 'button', class: 'danger small', text: 'Reset to defaults',
			onclick: () => confirmModal(
				`Reset “${src.name}” to defaults?`,
				'All customized settings on this source will return to their default values.',
				'Reset defaults',
				() => runAction({ action: 'reset_defaults', source: src.name }, { okToast: 'Reset to defaults' }),
			),
		}),
	);
}

function confirmModal(title, sub, okText, onOk) {
	const modal = $('#modal');
	const dlg = $('#dialog');
	S.galleryCanvases = [];
	dlg.replaceChildren(
		h('h2', { text: title }),
		h('p', { class: 'sub', text: sub }),
		h('div', { class: 'actions' },
			h('button', { type: 'button', text: 'Cancel', onclick: closeModal }),
			h('button', {
				type: 'button', class: 'primary', text: okText,
				onclick: () => { closeModal(); onOk(); },
			}),
		),
	);
	modal.hidden = false;
}

function closeModal() {
	$('#modal').hidden = true;
	S.galleryCanvases = [];
}

// ------------------------------------------------------------------ animation loop

function frameLoop(ms) {
	if (S.view !== 'sources') return requestAnimationFrame(frameLoop);
	const t = ms / 1000;

	// Main pet stage
	if (S.stageCanvas && S.data) {
		const src = currentSource();
		if (src?.kind === 'pet') {
			const c = S.stageCanvas;
			const ctx = c.getContext('2d');
			ctx.clearRect(0, 0, c.width, c.height);
			// subtle floor line
			ctx.fillStyle = 'rgba(255,255,255,0.06)';
			ctx.fillRect(0, c.height - 20, c.width, 1);

			const nPets = clamp(Number(src.settings.pets) || 1, 1, MAX_PETS);
			const animName = STAGE_ANIMS[S.stageAnimIdx] || 'idle';
			const slotW = c.width / nPets;
			for (let i = 0; i < nPets; i++) {
				const id = slotPackId(src, i);
				const cx = slotW * (i + 0.5);
				// soft shadow
				ctx.fillStyle = 'rgba(0,0,0,0.45)';
				ctx.beginPath();
				ctx.ellipse(cx, c.height - 20, Math.min(28, slotW * 0.22), 5, 0, 0, Math.PI * 2);
				ctx.fill();
				drawPackSprite(ctx, id, cx, c.height - 20, Math.min(132, slotW - 12), c.height - 34, t + i * 0.37, animName);
			}
		}
	}

	// Pet slot thumbnails
	for (const item of S.thumbCanvases) {
		const c = item.canvas;
		const ctx = c.getContext('2d');
		ctx.clearRect(0, 0, c.width, c.height);
		drawPackSprite(ctx, item.packId, c.width / 2, c.height - 6, c.width - 8, c.height - 10, t, 'idle');
	}

	// Character gallery thumbnails
	for (const item of S.galleryCanvases) {
		const c = item.canvas;
		const ctx = c.getContext('2d');
		ctx.clearRect(0, 0, c.width, c.height);
		drawPackSprite(ctx, item.packId, c.width / 2, c.height - 8, c.width - 12, c.height - 14, t, 'idle');
	}

	requestAnimationFrame(frameLoop);
}

// ------------------------------------------------------------------ boot

async function loadFonts() {
	try {
		const res = await api('fonts');
		S.fonts = res.fonts || [];
	} catch { /* optional */ }
}

function wireEvents() {
	$('#modal').addEventListener('click', (e) => {
		if (e.target === $('#modal')) closeModal();
	});
	document.addEventListener('keydown', (e) => {
		if (e.key === 'Escape' && !$('#modal').hidden) closeModal();
	});
	window.addEventListener('focus', () => {
		if (S.data && S.view === 'sources') refreshState({ preserveFocus: true }).catch(() => {});
	});
	window.addEventListener('hashchange', route);
	for (const b of document.querySelectorAll('#viewSwitch button')) b.addEventListener('click', () => goView(b.dataset.view));
}

// The Update card (/update/update.js): send edits still waiting before OBS closes for an update
// (and the character editor's, once it's loaded)
window.OBSPET_UPDATE_HOOKS = {
	async beforeUpdate() {
		for (const name of [...S.pendingPatches.keys()]) await flushPatch(name);
		if ($('#saved').classList.contains('err')) throw new Error("a setting didn't save");
		await window.OBSPET_EDITOR?.beforeUpdate();
	},
	canReload: () => S.pendingPatches.size === 0 && (window.OBSPET_EDITOR?.canReload() ?? true),
};

async function start() {
	wireEvents();
	requestAnimationFrame(frameLoop);
	if (KEY) route();

	if (!KEY) {
		$('#viewSwitch').hidden = true;
		$('#app').hidden = true;
		$('#emptyState').hidden = false;
		$('#emptyState').replaceChildren(h('div', { class: 'empty-box' },
			h('h2', { text: 'Open Settings from OBS' }),
			h('p', { text: 'Open your OBSPETS or OBSPETS Chat source properties in OBS and click “Open settings” (or Tools → OBSPETS: Settings).' }),
		));
		return;
	}

	try {
		await Promise.all([refreshState({ check: true }), loadFonts()]);
	} catch (err) {
		$('#app').hidden = true;
		$('#emptyState').hidden = false;
		$('#emptyState').replaceChildren(h('div', { class: 'empty-box' },
			h('h2', { text: "Couldn't reach OBS" }),
			h('p', { text: `Make sure OBS Studio is still running. (${err.message})` }),
			h('div', { class: 'empty-actions' },
				h('button', { type: 'button', class: 'primary', text: 'Retry', onclick: () => location.reload() }),
			),
		));
	}
}

start();
