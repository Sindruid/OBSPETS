// OBSPETS's Update card, on the settings page and in the character editor (both load this file).
// It asks the plugin (src/updater.cpp) whether a newer OBSPETS is out. Clicking it explains what
// happens, then the plugin downloads the installer, Windows asks for permission, OBS closes, the
// installer updates OBSPETS and opens OBS again. Meanwhile this page waits; the new OBS listens on
// the same address with the same key, so the page finds it again and reloads with the new files.
//
// A page can pass hooks as window.OBSPET_UPDATE_HOOKS:
//   beforeUpdate(): async, saves anything unsaved first (throw to stop the update)
//   canReload():    false while reloading would lose work (the page then asks before reloading)
'use strict';

(() => {
	const KEY = new URLSearchParams(location.search).get('k') || '';
	if (!KEY) return;
	const PAGE = location.pathname.startsWith('/editor') ? 'editor' : 'settings';
	const API = `/${PAGE}/api/self-update`;
	const FLAG = 'obspet-updating';   // sessionStorage: this tab saw an update start (-> its version)
	const hooks = () => window.OBSPET_UPDATE_HOOKS || {};

	let st = null;          // the plugin's last answer
	let gone = 0;           // when OBS stopped answering (0: it answers)
	let open = false;       // the dialog is up
	let mine = false;       // this page clicked Update (or saw one under way)
	let timer = null;
	let waitedTooLong = false;

	// ------------------------------------------------------------ talking to the plugin

	async function call(params = {}, init = {}) {
		const q = new URLSearchParams({ ...params, k: KEY });
		const r = await fetch(`${API}?${q}`, { cache: 'no-store', ...init });
		let body = {};
		try { body = await r.json(); } catch { /* not json */ }
		if (!r.ok) throw Object.assign(new Error(body.error || r.statusText || 'error'), { status: r.status });
		return body;
	}

	const busy = (p) => p === 'downloading' || p === 'verifying' || p === 'permission' || p === 'closing';
	const store = {
		get() { try { return sessionStorage.getItem(FLAG) || ''; } catch { return ''; } },
		set(v) { try { if (v) sessionStorage.setItem(FLAG, v); else sessionStorage.removeItem(FLAG); } catch { /* private mode */ } },
	};

	let restarted = false;   // OBS stopped answering, and answers again: wait for it to be ready, then reload

	async function poll(first = false) {
		clearTimeout(timer);
		try {
			const s = await call(first ? { check: '1' } : {});
			const prev = st;
			if (gone) restarted = true;
			gone = 0;
			waitedTooLong = false;
			st = s;
			if (restarted || (prev && prev.current && prev.current !== s.current)) {
				restarted = true;
				return back(s);
			}
			if (busy(s.phase)) {
				if (s.phase !== 'downloading' && s.phase !== 'verifying') {
					// (an update under way, maybe started on the other page: this page goes along)
					store.set(s.latest);
					mine = true;
					if (!open) show();
				}
			} else if (!starting) {
				// nothing under way (or it failed before OBS closed): no restart to wait for
				store.set('');
				mine = false;
			}
			render();
		} catch (e) {
			if (!gone) gone = Date.now();
			render();
		}
		schedule();
	}

	function schedule() {
		clearTimeout(timer);
		let ms = 60000;
		if (gone) ms = Date.now() - gone > 180000 ? 3000 : 1000;
		else if (st && busy(st.phase)) ms = 500;
		else if (open) ms = 2000;
		timer = setTimeout(() => poll(), ms);
	}

	// OBS answers again after being gone. With the same key that's the OBS an update opened (the
	// plugin hands the key on only then), or the same one after a hiccup. After an update, reload
	// once it has loaded everything, so the page runs the new files and sees the scenes.
	function back(s) {
		if (!s.ready) {
			render();
			timer = setTimeout(() => poll(), 700);
			return;
		}
		restarted = false;
		const updated = !!(store.get() || mine || s.updated_from || s.failed_to);
		if (!updated) {
			render();
			schedule();
			return;
		}
		store.set(store.get() || s.latest || s.current);
		const canReload = hooks().canReload ? hooks().canReload() !== false : true;
		if (canReload) {
			location.reload();
			return;
		}
		mine = true;
		reloadWaiting = true;
		show();
		render();
		schedule();
	}
	let reloadWaiting = false;

	// ------------------------------------------------------------ the card in the header

	function el(tag, props = {}, ...kids) {
		const n = document.createElement(tag);
		for (const [k, v] of Object.entries(props)) {
			if (v == null || v === false) continue;
			if (k === 'class') n.className = v;
			else if (k === 'text') n.textContent = v;
			else if (k.startsWith('on')) n.addEventListener(k.slice(2), v);
			else n.setAttribute(k, v === true ? '' : v);
		}
		for (const c of kids.flat()) if (c != null && c !== false) n.append(c instanceof Node ? c : document.createTextNode(String(c)));
		return n;
	}

	let card = null, overlay = null;

	function mount() {
		const css = el('style', { text: CSS });
		document.head.append(css);
		card = el('button', { type: 'button', class: 'upd-card', hidden: true, onclick: () => show() });
		const slot = document.getElementById('updateSlot');
		if (slot) slot.replaceWith(card);
		else document.querySelector('header.top')?.prepend(card);
		overlay = el('div', { class: 'upd-overlay', hidden: true, role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': 'updTitle' });
		overlay.addEventListener('click', (e) => { if (e.target === overlay && !lockedOpen()) hide(); });
		document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && open && !lockedOpen()) { e.stopPropagation(); hide(); } }, true);
		document.body.append(overlay);
	}

	function renderCard() {
		if (!card) return;
		const s = st;
		let show = false, cls = 'upd-card', label = '', sub = '';
		if ((gone || restarted || reloadWaiting) && (mine || store.get())) {
			show = true; cls += ' busy'; label = 'Updating OBSPETS'; sub = reloadWaiting ? 'Reload to finish' : 'OBS restarts';
		} else if (gone) {
			show = true; cls += ' warn'; label = "Can't reach OBS"; sub = 'Reconnecting';
		} else if (s && busy(s.phase)) {
			show = true; cls += ' busy'; label = 'Updating OBSPETS'; sub = phaseShort(s);
		} else if (s && s.available) {
			show = true; label = 'Update available'; sub = `OBSPETS ${s.latest}`;
		}
		card.hidden = !show;
		if (!show) return;
		card.className = cls;
		card.title = s && s.available ? `OBSPETS ${s.latest} is out (you have ${s.current}). Click to update.` : label;
		card.replaceChildren(el('span', { class: 'upd-dot' }), el('span', { class: 'upd-card-label', text: label }), sub ? el('span', { class: 'upd-card-sub', text: sub }) : null);
	}

	function phaseShort(s) {
		if (s.phase === 'downloading') return `Downloading ${Math.round((s.progress || 0) * 100)}%`;
		if (s.phase === 'verifying') return 'Checking the download';
		if (s.phase === 'permission') return 'Waiting for Windows';
		if (s.phase === 'closing') return 'OBS is closing';
		return '';
	}

	// ------------------------------------------------------------ the dialog

	let starting = false;
	let startError = '';

	const lockedOpen = () => !!(gone && mine) || (st && (st.phase === 'closing' || st.phase === 'permission'));

	function show() {
		open = true;
		overlay.hidden = false;
		render();
		schedule();
	}
	function hide() {
		open = false;
		overlay.hidden = true;
		startError = '';
		if (st && st.phase === 'error') call({ do: 'dismiss' }, { method: 'POST' }).catch(() => {});
		schedule();
	}

	async function start() {
		if (starting) return;
		starting = true;
		startError = '';
		render();
		try {
			if (hooks().beforeUpdate) await hooks().beforeUpdate();
		} catch (e) {
			starting = false;
			startError = `Couldn't save your changes first (${e.message || e}), so nothing was updated.`;
			render();
			return;
		}
		try {
			store.set(st.latest);
			mine = true;
			await call({ do: 'install' }, { method: 'POST' });
		} catch (e) {
			store.set('');
			mine = false;
			startError = e.message || String(e);
		}
		starting = false;
		poll();
	}

	const STEPS = [
		['download', 'Download the update'],
		['permission', 'Windows asks for permission'],
		['closing', 'OBS closes'],
		['install', 'OBSPETS updates and OBS opens again'],
		['back', 'This page reconnects'],
	];

	function stepIndex(s) {
		if (reloadWaiting || (restarted && !gone)) return 4;
		if (gone) return 3;
		if (!s) return 0;
		if (s.phase === 'downloading' || s.phase === 'verifying') return 0;
		if (s.phase === 'permission') return 1;
		if (s.phase === 'closing') return 2;
		return 4;
	}

	let lastSig = '', lastShape = '';
	function render() {
		renderCard();
		if (!open || !overlay) return;
		const s = st || {};
		// redraw only on a change; while downloading, only the bar moves (so it glides, and the
		// buttons keep their hover and focus)
		const flags = [!!gone, restarted, waitedTooLong, starting, startError, mine, reloadWaiting, store.get()];
		const sig = JSON.stringify([s, ...flags]);
		const shape = JSON.stringify([{ ...s, progress: 0 }, ...flags]);
		if (sig === lastSig) return;
		const bar = overlay.querySelector('.upd-bar i');
		lastSig = sig;
		if (shape === lastShape && bar) {
			const pct = Math.round((s.progress || 0) * 100);
			bar.style.width = pct + '%';
			const t = overlay.querySelector('.upd-pct');
			if (t && s.phase === 'downloading') t.textContent = pct + '%';
			return;
		}
		lastShape = shape;
		const dlg = el('div', { class: 'upd-dialog' });
		const updating = ((gone || restarted) && (mine || store.get())) || busy(s.phase) || reloadWaiting;
		const target = s.latest || store.get();

		if (updating) {
			const at = stepIndex(s);
			dlg.append(
				el('h2', { id: 'updTitle', text: `Updating OBSPETS${target ? ' to ' + target : ''}` }),
				el('ol', { class: 'upd-steps' }, STEPS.map(([id, text], i) => {
					const li = el('li', { class: i < at ? 'done' : i === at ? 'now' : '' }, el('span', { class: 'upd-mark' }), el('span', { class: 'upd-step', text }));
					if (i === at) li.append(stepDetail(id, s));
					return li;
				})),
			);
			if (gone && Date.now() - gone > 180000 && !waitedTooLong) waitedTooLong = true;
			if (waitedTooLong) {
				dlg.append(el('div', { class: 'upd-note warn', text: "OBS hasn't come back yet. If it didn't open, open it yourself. This page reconnects as soon as it's running (or open Settings again from OBS's Tools menu)." }));
			}
			if (reloadWaiting) {
				dlg.append(el('div', { class: 'upd-actions' },
					el('span', { class: 'upd-grow muted', text: 'OBS is back. Reload to use the new version.' }),
					el('button', { type: 'button', class: 'primary', text: 'Reload', onclick: () => location.reload() })));
			}
		} else if (s.phase === 'error' || startError) {
			dlg.append(
				el('h2', { id: 'updTitle', text: "OBSPETS didn't update" }),
				el('p', { class: 'upd-sub', text: startError || s.error || 'Something went wrong.' }),
				el('div', { class: 'upd-actions' },
					el('button', { type: 'button', text: 'Close', onclick: hide }),
					s.available ? el('button', { type: 'button', class: 'primary', text: 'Try again', disabled: !!s.blocked || !s.signed_in || starting, onclick: start }) : null),
			);
		} else if (s.available) {
			dlg.append(el('h2', { id: 'updTitle', text: `Update OBSPETS to ${s.latest}?` }));
			if (s.manual) {
				dlg.append(
					el('p', { class: 'upd-sub', text: `You have ${s.current}. This copy of OBSPETS was installed by hand, so it can't update itself. Download the new installer from sindruid.com, close OBS, and run it.` }),
					el('div', { class: 'upd-actions' },
						el('button', { type: 'button', text: 'Not now', onclick: hide }),
						el('a', { class: 'btn primary upd-link', href: s.page_url, target: '_blank', rel: 'noopener', text: 'Open the download page' })),
				);
			} else {
				dlg.append(
					el('p', { class: 'upd-sub', text: `You have ${s.current}. OBS closes, OBSPETS updates, and OBS opens again by itself. It takes about half a minute. Your scenes, pets and settings stay as they are, and this page carries on when OBS is back.` }),
					el('ul', { class: 'upd-list' },
						el('li', { text: 'Windows asks for permission first: click Yes.' }),
						el('li', { text: `The download is ${s.bytes ? (s.bytes / 1048576).toFixed(1) + ' MB' : 'small'}, from sindruid.com, and checked before it runs.` })),
				);
				const why = s.blocked || (!s.signed_in ? 'Sign in first (Account): the update downloads with your Stream Tools account.' : '');
				if (why) dlg.append(el('div', { class: 'upd-note warn', text: why }));
				dlg.append(el('div', { class: 'upd-actions' },
					el('a', { class: 'upd-grow upd-small', href: s.page_url, target: '_blank', rel: 'noopener', text: 'OBSPETS on sindruid.com ↗' }),
					el('button', { type: 'button', text: 'Not now', onclick: hide }),
					el('button', { type: 'button', class: 'primary', text: starting ? 'Starting…' : 'Update and restart OBS', disabled: !!why || starting, onclick: start })));
			}
		} else {
			dlg.append(
				el('h2', { id: 'updTitle', text: 'OBSPETS is up to date' }),
				el('p', { class: 'upd-sub', text: `You have ${s.current || 'the newest version'}.` }),
				el('div', { class: 'upd-actions' }, el('button', { type: 'button', text: 'Close', onclick: hide })),
			);
		}
		overlay.replaceChildren(dlg);
	}

	function stepDetail(id, s) {
		if (id === 'download') {
			const pct = s.phase === 'verifying' ? 100 : Math.round((s.progress || 0) * 100);
			return el('div', { class: 'upd-detail' },
				el('div', { class: 'upd-bar' }, el('i', { style: `width:${pct}%` })),
				el('span', { class: 'muted upd-pct', text: s.phase === 'verifying' ? 'Checking the download…' : `${pct}%` }));
		}
		if (id === 'permission') {
			return el('div', { class: 'upd-detail muted', text: 'Click Yes in the Windows prompt. Not seeing it? Look for a flashing shield in the taskbar.' });
		}
		if (id === 'closing') {
			return el('div', { class: 'upd-detail muted', text: s.hint || 'OBS saves your scenes and closes.' });
		}
		if (id === 'install') {
			return el('div', { class: 'upd-detail muted', text: 'This usually takes 10 to 30 seconds.' });
		}
		return el('div', { class: 'upd-detail muted', text: 'OBS is starting…' });
	}

	// ------------------------------------------------------------ after a restart

	function afterReload(s) {
		const target = store.get();
		if (!target) return;
		store.set('');
		if (s.updated_from) {
			note(`OBSPETS is updated to ${s.current}.`, 'ok');
		} else if (s.failed_to) {
			st = s;
			startError = `The update to ${s.failed_to} didn't install, so this is still OBSPETS ${s.current}. ` +
				'Try again, or download the installer from sindruid.com.';
			show();
		}
	}

	function note(text, kind) {
		const n = el('div', { class: 'upd-toast ' + (kind || ''), role: 'status' }, el('span', { class: 'upd-dot' }), el('span', { text }));
		document.body.append(n);
		setTimeout(() => n.classList.add('out'), 5200);
		setTimeout(() => n.remove(), 5800);
	}

	// ------------------------------------------------------------ styles (the pages' own palette)

	const CSS = `
.upd-card { display: inline-flex; align-items: center; gap: 8px; padding: 5px 11px; min-height: 30px; cursor: pointer;
	background: var(--accent-dim, rgba(226,75,26,.14)); border: 1px solid var(--accent-line, rgba(226,75,26,.45)); color: var(--text);
	border-radius: var(--radius, 0); font-size: 12.5px; line-height: 1.2; white-space: nowrap; }
.upd-card:hover { background: rgba(226,75,26,.22); border-color: var(--accent, #e24b1a); }
.upd-card[hidden] { display: none; }
.upd-card-label { font-weight: 600; }
.upd-card-sub { color: var(--muted); }
.upd-card .upd-dot, .upd-toast .upd-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--accent, #e24b1a); flex: none; box-shadow: 0 0 0 3px rgba(226,75,26,.18); }
.upd-card.busy .upd-dot { animation: upd-pulse 1.1s ease-in-out infinite; }
.upd-card.warn { background: rgba(232,180,76,.10); border-color: rgba(232,180,76,.45); }
.upd-card.warn .upd-dot { background: var(--warn, #e8b44c); box-shadow: 0 0 0 3px rgba(232,180,76,.18); animation: upd-pulse 1.1s ease-in-out infinite; }
@keyframes upd-pulse { 50% { opacity: .35; } }
.upd-overlay { position: fixed; inset: 0; z-index: 200; background: rgba(0,0,0,.72); backdrop-filter: blur(4px);
	display: flex; align-items: center; justify-content: center; padding: 20px; }
.upd-overlay[hidden] { display: none; }
.upd-dialog { width: min(480px, 100%); background: var(--panel, #111); border: 1px solid var(--line2, rgba(255,255,255,.14));
	border-radius: var(--radius, 0); padding: 22px 22px 18px; box-shadow: 0 24px 60px rgba(0,0,0,.6); }
.upd-dialog h2 { margin: 0 0 6px; font-size: 20px; font-weight: 650; letter-spacing: -0.03em; }
.upd-sub { color: var(--muted); margin: 0 0 12px; font-size: 13.5px; }
.upd-list { margin: 0 0 4px; padding-left: 18px; color: var(--muted); font-size: 13px; }
.upd-list li { margin: 2px 0; }
.upd-note { margin-top: 12px; padding: 9px 11px; font-size: 13px; border: 1px solid var(--line2, rgba(255,255,255,.14)); }
.upd-note.warn { border-color: rgba(232,180,76,.45); background: rgba(232,180,76,.10); color: var(--text); }
.upd-actions { display: flex; align-items: center; justify-content: flex-end; gap: 8px; margin-top: 18px; }
.upd-grow { flex: 1; }
.upd-small { color: var(--muted); font-size: 12.5px; }
.upd-small:hover { color: var(--text); }
.upd-link { display: inline-flex; align-items: center; padding: 7px 14px; background: var(--accent, #e24b1a); color: var(--accent-ink, #fff);
	border: 1px solid var(--accent, #e24b1a); font-weight: 600; text-decoration: none; }
.upd-link:hover { background: var(--accent-hover, #f0602f); }
.upd-steps { list-style: none; margin: 14px 0 0; padding: 0; }
.upd-steps li { display: grid; grid-template-columns: 22px 1fr; column-gap: 8px; padding: 6px 0; color: var(--faint, #5f5f5f); font-size: 13.5px; }
.upd-steps li.done { color: var(--muted); }
.upd-steps li.now { color: var(--text); font-weight: 600; }
.upd-mark { width: 16px; height: 16px; margin-top: 2px; border-radius: 50%; border: 1.5px solid currentColor; position: relative; }
.upd-steps li.done .upd-mark { border-color: var(--ok, #4cc38a); background: var(--ok, #4cc38a); }
.upd-steps li.done .upd-mark::after { content: ''; position: absolute; left: 4.5px; top: 1.5px; width: 4px; height: 8px;
	border: solid var(--bg, #0a0a0a); border-width: 0 2px 2px 0; transform: rotate(45deg); }
.upd-steps li.now .upd-mark { border-color: var(--accent, #e24b1a); border-top-color: transparent; animation: upd-spin .8s linear infinite; }
@keyframes upd-spin { to { transform: rotate(360deg); } }
.upd-detail { grid-column: 2; margin-top: 4px; font-size: 12.5px; font-weight: 400; display: flex; align-items: center; gap: 10px; }
.upd-detail.muted, .upd-detail .muted { color: var(--muted); }
.upd-bar { flex: 1; height: 4px; background: var(--line2, rgba(255,255,255,.14)); overflow: hidden; }
.upd-bar i { display: block; height: 100%; background: var(--accent, #e24b1a); transition: width .3s ease; }
.upd-toast { position: fixed; left: 50%; bottom: 22px; transform: translateX(-50%); z-index: 210; display: flex; align-items: center; gap: 9px;
	padding: 10px 16px; background: var(--panel2, #161616); border: 1px solid var(--line2, rgba(255,255,255,.14)); color: var(--text);
	font-size: 13.5px; box-shadow: 0 12px 30px rgba(0,0,0,.5); transition: opacity .5s ease, transform .5s ease; }
.upd-toast.ok { border-color: var(--ok, #4cc38a); }
.upd-toast.ok .upd-dot { background: var(--ok, #4cc38a); box-shadow: 0 0 0 3px rgba(76,195,138,.18); }
.upd-toast.out { opacity: 0; transform: translateX(-50%) translateY(8px); }
@media (prefers-reduced-motion: reduce) {
	.upd-card.busy .upd-dot, .upd-card.warn .upd-dot, .upd-steps li.now .upd-mark { animation: none; }
}
`;

	// ------------------------------------------------------------ boot

	async function boot() {
		mount();
		try {
			const s = await call({ check: '1' });
			st = s;
			afterReload(s);
			if (busy(s.phase) && s.phase !== 'downloading' && s.phase !== 'verifying') {
				mine = true;
				store.set(s.latest);
				show();
			}
			render();
		} catch {
			/* the page itself says when OBS can't be reached */
		}
		schedule();
		// a sindruid.com check that was due happens soon after the page opens
		setTimeout(() => { if (!gone && st && !st.checked) poll(); }, 4000);
		window.addEventListener('focus', () => { if (!busy(st?.phase) && !gone) poll(); });
	}

	if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
	else boot();
})();
