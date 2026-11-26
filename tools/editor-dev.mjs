// A stand-in for the plugin's editor API, to work on data/editor/ without OBS:
//
//   node tools/editor-dev.mjs [user packs folder]
//   -> http://127.0.0.1:8798/editor/?k=dev
//
// It serves data/editor/ and answers /editor/api/... the way src/editor.cpp does, reading
// pack.lua with a small parser for the plain tables packs use. Built-in characters come from
// data/packs/ (and ../../sources/pet/packs/ if it exists). Your packs default to .editor-dev/
// (git-ignored). "Try it on stream" and "Show on my Pet" only log.
import { createServer } from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const EDITOR = path.join(root, 'data', 'editor');
const BUILTIN = [path.join(root, 'data', 'packs'), path.resolve(root, '..', '..', 'sources', 'pet', 'packs')];
const USER = path.resolve(process.argv[2] || path.join(root, '.editor-dev'));
const PORT = 8798, KEY = 'dev';
const SOUNDS = path.join(root, 'data', 'sounds');   // the plugin's sound sets: <set>/<role>/*.wav
fs.mkdirSync(USER, { recursive: true });
let onStream = null;

// ------------------------------------------------------------------ pack.lua, as data

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
			if (src.startsWith('..', i)) {
				i += 2;
				v = String(v) + String(prim());
			} else return v;
		}
	};
	const table = () => {
		i++;
		const t = { __arr: [] };
		for (;;) {
			ws();
			if (src[i] === '}') {
				i++;
				return t;
			}
			if (src[i] === '[') {
				i++;
				const k = exp();
				ws();
				i++; // ]
				ws();
				i++; // =
				t[k] = exp();
			} else {
				const m = /^([A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)/.exec(src.slice(i));
				if (m) {
					i += m[0].length;
					t[m[1]] = exp();
				} else t.__arr.push(exp());
			}
			ws();
			if (src[i] === ',' || src[i] === ';') i++;
		}
	};
	ws();
	if (!src.startsWith('return', i)) throw new Error('pack.lua must end with `return { ... }`');
	i += 6;
	return exp();
}

const ANIMS = ['idle', 'look', 'clean', 'clean2', 'walk', 'run', 'sleep', 'paw', 'scared'];
const FB = { look: 'idle', clean: 'idle', clean2: 'clean', walk: 'idle', run: 'walk', sleep: 'idle', paw: 'idle', scared: 'look' };
const POSES = ['crouch', 'rise', 'fall', 'land'];
const ROLES = ['pet', 'notice', 'cheer', 'question', 'plead', 'angry', 'swipe', 'land', 'pounce', 'fling', 'pop'];
const COLORS = { heart: '#c42a1f', glint: '#ece4de', bang: '#f8c934', anger: '#db3329', mark: '#ece4de', note: '#f8c934', note2: '#ece4de', spark: '#ece4de', z: '#ece4de' };

function loadPack(dir) {
	const T = parseLua(fs.readFileSync(path.join(dir, 'pack.lua'), 'utf8'));
	const id = path.basename(dir);
	const inDir = (n, base) => (path.isAbsolute(n) ? n : path.join(base, n));
	if (!T.sheet) throw new Error('pack.lua has no `sheet` (the sprite sheet PNG)');
	const sheet = inDir(T.sheet, dir);
	if (!fs.existsSync(sheet)) throw new Error("the sprite sheet isn't there: " + sheet);
	let cw = 32, ch = 32;
	if (typeof T.cell === 'object' && T.cell) {
		cw = T.cell.__arr[0] ?? T.cell.w ?? 32;
		ch = T.cell.__arr[1] ?? T.cell.h ?? cw;
	} else if (typeof T.cell === 'number') cw = ch = T.cell;
	const unit = ch / 32;
	const B = T.body || {};
	const body = {
		half: B.half ?? 9 * unit, top: B.top ?? 15 * unit, top_asleep: B.top_asleep ?? 10 * unit, head: B.head ?? 3 * unit,
		paw: B.paw ? [B.paw.__arr[0], B.paw.__arr[1]] : [9 * unit, 8 * unit], shadow: B.shadow ?? 6 * unit,
	};
	const speed = { walk: T.speed?.walk ?? 24 * unit, run: T.speed?.run ?? 70 * unit };
	const anims = {};
	for (const k of ANIMS) {
		const a = T.anims?.[k];
		if (!a) continue;
		let cells = [];
		if (a.cells) cells = a.cells.__arr.map((c) => [c.__arr[0] ?? c.col, c.__arr[1] ?? c.row]);
		else {
			const row = a.row ?? a.__arr[0], n = a.frames ?? a.__arr[1] ?? 1, col = a.col ?? 0;
			for (let j = 0; j < n; j++) cells.push([col + j, row]);
		}
		const n = cells.length, whoosh = a.whoosh ?? Math.floor(n * 0.34);
		anims[k] = { fps: a.fps ?? a.__arr[2] ?? 8, whoosh, hit: a.hit ?? Math.min(n - 1, whoosh + 1), cells };
	}
	if (!anims.idle) throw new Error('pack.lua needs at least an `idle` animation');
	const poses = {};
	for (const k of POSES) {
		const p = T.poses?.[k];
		if (p) poses[k] = [p.col ?? p.__arr[1] ?? 0, p.row ?? p.__arr[0] ?? 0];
	}
	const sdir = T.sound_dir ? inDir(T.sound_dir, dir) : dir;
	const files = {};
	const role = (k, depth) => {
		const v = T.sounds?.[k];
		if (typeof v === 'string' && ROLES.includes(v) && depth < 4) return role(v, depth + 1);
		const names = typeof v === 'string' ? [v] : v ? v.__arr : [];
		return names.map((n) => inDir(n, sdir)).filter((f) => fs.existsSync(f));
	};
	const sets = soundSets();
	let soundSet = String(T.sound_set || 'default').toLowerCase();
	if (soundSet !== 'none' && !sets[soundSet]) soundSet = 'default';
	const defaults = [];
	for (const k of ROLES) {
		const f = role(k, 0);
		if (f.length) files[k] = f;
		else if (T.sounds?.[k] !== false && soundSet !== 'none' && (sets[soundSet]?.[k] || sets.default?.[k])) defaults.push(k);
	}
	const colors = { ...COLORS, ...(T.colors ? Object.fromEntries(Object.entries(T.colors).filter(([k]) => k !== '__arr')) : {}) };
	return {
		id, name: T.name || id, describe: T.describe || '', pronouns: T.pronouns || '', personality: T.personality || '',
		faces: T.faces === 'left' ? 'left' : 'right', cell: [cw, ch], feet: T.feet || 0, hue: T.hue || 0, look: { saturation: T.saturation ?? 1, contrast: T.contrast ?? 1, lightness: T.lightness ?? 0, tint: T.tint || '#ffffff', tint_amount: T.tint ? (T.tint_amount ?? 1) : 0, recolor: T.recolor ? Object.fromEntries(Object.entries(T.recolor).filter(([k]) => k !== '__arr')) : {} }, body, speed, anims, poses, colors, sheet, files, defaults, soundSet,
	};
}

// set -> role -> the built-in .wav files (data/sounds/<set>/<role>/)
function soundSets() {
	const out = {};
	if (!fs.existsSync(SOUNDS)) return out;
	for (const set of fs.readdirSync(SOUNDS).sort()) {
		if (!fs.statSync(path.join(SOUNDS, set)).isDirectory()) continue;
		for (const k of ROLES) {
			const d = path.join(SOUNDS, set, k);
			if (!fs.existsSync(d)) continue;
			const f = fs.readdirSync(d).filter((n) => n.toLowerCase().endsWith('.wav')).sort();
			if (f.length) (out[set] ??= {})[k] = f.map((n) => path.join(d, n));
		}
	}
	return out;
}

function listPacks() {
	const out = [], seen = new Set();
	const scan = (dir, user) => {
		if (!fs.existsSync(dir)) return;
		for (const n of fs.readdirSync(dir)) {
			if (n.startsWith('.') || n.startsWith('_') || seen.has(n)) continue;
			const p = path.join(dir, n);
			if (fs.statSync(p).isDirectory() && fs.existsSync(path.join(p, 'pack.lua'))) {
				seen.add(n);
				out.push({ id: n, path: p, user });
			}
		}
	};
	scan(USER, true);
	for (const d of BUILTIN) scan(d, false);
	return out.sort((a, b) => a.id.localeCompare(b.id));
}

// ------------------------------------------------------------------ the API

const GOOD_ID = /^[a-z0-9][a-z0-9_-]{0,39}$/;
const GOOD_PATH = /^(pack\.json|pack\.lua|sheet\.png|source\/sheet\.png|anims\/[a-z0-9]{1,16}\.png|sounds\/[a-z]{1,16}\/[a-z0-9][a-z0-9_-]{0,47}\.wav)$/;
const stamp = () => new Date().toISOString().replace(/[-:]/g, '').replace('T', '-').slice(0, 15);
function keepOld(dir, rel) {
	const from = path.join(dir, rel);
	if (!fs.existsSync(from)) return;
	const made = fs.existsSync(path.join(dir, 'pack.json'));
	if (!rel.startsWith('anims/') && !rel.startsWith('sounds/') && (made || rel === 'pack.json')) return fs.unlinkSync(from);
	const to = path.join(dir, '.history', stamp(), rel);
	fs.mkdirSync(path.dirname(to), { recursive: true });
	fs.renameSync(from, to);
}

async function api(what, q, req) {
	const body = await new Promise((res) => {
		const parts = [];
		req.on('data', (c) => parts.push(c));
		req.on('end', () => res(Buffer.concat(parts)));
	});
	if (what === 'packs') {
		const packs = listPacks().map((e) => {
			let name = e.id, ok = true;
			try {
				name = loadPack(e.path).name;
			} catch {
				ok = false;
				const j = path.join(e.path, 'pack.json');
				if (fs.existsSync(j)) name = JSON.parse(fs.readFileSync(j, 'utf8')).name || name;
			}
			return { id: e.id, name, user: e.user, made_here: fs.existsSync(path.join(e.path, 'pack.json')), on_stream: e.id === onStream, ok };
		});
		return [200, { packs, pets: 1 }];
	}
	if (what === 'defaults') {
		const sets = soundSets();
		return [200, Object.fromEntries(Object.entries(sets).map(([s, roles]) => [s, Object.fromEntries(Object.entries(roles).map(([k, v]) => [k, v.map((f) => path.basename(f))]))]))];
	}
	if (what === 'default-sound') {
		const f = soundSets()[q.get('set') || 'default']?.[q.get('role')]?.[+q.get('i')];
		return f ? [200, fs.readFileSync(f), 'audio/wav'] : [404, { error: 'no such sound' }];
	}
	const id = q.get('id') || '';
	if (!GOOD_ID.test(id)) return [400, { error: 'bad id' }];
	const e = listPacks().find((p) => p.id === id);
	const mine = path.join(USER, id);
	if (req.method === 'GET') {
		if (what === 'pack') {
			if (!e) return [404, { error: 'no such character' }];
			const editor = fs.existsSync(path.join(e.path, 'pack.json')) ? JSON.parse(fs.readFileSync(path.join(e.path, 'pack.json'), 'utf8')) : null;
			try {
				const P = loadPack(e.path);
				const sounds = Object.fromEntries(Object.entries(P.files).map(([k, v]) => [k, v.map((f) => path.basename(f))]));
				const anims = Object.fromEntries(Object.entries(P.anims));
				return [200, { id, user: e.user, editor, name: P.name, describe: P.describe, pronouns: P.pronouns, personality: P.personality, faces: P.faces, cell: P.cell, feet: P.feet, hue: P.hue, ...P.look, body: P.body, speed: P.speed, anims, poses: P.poses, sounds, default_sounds: P.defaults, sound_set: P.soundSet, colors: P.colors, warnings: [] }];
			} catch (err) {
				return [200, { id, user: e.user, editor, error: err.message }];
			}
		}
		if (what === 'sheet') return e ? [200, fs.readFileSync(loadPack(e.path).sheet), 'image/png'] : [404, { error: 'no such character' }];
		if (what === 'sound') {
			const f = e && loadPack(e.path).files[q.get('role')]?.[+q.get('i')];
			return f ? [200, fs.readFileSync(f), 'audio/wav'] : [404, { error: 'no such sound' }];
		}
		if (what === 'file') {
			const p = q.get('path');
			if (!e || !e.user || !GOOD_PATH.test(p) || !fs.existsSync(path.join(mine, p))) return [404, { error: 'no such file' }];
			return [200, fs.readFileSync(path.join(mine, p)), p.endsWith('.png') ? 'image/png' : p.endsWith('.wav') ? 'audio/wav' : 'text/plain'];
		}
		return [404, { error: 'unknown' }];
	}
	if (what === 'put' || what === 'del') {
		const p = q.get('path');
		if (!GOOD_PATH.test(p)) return [400, { error: 'bad path' }];
		if (e && !e.user) return [403, { error: "that's a built-in character: make a copy to change it" }];
		keepOld(mine, p);
		if (what === 'put') {
			fs.mkdirSync(path.dirname(path.join(mine, p)), { recursive: true });
			fs.writeFileSync(path.join(mine, p), body);
		}
		return [200, { ok: true }];
	}
	if (what === 'reveal') {
		const p = q.get('path'), f = path.join(mine, p || '');
		if (!e || !e.user || !GOOD_PATH.test(p) || !fs.existsSync(f)) return [404, { error: 'no such file' }];
		if (process.platform === 'win32') spawn('explorer.exe', ['/select,' + f], { detached: true, stdio: 'ignore' }).unref();
		console.log('reveal', f);
		return [200, { ok: true }];
	}
	if (what === 'done') {
		console.log('reload', id);
		return [200, { ok: true }];
	}
	if (what === 'use') {
		onStream = id;
		console.log('use', id);
		return [200, { ok: true, pets: 1 }];
	}
	if (what === 'try') {
		console.log('try', id, q.get('what'));
		return [200, { ok: true, pets: id === onStream ? 1 : 0 }];
	}
	if (what === 'remove') {
		if (!e || !e.user) return [403, { error: 'only your own characters can be removed' }];
		fs.mkdirSync(path.join(USER, '.trash'), { recursive: true });
		fs.renameSync(mine, path.join(USER, '.trash', id + '-' + stamp()));
		return [200, { ok: true }];
	}
	return [404, { error: 'unknown' }];
}

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8' };
createServer(async (req, res) => {
	const u = new URL(req.url, 'http://x');
	try {
		if (u.pathname.startsWith('/editor/api/')) {
			if (u.searchParams.get('k') !== KEY) return res.writeHead(403).end('{"error":"key"}');
			const [status, out, type] = await api(u.pathname.slice(12), u.searchParams, req);
			res.writeHead(status, { 'Content-Type': type || 'application/json', 'Cache-Control': 'no-store' });
			return res.end(Buffer.isBuffer(out) ? out : JSON.stringify(out));
		}
		if (u.pathname.startsWith('/editor/')) {
			const f = path.join(EDITOR, u.pathname.slice(8) || 'index.html');
			if (f.startsWith(EDITOR) && fs.existsSync(f)) {
				res.writeHead(200, { 'Content-Type': TYPES[path.extname(f)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
				return res.end(fs.readFileSync(f));
			}
		}
		res.writeHead(404).end('not found');
	} catch (err) {
		console.error(err);
		res.writeHead(500, { 'Content-Type': 'application/json' }).end(JSON.stringify({ error: err.message }));
	}
}).listen(PORT, '127.0.0.1', () => console.log(`http://127.0.0.1:${PORT}/editor/?k=${KEY}  (your packs: ${USER})`));
