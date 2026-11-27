// A stand-in for the plugin's settings & editor APIs, to work on data/settings/ without OBS:
//
//   node tools/settings-dev.mjs
//   -> http://127.0.0.1:8799/settings/?k=dev
//
// (Only the Sources view: its editor API is the pack and sheet the previews draw. For the
// Characters view, use tools/editor-dev.mjs.)
//
import { createServer } from 'node:http';
import { fakeUpdater } from './fake-updater.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// the plugin's REACT_NEEDS (src/pack.cpp): reaction -> the animations it needs
const REACT_NEEDS = { curious: 'look', judge: 'look', play: 'paw', swat_chat: 'paw', yank: 'paw', zoomies: 'run',
	sleepy: 'sleep', beg: 'beg', startled: 'scared', scared: 'scared', angry: 'angry' };
const ANIM_LABEL = { look: 'Look', paw: 'Swat', run: 'Run', sleep: 'Sleep', beg: 'Beg', scared: 'Scared', angry: 'Angry' };
const cantOf = (named) => Object.fromEntries(Object.entries(REACT_NEEDS).filter(([, a]) => !named.has(a)).map(([r, a]) => [r, ANIM_LABEL[a]]));

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const SETTINGS_DIR = path.join(root, 'data', 'settings');
const EDITOR_DIR = path.join(root, 'data', 'editor');
const PACK_DIRS = [path.join(root, 'data', 'packs'), path.resolve(root, '..', '..', 'sources', 'pet', 'packs')];
const PORT = 8799, KEY = 'dev';

function parseLua(src) {
	let i = 0;
	const ws = () => {
		for (;;) {
			while (i < src.length && /\s/.test(src[i])) i++;
			if (src.startsWith('--[[', i)) i = src.indexOf(']]', i) + 2;
			else if (src.startsWith('--', i)) while (i < src.length && src[i] !== '\n') i++;
			else return;
		}
	};
	const str = () => {
		const q = src[i++];
		let o = '';
		while (src[i] !== q) {
			if (src[i] === '\\') {
				const c = src[++i];
				o += c === 'n' ? '\n' : c === 't' ? '\t' : c;
				i++;
			} else o += src[i++];
		}
		i++;
		return o;
	};
	const prim = () => {
		ws();
		const c = src[i];
		if (c === '"' || c === "'") return str();
		if (c === '{') return table();
		if (c === '-' || /[0-9.]/.test(c)) {
			const m = /^-?[0-9.]+(e-?[0-9]+)?/.exec(src.slice(i));
			i += m[0].length;
			return +m[0];
		}
		const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(src.slice(i));
		if (!m) throw new Error('lua: unexpected ' + src.slice(i, i + 20));
		i += m[0].length;
		return m[0] === 'true' ? true : m[0] === 'false' ? false : null;
	};
	const exp = () => {
		let v = prim();
		for (;;) {
			ws();
			if (src.startsWith('..', i)) { i += 2; v = String(v) + String(prim()); }
			else return v;
		}
	};
	const table = () => {
		i++;
		const t = { __arr: [] };
		for (;;) {
			ws();
			if (src[i] === '}') { i++; return t; }
			if (src[i] === '[') {
				i++;
				const k = exp();
				ws(); i++; ws(); i++;
				t[k] = exp();
			} else {
				const m = /^([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)/.exec(src.slice(i));
				if (m) { i += m[0].length; t[m[1]] = exp(); }
				else t.__arr.push(exp());
			}
			ws();
			if (src[i] === ',' || src[i] === ';') i++;
		}
	};
	ws();
	if (!src.startsWith('return', i)) throw new Error('bad pack.lua');
	i += 6;
	return exp();
}

function listPacks() {
	const out = [], seen = new Set();
	for (const dir of PACK_DIRS) {
		if (!fs.existsSync(dir)) continue;
		for (const n of fs.readdirSync(dir)) {
			if (n.startsWith('.') || n.startsWith('_') || seen.has(n)) continue;
			const p = path.join(dir, n);
			if (fs.statSync(p).isDirectory() && fs.existsSync(path.join(p, 'pack.lua'))) {
				seen.add(n);
				try {
					const T = parseLua(fs.readFileSync(path.join(p, 'pack.lua'), 'utf8'));
					let cw = 32, ch = 32;
					if (typeof T.cell === 'object' && T.cell) {
						cw = T.cell.__arr[0] ?? T.cell.w ?? 32;
						ch = T.cell.__arr[1] ?? T.cell.h ?? cw;
					} else if (typeof T.cell === 'number') cw = ch = T.cell;
					const anims = {}, named = new Set();
					for (const k of ['idle', 'look', 'clean', 'clean2', 'walk', 'run', 'sleep', 'paw', 'stretch', 'call', 'beg', 'scared', 'angry']) {
						const a = T.anims?.[k];
						if (!a) continue;
						named.add(k);
						if (typeof a === 'string') continue;   // a stand-in: counts as having it
						const cells = [];
						if (a.cells) for (const c of a.cells.__arr) cells.push([c.__arr[0] ?? c.col, c.__arr[1] ?? c.row]);
						else {
							const row = a.row ?? a.__arr[0], nf = a.frames ?? a.__arr[1] ?? 1, col = a.col ?? 0;
							for (let j = 0; j < nf; j++) cells.push([col + j, row]);
						}
						anims[k] = { fps: a.fps ?? a.__arr[2] ?? 8, cells };
					}
					out.push({
						id: n, path: p, name: T.name || n, user: false, ok: true, nsounds: 6, cant: cantOf(named),
						sheet: path.isAbsolute(T.sheet) ? T.sheet : path.join(p, T.sheet),
						info: {
							id: n, name: T.name || n, faces: T.faces === 'left' ? 'left' : 'right',
							cell: [cw, ch], feet: T.feet || 0, hue: T.hue || 0,
							saturation: T.saturation ?? 1, contrast: T.contrast ?? 1, lightness: T.lightness ?? 0,
							tint: T.tint || '#ffffff', tint_amount: T.tint ? (T.tint_amount ?? 1) : 0,
							recolor: T.recolor ? Object.fromEntries(Object.entries(T.recolor).filter(([k]) => k !== '__arr')) : {},
							anims,
						},
					});
				} catch {
					out.push({ id: n, path: p, name: n, user: false, ok: false, nsounds: 0 });
				}
			}
		}
	}
	return out.sort((a, b) => a.id.localeCompare(b.id));
}

const PACKS = listPacks();
const DEFAULT_BOTS = 'nightbot, streamelements, fossabot, moobot, streamlabs, wizebot, kofistreambot, soundalerts, sery_bot, kickbot';

const state = {
	version: '0.3.0-dev',
	backend: 'https://sindruid.com',
	no_echo: true,   // OBS 32+ stops monitoring echo itself
	// where the streamer hears the pet; FAKE_ECHO=1: Desktop Audio records that device too
	monitor: process.env.FAKE_ECHO
		? { device: 'Speakers (Realtek(R) Audio)', capture: 'Desktop Audio', echo: true }
		: { device: 'SteelSeries Sonar - Aux (SteelSeries Sonar Virtual Audio Device)', capture: '', echo: false },
	default_bots: DEFAULT_BOTS,
	account: {
		signed_in: true,
		linking: false,
		link_platform: '',
		login: 'sindruid',
		kick_login: '',
		twitch_live: true,
		kick_live: false,
		code: '',
		link_url: '',
		message: '',
		twitch_missing: '',
		kick_missing: '',
		moods_server_on: true,
		automod_on: true, automod: ['bots', 'scams', 'slurs'], ai_gap: 5, react_on: true, daily_cap: 2000,
		mood_note: 'AI chat reactions active',
		reward_title: 'Pet Blob',
		reward: 'there',
		reward_msg: 'Reward active on Twitch',
		kick_reward: 'unknown',
		kick_reward_msg: '',
	},
	sources: [
		{
			kind: 'pet',
			name: 'OBSPETS',
			active: true,
			settings: {
				pets: 1,
				pack: PACKS[0]?.id || 'blob',
				pack2: PACKS[1]?.id || 'blob',
				pack_dir: '',
				name: '',
				scale: 5,
				weight: 5,
				who: 'turns',
				at_once: 6,
				swap_min: 10,
				swap_alert: false,
				next_in: 'turns',
				perch: '',
				visit: 0.5,
				perch_box: false,
				other_yank: false,
				floor: 24,
				alert: '',
				avoid_list: [{ value: 'Webcam' }],
				reactions: true,
				twitch_alerts: true,
				reward: 'Pet {name}',
				moods: true,
				chatty: 'sometimes',
				automod_only: false,
				ignore: DEFAULT_BOTS,
				hear: 'both',
				sfx_happy: 'stream', sfx_zoomies: 'off',   // (a couple set apart, to show "mixed")
				ear_vol: 1,
				naps: 1,
				energy: 1,
				label_after: ' pet {name}',
				label_color: 0xFFDEE4EC,
				label_font: { face: 'Segoe UI Semibold', size: 26, flags: 0 },
			},
			meta: {
				pack_ok: true,
				pack_id: PACKS[0]?.id || 'blob',
				pack_name: PACKS[0]?.name || 'Blob',
				pack_err: '',
				pack_status: `${PACKS[0]?.name || 'Blob'} is loaded, with OBSPETS's default sounds.`,
				pack_has_sounds: true,
				auto_chat: 'OBSPETS Chat',
				auto_alert: 'Stream Alerts',
				moved: false,
				chat_kind: 'ours',
				chat_name: 'OBSPETS Chat',
				effective_name: PACKS[0]?.name || 'Blob',
				reward_now: `Pet ${PACKS[0]?.name || 'Blob'}`,
				avoid_items: ['Webcam'],
			},
		},
		{
			kind: 'chat',
			name: 'OBSPETS Chat',
			active: true,
			settings: {
				channel: '',
				kick: '',
				width: 450,
				height: 700,
				test: false,
				history: false,
				max: 30,
				fade: 0,
				order: 'up',
				align: 'left',
				anim: 'slide',
				font: { face: 'Segoe UI', size: 24, flags: 0, style: 'Semibold' },
				text_color: 0xFFDEE4EC,
				outline: 3,
				outline_color: 0xFF050303,
				shadow: true,
				name_colors: 'twitch',
				name_color: 0xFFDBC1A6,
				readable: true,
				badges: true,
				pronouns: false,
				name_line: false,
				colon: false,
				bg: 'none',
				bg_color: 0xB0140F0F,
				radius: 8,
				padding: 8,
				gap: 6,
				emotes_7tv: true,
				emotes_bttv: true,
				emotes_ffz: true,
				emote_scale: 100,
				big_emotes: true,
				roles: true,
				role_color: 0xFF3C48E0,
				mentions: true,
				mention_color: 0xFF3CB4F0,
				highlights: true,
				first: true,
				replies: true,
				me_color: true,
				hide_users: DEFAULT_BOTS,
				hide_commands: true,
				hide_links: false,
				words: '',
				words_mode: 'hide',
				css: '',
			},
			meta: {
				status: 'Connected to Twitch (#sindruid)',
				has_browser: true,
			},
		},
	],
	picture_sources: [
		{ name: 'OBSPETS Chat', is_obspet_chat: true },
		{ name: 'Stream Alerts', is_obspet_chat: false },
		{ name: 'Webcam', is_obspet_chat: false },
		{ name: 'Game Capture', is_obspet_chat: false },
	],
	packs: PACKS.map(({ id, name, user, ok, nsounds, cant }) => ({ id, name, user, ok, nsounds, cant: cant || {} })),
};

const TYPES = {
	'.html': 'text/html; charset=utf-8',
	'.js': 'text/javascript; charset=utf-8',
	'.css': 'text/css; charset=utf-8',
	'.png': 'image/png',
};

function readBody(req) {
	return new Promise((res) => {
		const parts = [];
		req.on('data', (c) => parts.push(c));
		req.on('end', () => res(Buffer.concat(parts).toString('utf8')));
	});
}

createServer(async (req, res) => {
	const u = new URL(req.url, 'http://x');
	try {
		if (fakeUpdater(req, res, u)) return;
		if (u.pathname.startsWith('/settings/api/')) {
			if (u.searchParams.get('k') !== KEY) return res.writeHead(403).end('{"error":"key"}');
			const what = u.pathname.slice(14);
			if (what === 'state') {
				res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
				return res.end(JSON.stringify(state));
			}
			if (what === 'fonts') {
				res.writeHead(200, { 'Content-Type': 'application/json' });
				return res.end(JSON.stringify({ fonts: ['Arial', 'Bahnschrift', 'Cascadia Code', 'Comic Sans MS', 'Consolas', 'Georgia', 'Impact', 'Inter', 'Segoe UI', 'Segoe UI Semibold', 'Tahoma', 'Trebuchet MS', 'Verdana'] }));
			}
			if (what === 'update' && req.method === 'POST') {
				const name = u.searchParams.get('name');
				const patch = JSON.parse(await readBody(req));
				const src = state.sources.find((s) => s.name === name);
				if (!src) return res.writeHead(404).end('{"error":"not found"}');
				Object.assign(src.settings, patch);
				if (src.kind === 'pet') {
					if (Array.isArray(patch.avoid_list)) src.meta.avoid_items = patch.avoid_list.map((x) => x.value).filter(Boolean);
					const p = PACKS.find((x) => x.id === src.settings.pack);
					if (p) {
						src.meta.pack_id = p.id;
						src.meta.pack_name = p.name;
						src.meta.effective_name = src.settings.name || p.name;
					}
				}
				console.log('update', name, patch);
				res.writeHead(200, { 'Content-Type': 'application/json' });
				return res.end('{"ok":true}');
			}
			if (what === 'action' && req.method === 'POST') {
				const body = JSON.parse(await readBody(req));
				console.log('action', body);
				if (body.action === 'sign_in') {
					state.account.linking = true;
					state.account.link_platform = body.platform;
					state.account.code = 'PET-4289';
					state.account.link_url = 'https://sindruid.com/link';
					state.account.message = 'Dev preview linking code.';
				} else if (body.action === 'cancel_sign_in') {
					state.account.linking = false;
					state.account.code = '';
				} else if (body.action === 'sign_out') {
					state.account.signed_in = false;
					state.account.login = '';
					state.account.kick_login = '';
				} else if (body.action === 'set_automod') {
					state.account.automod_on = !!body.on;
					state.account.automod = (body.cats || []).map((c) => c.value);
				} else if (body.action === 'set_reacting') {
					state.account.react_on = state.account.moods_server_on = !!body.on;
					state.account.ai_gap = Math.min(600, Math.max(3, +body.gap || 5));
					state.account.daily_cap = Math.min(20000, Math.max(0, +body.daily_cap || 0));
				} else if (body.action === 'try_chat') {
					// the chat tester: a rough stand-in for the AI, after about as long as it takes
					await new Promise((r) => setTimeout(r, 900));
					const t = String(body.text || '').toLowerCase();
					const pick = [
						[/streamboost|followers on|nitro|gift\.|\.ru\b/, 'yank', 'spam advertising', true],
						[/uninstall|so bad|trash|you suck/, 'angry', 'someone is mean to the streamer', false],
						[/\bhi\b|hello|hey|missed you/, 'greet', 'a viewer says hi', false],
						[/cute|best|love/, 'love', 'praise for the pet', false],
						[/\?/, 'curious', 'a question about the pet', false],
						[/lets go|insane|pog|hype/, 'zoomies', 'chat is hyped', false],
					].find(([re]) => re.test(t));
					const [, kind, why, del] = pick || [null, 'none', 'nothing for the pet here', false];
					res.writeHead(200, { 'Content-Type': 'application/json' });
					return res.end(JSON.stringify({ ok: true, kind, why, delete: del, played: !!body.play && kind !== 'none', moods_on: true, automod_only: false, allow_delete: true }));
				}
				res.writeHead(200, { 'Content-Type': 'application/json' });
				return res.end('{"ok":true}');
			}
			return res.writeHead(404).end('{"error":"unknown"}');
		}
		if (u.pathname.startsWith('/editor/api/')) {
			if (u.searchParams.get('k') !== KEY) return res.writeHead(403).end('{"error":"key"}');
			const what = u.pathname.slice(12);
			const id = u.searchParams.get('id');
			const p = PACKS.find((x) => x.id === id);
			if (what === 'pack' && p?.info) {
				res.writeHead(200, { 'Content-Type': 'application/json' });
				return res.end(JSON.stringify(p.info));
			}
			if (what === 'sheet' && p?.sheet && fs.existsSync(p.sheet)) {
				res.writeHead(200, { 'Content-Type': 'image/png' });
				return res.end(fs.readFileSync(p.sheet));
			}
			return res.writeHead(404).end('{"error":"not found"}');
		}
		if (u.pathname.startsWith('/settings/')) {
			const f = path.join(SETTINGS_DIR, u.pathname.slice(10) || 'index.html');
			if (f.startsWith(SETTINGS_DIR) && fs.existsSync(f)) {
				res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
				return res.end(fs.readFileSync(f));
			}
		}
		if (u.pathname.startsWith('/editor/')) {
			const f = path.join(EDITOR_DIR, u.pathname.slice(8) || 'index.html');
			if (f.startsWith(EDITOR_DIR) && fs.existsSync(f)) {
				res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
				return res.end(fs.readFileSync(f));
			}
		}
		res.writeHead(404).end('not found');
	} catch (err) {
		console.error(err);
		res.writeHead(500, { 'Content-Type': 'application/json' }).end(JSON.stringify({ error: err.message }));
	}
}).listen(PORT, '127.0.0.1', () => console.log(`Settings dev server: http://127.0.0.1:${PORT}/settings/?k=${KEY}`));
