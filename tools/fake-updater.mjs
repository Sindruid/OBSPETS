// A stand-in for the plugin's updater (src/updater.cpp) for the dev servers (settings-dev.mjs,
// editor-dev.mjs), to work on the Update card (data/update/update.js) without OBS. It plays a
// whole update: the download, the Windows prompt, OBS closing (the server stops answering for a
// while), and OBS coming back with the new version on the same address.
//
//   FAKE_UPDATE=9.9.0               a newer version is out (default: none)
//   FAKE_UPDATE_RESULT=ok           ok | failed (the installer didn't install) | declined (no to Windows) | download
//   FAKE_UPDATE_BLOCKED="You're live. ..."   the reason the button is off
//   FAKE_UPDATE_MANUAL=1            installed by hand: a download link instead
//   FAKE_UPDATE_SIGNED_OUT=1
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
export const UPDATE_JS = path.resolve(here, '..', 'data', 'update', 'update.js');

const env = process.env;
const st = {
	current: JSON.parse(fs.readFileSync(path.join(here, '..', 'buildspec.json'), 'utf8')).version, latest: env.FAKE_UPDATE || '', available: false, checked: true, bytes: 5544247,
	phase: 'idle', progress: 0, error: '', hint: '', blocked: env.FAKE_UPDATE_BLOCKED || '',
	manual: env.FAKE_UPDATE_MANUAL === '1', signed_in: env.FAKE_UPDATE_SIGNED_OUT !== '1',
	page_url: 'https://sindruid.com/streamtools/obspets', updated_from: '', failed_to: '', ready: true,
};
const refresh = () => { st.available = !!st.latest && st.latest !== st.current; };
refresh();
let downUntil = 0;
const later = (ms) => new Promise((r) => setTimeout(r, ms));

async function play() {
	const result = env.FAKE_UPDATE_RESULT || 'ok';
	st.phase = 'downloading';
	for (let i = 0; i <= 20; i++) {
		st.progress = i / 20;
		await later(150);
		if (result === 'download' && i === 9) {
			Object.assign(st, { phase: 'error', progress: 0, error: "Couldn't download the update (Timeout was reached). Check the internet connection and try again." });
			return;
		}
	}
	st.phase = 'verifying';
	await later(600);
	st.phase = 'permission';
	await later(2500);
	if (result === 'declined') {
		Object.assign(st, { phase: 'error', error: "Windows didn't get permission, so nothing changed. Click Update to try again." });
		return;
	}
	st.phase = 'closing';
	await later(1500);
	// OBS closes: nothing answers, then the new OBS starts on the same port with the same key
	downUntil = Date.now() + 6000;
	await later(6000);
	const from = st.current;
	Object.assign(st, { phase: 'idle', progress: 0, hint: '', ready: false });
	if (result === 'ok') {
		st.current = st.latest;
		st.updated_from = from;
	} else {
		st.failed_to = st.latest;
	}
	refresh();
	await later(2000);
	st.ready = true;
}

// true if it answered (or, while "OBS is closed", dropped) the request
export function fakeUpdater(req, res, u) {
	if (Date.now() < downUntil) {
		req.socket.destroy();
		return true;
	}
	if (u.pathname === '/update/update.js') {
		res.writeHead(200, { 'Content-Type': 'text/javascript; charset=utf-8', 'Cache-Control': 'no-store' });
		res.end(fs.readFileSync(UPDATE_JS));
		return true;
	}
	if (u.pathname !== '/settings/api/self-update' && u.pathname !== '/editor/api/self-update') return false;
	const send = (code, body) => res.writeHead(code, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }).end(JSON.stringify(body));
	if (req.method === 'GET') return send(200, st), true;
	const what = u.searchParams.get('do');
	if (what === 'dismiss') {
		if (st.phase === 'error') Object.assign(st, { phase: 'idle', error: '' });
		st.updated_from = st.failed_to = '';
		return send(200, { ok: true }), true;
	}
	if (what === 'install') {
		if (st.blocked) return send(409, { error: st.blocked }), true;
		if (!st.signed_in) return send(409, { error: 'Sign in first (Account), then update.' }), true;
		if (['downloading', 'verifying', 'permission', 'closing'].includes(st.phase)) return send(200, { ok: true }), true;
		Object.assign(st, { error: '', progress: 0 });
		play();
		console.log('fake update: installing', st.latest);
		return send(200, { ok: true }), true;
	}
	return send(404, { error: 'unknown' }), true;
}
