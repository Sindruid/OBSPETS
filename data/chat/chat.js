// OBSPETS Chat's page: anonymous, read-only Twitch chat (IRC over WebSocket) and Kick chat (Kick's
// public Pusher socket) in one list, styled by the source's properties. It reports where its
// messages are so a Pet can sit on them and yank one out.
//
// The plugin -> this page: window events (obs-browser's javascript_event), each with a JSON detail:
//   obspet_config      the whole config (the same as GET /chat/api/config)
//   obspet_yank        { req, id, user, strike }: a Pet is about to yank that message: where is it?
//                      (strike: draw its picture crossed out)
//   obspet_yank_done   { req, took }: took = the Pet has it, so the real line goes now
//   obspet_strike      { id }: Auto mod deleted that message on its platform. Keep it, crossed out,
//                      for the Pet to rip out (it's coming), instead of dropping it on the delete event
//   obspet_line        { id, user, text, color }: show this line (the Pet's "Try a reaction")
// This page -> the plugin: POST /chat/api/report { kind: "layout" | "yank" | "status", ... }.
"use strict";

const Q = new URLSearchParams(location.search);
const ID = Q.get("id") || "", KEY = Q.get("k") || "";
const api = (what, extra = "") => `/chat/api/${what}?id=${encodeURIComponent(ID)}&k=${encodeURIComponent(KEY)}${extra}`;
const post = obj => fetch(api("report"), { method: "POST", body: JSON.stringify(obj) }).catch(() => {});

const root = document.documentElement;
const list = document.getElementById("list");
const note = document.getElementById("note");
const vars = document.getElementById("vars");
const custom = document.getElementById("custom");

let cfg = null;

// ------------------------------------------------------------------ status

let lastStatus = "";
function status(text, onScreen = "") {
  note.textContent = onScreen;
  if (text === lastStatus) return;
  lastStatus = text;
  post({ kind: "status", text });
}

// ------------------------------------------------------------------ colors

function parseColor(s) {
  const m = /rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)/.exec(s || "");
  if (m) return [+m[1], +m[2], +m[3], m[4] === undefined ? 1 : +m[4]];
  const h = /^#([0-9a-f]{6})$/i.exec(s || "");
  if (h) { const n = parseInt(h[1], 16); return [n >> 16, n >> 8 & 255, n & 255, 1]; }
  return null;
}
const rgba = (c, a) => `rgba(${c[0]},${c[1]},${c[2]},${a})`;
function toHsl([r, g, b]) {
  r /= 255; g /= 255; b /= 255;
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, d = mx - mn;
  let h = 0, s = 0;
  if (d) {
    s = d / (1 - Math.abs(2 * l - 1));
    h = (mx === r ? ((g - b) / d) % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4) * 60;
  }
  return [(h + 360) % 360, s, l];
}
const lum = c => (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255;

// A chatter's name color, kept readable against the chat's text: light text (dark stream) gets
// names at least 62 % light, dark text at most 40 %.
const nameCache = new Map();
function nameColor(hex) {
  if (cfg.name_colors === "one" || !hex) return cfg.name_color;
  if (!cfg.readable) return hex;
  let c = nameCache.get(hex);
  if (c) return c;
  const rgb = parseColor(hex);
  if (!rgb) return cfg.name_color;
  const [h, s, l] = toHsl(rgb);
  const light = lum(parseColor(cfg.text_color) || [236, 228, 222]) > 0.5;
  const L = light ? Math.max(l, 0.62) : Math.min(l, 0.4);
  c = `hsl(${Math.round(h)} ${Math.round(s * 100)}% ${Math.round(L * 100)}%)`;
  nameCache.set(hex, c);
  return c;
}

// ------------------------------------------------------------------ applying the config

function strokeShadow(px, color, shadow) {
  const out = [];
  if (px > 0) {
    const n = Math.max(8, Math.round(px * 6));
    for (let i = 0; i < n; i++) {
      const a = (i / n) * Math.PI * 2;
      out.push(`${(Math.cos(a) * px).toFixed(2)}px ${(Math.sin(a) * px).toFixed(2)}px 0 ${color}`);
    }
  }
  if (shadow) out.push("0 2px 3px rgba(0,0,0,.85)", "0 0 10px rgba(0,0,0,.55)");
  return out.length ? out.join(", ") : "none";
}

const esc = s => String(s).replace(/[\\"]/g, "\\$&");
let wordRe = null, mentionRe = null, hideUsers = new Set();

function apply(next) {
  const prev = cfg;
  cfg = next;
  root.className = [
    cfg.order === "down" ? "down" : "up",
    cfg.align === "right" ? "right" : "left",
    "bg-" + (cfg.bg || "none"),
    cfg.roles ? "marks" : "",
    cfg.name_line ? "name-line" : "",
  ].filter(Boolean).join(" ");
  const mention = parseColor(cfg.mention_color) || [240, 180, 60];
  vars.textContent = `:root {
    --font: "${esc(cfg.font_face || "Segoe UI")}", "Segoe UI", system-ui, sans-serif;
    --size: ${cfg.font_size || 24}px; --weight: ${cfg.bold ? 700 : 600}; --style: ${cfg.italic ? "italic" : "normal"};
    --text: ${cfg.text_color}; --name: ${cfg.name_color};
    --stroke: ${strokeShadow(cfg.outline || 0, cfg.outline_color, cfg.shadow)};
    --bg: ${cfg.bg_color}; --radius: ${cfg.radius}px; --pad: ${cfg.padding}px; --gap: ${cfg.gap}px;
    --emote: ${(1.6 * (cfg.emote_scale || 100) / 100).toFixed(3)}em;
    --role: ${cfg.role_color}; --mention: ${cfg.mention_color}; --mention-soft: ${rgba(mention, 0.24)};
  }`;
  custom.textContent = cfg.css || "";
  hideUsers = new Set((cfg.hide_users || "").toLowerCase().split(",").map(s => s.trim().replace(/^@/, "")).filter(Boolean));
  const words = (cfg.words || "").split(",").map(s => s.trim()).filter(Boolean).map(w => w.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"));
  wordRe = words.length ? new RegExp(`(?<![\\p{L}\\p{N}_])(?:${words.join("|")})(?![\\p{L}\\p{N}_])`, "giu") : null;
  const mine = [cfg.channel, cfg.kick].filter(Boolean).map(c => c.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"));
  mentionRe = mine.length ? new RegExp(`(?<![\\w])@?(?:${mine.join("|")})(?![\\w])`, "i") : null;
  nameCache.clear();
  while (list.childElementCount > cfg.max) list.firstElementChild.remove();

  if (!prev || prev.channel !== cfg.channel || prev.kick !== cfg.kick || prev.test !== cfg.test ||
      prev.kick_key !== cfg.kick_key || prev.kick_cluster !== cfg.kick_cluster || prev.kick_room !== cfg.kick_room) start();
  if (!prev || prev.channel !== cfg.channel || prev.signed_in !== cfg.signed_in || prev.badges !== cfg.badges) loadBadges();
  if (cfg.pronouns && !pronounNames) loadPronounNames();
  requestAnimationFrame(report);
}

// ------------------------------------------------------------------ emotes, badges, pronouns

const extra = { "7tv": new Map(), bttv: new Map(), ffz: new Map() };
let extraFor = "";
async function loadExtra(roomId) {
  if (extraFor === roomId) return;
  extraFor = roomId;
  for (const m of Object.values(extra)) m.clear();
  const get = u => fetch(u).then(r => r.ok ? r.json() : null).catch(() => null);
  const [stvG, stvC, btG, btC, ffz] = await Promise.all([
    get("https://7tv.io/v3/emote-sets/global"),
    get("https://7tv.io/v3/users/twitch/" + roomId),
    get("https://api.betterttv.net/3/cached/emotes/global"),
    get("https://api.betterttv.net/3/cached/users/twitch/" + roomId),
    get("https://api.betterttv.net/3/cached/frankerfacez/users/twitch/" + roomId),
  ]);
  for (const e of ffz || []) extra.ffz.set(e.code, e.images["2x"] || e.images["1x"]);
  for (const e of [...(btG || []), ...(btC?.channelEmotes || []), ...(btC?.sharedEmotes || [])])
    extra.bttv.set(e.code, `https://cdn.betterttv.net/emote/${e.id}/2x`);
  for (const e of [...(stvG?.emotes || []), ...(stvC?.emote_set?.emotes || [])]) {
    const host = e.data?.host;
    if (host) extra["7tv"].set(e.name, `https:${host.url}/2x.webp`);
  }
}
function extraEmote(word) {
  return (cfg.emotes_7tv && extra["7tv"].get(word)) || (cfg.emotes_bttv && extra.bttv.get(word)) ||
    (cfg.emotes_ffz && extra.ffz.get(word)) || null;
}

// Badge pictures come through the plugin (Helix needs the streamer's sign-in): {"set/version": url}
let badgeMap = {};
async function loadBadges() {
  badgeMap = {};
  if (!cfg.badges || !cfg.signed_in || !cfg.channel) return;
  try {
    const r = await fetch(api("badges", "&channel=" + encodeURIComponent(cfg.channel)));
    if (r.ok) badgeMap = await r.json();
  } catch {}
}

let pronounNames = null;
const pronounOf = new Map();   // login -> "he/him" | "" | Promise
async function loadPronounNames() {
  pronounNames = {};
  try {
    const r = await fetch("https://api.pronouns.alejo.io/v1/pronouns");
    if (r.ok) pronounNames = await r.json();
  } catch {}
}
function pronounText(p) {
  const a = pronounNames?.[p.pronoun_id], b = p.alt_pronoun_id ? pronounNames?.[p.alt_pronoun_id] : null;
  if (!a) return "";
  if (b) return `${a.subject}/${b.subject}`;
  return a.singular ? a.subject : `${a.subject}/${a.object}`;
}
function pronouns(login, el) {
  if (!cfg.pronouns || !login) return;
  const have = pronounOf.get(login);
  const put = t => { if (t) { el.textContent = t; el.hidden = false; requestAnimationFrame(report); } };
  if (typeof have === "string") return put(have);
  const p = have || fetch("https://api.pronouns.alejo.io/v1/users/" + encodeURIComponent(login))
    .then(r => r.ok ? r.json() : null).then(j => j ? pronounText(j) : "").catch(() => "");
  pronounOf.set(login, p);
  p.then(t => { pronounOf.set(login, t); put(t); });
}

// ------------------------------------------------------------------ Twitch chat

const unesc = v => v.replace(/\\(.)/g, (_, c) => c === "s" ? " " : c === ":" ? ";" : c === "n" ? "\n" : c === "r" ? "\r" : c);
function parse(line) {
  const m = { tags: {}, nick: "", cmd: "", chan: "", text: "" };
  let i = 0;
  if (line[0] === "@") {
    const sp = line.indexOf(" ");
    for (const kv of line.slice(1, sp).split(";")) {
      const eq = kv.indexOf("=");
      if (eq < 0) m.tags[kv] = "";
      else m.tags[kv.slice(0, eq)] = unesc(kv.slice(eq + 1));
    }
    i = sp + 1;
  }
  if (line[i] === ":") {
    const sp = line.indexOf(" ", i);
    m.nick = line.slice(i + 1, sp).split("!")[0];
    i = sp + 1;
  }
  const t = line.indexOf(" :", i);
  const head = (t < 0 ? line.slice(i) : line.slice(i, t)).split(" ");
  m.cmd = head[0];
  m.chan = (head[1] || "").replace(/^#/, "");
  if (t >= 0) m.text = line.slice(t + 2);
  else if (head.length > 2) m.text = head.slice(2).join(" ");
  return m;
}

let ws = null, kws = null, wsGen = 0, testTimer = 0;
// Each platform's state, said together: "Showing x's Twitch chat and y's Kick chat."
const states = {};
function state(pf, text) {
  states[pf] = text;
  status(Object.values(states).filter(Boolean).join(" "));
}
function start() {
  wsGen++;
  for (const s of [ws, kws]) if (s) { s.onclose = null; s.close(); }
  ws = kws = null;
  for (const k of Object.keys(states)) delete states[k];
  clearInterval(testTimer);
  list.replaceChildren();
  if (cfg.test) {
    status("Showing test messages. Turn \"Show test messages\" off to show your chat.");
    runTests();
    return;
  }
  if (!cfg.channel && !cfg.kick) {
    status("No channel yet: type your Twitch or Kick channel above, or sign in on an OBSPETS source.",
      "OBSPETS Chat: no channel yet. Type your channel in its properties.");
    return;
  }
  if (cfg.channel) connect(cfg.channel, wsGen);
  if (cfg.kick) kickConnect(cfg.kick, wsGen);
}

function connect(channel, gen, delay = 1000) {
  const status = text => state("twitch", text);
  status(`Connecting to ${channel}'s Twitch chat…`);
  let pingTimer = 0, history = cfg.history;
  const s = ws = new WebSocket("wss://irc-ws.chat.twitch.tv:443");
  s.onopen = () => {
    s.send("CAP REQ :twitch.tv/tags twitch.tv/commands");
    s.send("NICK justinfan" + (10000 + Math.floor(Math.random() * 80000)));
    s.send("JOIN #" + channel);
  };
  s.onmessage = e => {
    for (const line of e.data.split("\r\n")) {
      if (!line) continue;
      if (line.startsWith("PING")) { s.send("PONG" + line.slice(4)); continue; }
      const m = parse(line);
      if (m.cmd === "001") delay = 1000;
      else if (m.cmd === "JOIN" || m.cmd === "ROOMSTATE") {
        status(`Showing ${channel}'s Twitch chat.`);
        if (m.cmd === "ROOMSTATE" && m.tags["room-id"]) loadExtra(m.tags["room-id"]);
        if (m.cmd === "ROOMSTATE" && history) { history = false; loadHistory(channel, gen); }
      } else if (m.cmd === "RECONNECT") s.close();
      else onMessage(m);
    }
    clearTimeout(pingTimer);
    pingTimer = setTimeout(() => s.close(), 360000);   // Twitch PINGs every ~5 min
  };
  s.onclose = () => {
    clearTimeout(pingTimer);
    if (gen !== wsGen) return;
    status(`Can't reach ${channel}'s Twitch chat right now. Trying again…`);
    setTimeout(() => { if (gen === wsGen) connect(channel, gen, Math.min(delay * 2, 30000)); }, delay);
  };
}

async function loadHistory(channel, gen) {
  try {
    const r = await fetch(`https://recent-messages.robotty.de/api/v2/recent-messages/${encodeURIComponent(channel)}?limit=${cfg.max}`);
    if (!r.ok || gen !== wsGen) return;
    const j = await r.json();
    const now = Date.now();
    for (const line of j.messages || []) {
      const m = parse(line);
      const at = +m.tags["tmi-sent-ts"] || now;
      if (cfg.fade > 0 && now - at > cfg.fade * 1000) continue;
      onMessage(m, true);
    }
  } catch {}
}

// ------------------------------------------------------------------ Kick chat

// Kick's chat comes over the public socket kick.com's own page reads (no sign-in). Each message is
// converted to the shape of a Twitch one, so show() draws both. User ids get a "k" prefix so they
// never clash with Twitch's.
// Fallback Pusher key (Nov 2026). The plugin sends the current one (cfg.kick_key) and, for the
// signed-in channel, its ids (cfg.kick_room), so kick.com needn't be asked.
const KICK_KEY = "34bf7a0ff419a2a775b9", KICK_CLUSTER = "us2";

async function kickInfo(slug) {
  if (cfg.kick_room && cfg.kick_channel_id && slug === cfg.kick)
    return { slug, channelId: cfg.kick_channel_id, chatroomId: cfg.kick_room };
  const get = u => fetch(u, { credentials: "omit" }).then(r => r.ok ? r.json() : null).catch(() => null);
  const c = await get("https://kick.com/api/v2/channels/" + encodeURIComponent(slug));
  return c?.chatroom?.id ? { slug: c.slug, channelId: c.id, chatroomId: c.chatroom.id } : null;
}

// "[emote:123:name] hi" -> text "name hi" and the emote tag "kick_123:0-3"
function kickEmotes(content) {
  const out = [], byId = new Map();
  let at = 0, last = 0, m;
  const re = /\[emote:(\d+):([^\]\s]*)\]/g;
  while ((m = re.exec(content))) {
    const before = content.slice(last, m.index);
    out.push(before); at += Array.from(before).length;
    const name = m[2] || "emote", n = Array.from(name).length;
    byId.set(m[1], [...(byId.get(m[1]) || []), `${at}-${at + n - 1}`]);
    out.push(name); at += n;
    last = re.lastIndex;
  }
  out.push(content.slice(last));
  return { text: out.join(""), tag: [...byId].map(([id, r]) => `kick_${id}:${r.join(",")}`).join("/") };
}

function kickMessage(d, slug) {
  const s = d.sender || {}, id = s.identity || {};
  const { text, tag } = kickEmotes(d.content || "");
  const badges = (id.badges || []).map(b => String(b.type || "").replace(/_/g, "-") + "/" + (+b.count || 1)).filter(b => !b.startsWith("/")).join(",");
  const tags = {
    id: d.id, "user-id": s.id ? "k" + s.id : "", login: (s.slug || s.username || "").toLowerCase(), "display-name": s.username || "",
    color: id.color || "", badges, emotes: tag, "tmi-sent-ts": String(Date.parse(d.created_at) || Date.now()),
  };
  let meta = d.metadata;
  if (typeof meta === "string") { try { meta = JSON.parse(meta); } catch { meta = {}; } }
  if (d.type === "reply" && meta?.original_sender) {
    tags["reply-parent-display-name"] = meta.original_sender.username || "";
    tags["reply-parent-user-login"] = (meta.original_sender.slug || meta.original_sender.username || "").toLowerCase();
    tags["reply-parent-msg-body"] = kickEmotes(meta.original_message?.content || "").text;
  }
  return { platform: "kick", cmd: "PRIVMSG", nick: tags.login, chan: slug, text, tags };
}

function kickEvent(name, d, slug) {
  switch (String(name).replace(/^App\\Events\\/, "")) {
    case "ChatMessageEvent": return kickMessage(d, slug);
    case "MessageDeletedEvent": return { cmd: "CLEARMSG", tags: { "target-msg-id": d.message?.id || d.id || "" } };
    case "ChatroomClearEvent": return { cmd: "CLEARCHAT", tags: {} };
    case "UserBannedEvent": return { cmd: "CLEARCHAT", tags: { "target-user-id": d.user?.id ? "k" + d.user.id : "" } };
  }
  return null;
}

async function kickConnect(slug, gen) {
  const status = text => state("kick", text);
  status(`Connecting to ${slug}'s Kick chat…`);
  let info = null;
  for (let wait = 2000; !info; wait = Math.min(wait * 2, 60000)) {
    info = await kickInfo(slug);
    if (gen !== wsGen) return;
    if (!info) { status(`Can't find ${slug}'s Kick chat right now. Trying again…`); await new Promise(r => setTimeout(r, wait)); }
    if (gen !== wsGen) return;
  }
  let delay = 1000, history = cfg.history;
  const open = () => {
    const s = kws = new WebSocket(`wss://ws-${cfg.kick_cluster || KICK_CLUSTER}.pusher.com/app/${cfg.kick_key || KICK_KEY}?protocol=7&client=js&version=8.4.0&flash=false`);
    let idle = 0, dead = 0, activity = 120;
    const alive = () => {
      clearTimeout(idle); clearTimeout(dead);
      idle = setTimeout(() => { s.send(JSON.stringify({ event: "pusher:ping", data: {} })); dead = setTimeout(() => s.close(), 30000); }, activity * 1000);
    };
    s.onmessage = e => {
      alive();
      let m; try { m = JSON.parse(e.data); } catch { return; }
      if (m.event === "pusher:connection_established") {
        try { activity = JSON.parse(m.data).activity_timeout || activity; } catch {}
        const r = info.chatroomId;
        for (const ch of [`chatrooms.${r}.v2`, `chatroom_${r}`, `chatrooms.${r}`])
          s.send(JSON.stringify({ event: "pusher:subscribe", data: { auth: "", channel: ch } }));
        delay = 1000;
        status(`Showing ${slug}'s Kick chat.`);
        if (history) { history = false; kickHistory(info, gen); }
        return;
      }
      if (m.event === "pusher:ping") { s.send(JSON.stringify({ event: "pusher:pong", data: {} })); return; }
      if (!m.event || m.event.startsWith("pusher")) return;
      let d = m.data;
      if (typeof d === "string") { try { d = JSON.parse(d); } catch { return; } }
      const msg = kickEvent(m.event, d || {}, info.slug);
      if (msg) onMessage(msg);
    };
    s.onclose = () => {
      clearTimeout(idle); clearTimeout(dead);
      if (gen !== wsGen) return;
      status(`Can't reach ${slug}'s Kick chat right now. Trying again…`);
      setTimeout(() => { if (gen === wsGen) open(); }, delay);
      delay = Math.min(delay * 2, 30000);
    };
  };
  open();
}

async function kickHistory(info, gen) {
  try {
    const r = await fetch(`https://kick.com/api/v2/channels/${info.channelId}/messages`, { credentials: "omit" });
    if (!r.ok || gen !== wsGen) return;
    const j = await r.json();
    const now = Date.now();
    for (const d of (j?.data?.messages || []).slice().reverse().slice(-cfg.max)) {
      const m = kickMessage(d, info.slug);
      if (cfg.fade > 0 && now - +m.tags["tmi-sent-ts"] > cfg.fade * 1000) continue;
      onMessage(m, true);
    }
  } catch {}
}

function onMessage(m, old = false) {
  if (m.cmd === "PRIVMSG") show(m, old);
  // (a line Auto mod struck stays for the Pet, though its platform says it's deleted)
  else if (m.cmd === "CLEARMSG") removeWhere(el => el.dataset.id === m.tags["target-msg-id"] && !el.classList.contains("struck"));
  else if (m.cmd === "CLEARCHAT") {
    const uid = m.tags["target-user-id"];
    removeWhere(el => !uid || (el.dataset.uid === uid && !el.classList.contains("struck")));
  }
}
function removeWhere(pred) {
  for (const el of [...list.children]) if (pred(el)) el.remove();
  requestAnimationFrame(report);
}

// ------------------------------------------------------------------ one message

const LINK = /(?:https?:\/\/|www\.)\S+|\b[\w-]+\.(?:com|net|org|tv|gg|io|ly|me|co|xyz|ru|de|uk|app|dev|link|shop|live)\b(?:\/\S*)?/i;

function twitchParts(text, tag) {
  if (!tag) return [{ text }];
  const cp = Array.from(text), spans = [];
  for (const part of tag.split("/")) {
    const [id, ranges] = part.split(":");
    if (!ranges) continue;
    for (const r of ranges.split(",")) {
      const [a, b] = r.split("-").map(Number);
      spans.push({ a, b, id });
    }
  }
  spans.sort((x, y) => x.a - y.a);
  const out = [];
  let at = 0;
  for (const s of spans) {
    if (s.a < at) continue;
    if (s.a > at) out.push({ text: cp.slice(at, s.a).join("") });
    const url = s.id.startsWith("kick_") ? `https://files.kick.com/emotes/${s.id.slice(5)}/fullsize`
      : `https://static-cdn.jtvnw.net/emoticons/v2/${s.id}/default/dark/2.0`;
    out.push({ emote: url, name: cp.slice(s.a, s.b + 1).join("") });
    at = s.b + 1;
  }
  if (at < cp.length) out.push({ text: cp.slice(at).join("") });
  return out;
}

function emoteImg(url, name) {
  const img = document.createElement("img");
  img.className = "emote"; img.crossOrigin = "anonymous"; img.decoding = "async";
  img.src = url; img.alt = name; img.title = name;
  img.onload = () => requestAnimationFrame(report);
  return img;
}

function show(m, old) {
  const t = m.tags, login = (m.nick || "").toLowerCase();
  let text = m.text || "", action = false;
  if (text.startsWith("\x01ACTION ") && text.endsWith("\x01")) { text = text.slice(8, -1); action = true; }
  if (hideUsers.has(login)) return;
  if (cfg.hide_commands && text.startsWith("!")) return;
  if (cfg.hide_links && LINK.test(text)) return;
  if (wordRe && cfg.words_mode !== "mask") { wordRe.lastIndex = 0; if (wordRe.test(text)) return; }

  const badges = t.badges || "";
  const el = document.createElement("div");
  el.className = "msg";
  el.dataset.id = t.id || "";
  el.dataset.uid = t["user-id"] || "";
  el.dataset.login = login;
  const display = t["display-name"] || m.nick || "";
  el.dataset.name = display;
  if (/(^|,)broadcaster\//.test(badges)) el.classList.add("role", "host");
  if (/(^|,)moderator\//.test(badges)) el.classList.add("role", "mod");
  if (/(^|,)vip\//.test(badges)) el.classList.add("role", "vip");
  if (/(^|,)(subscriber|founder)\//.test(badges)) el.classList.add("sub");
  if (action) el.classList.add("action");
  if (cfg.highlights && t["msg-id"] === "highlighted-message") el.classList.add("highlight");
  const color = nameColor(t.color);

  // who it's replying to (Twitch puts "@them " at the start of the text too)
  const replyName = t["reply-parent-display-name"];
  if (replyName) {
    el.classList.add("reply");
    const at = "@" + (t["reply-parent-user-login"] || replyName);
    if (cfg.replies && text.toLowerCase().startsWith(at.toLowerCase() + " ")) text = text.slice(at.length + 1);
    if (cfg.replies) {
      const r = document.createElement("span");
      r.className = "reply-to";
      r.textContent = `↪ @${replyName}: ${t["reply-parent-msg-body"] || ""}`;
      el.append(r);
    }
  }

  const head = document.createElement("span");
  head.className = "head";
  if (cfg.badges) for (const b of badges.split(",").filter(Boolean)) {
    const url = badgeMap[b];
    if (!url) continue;
    const img = document.createElement("img");
    img.className = "badge"; img.crossOrigin = "anonymous"; img.src = url; img.alt = b.split("/")[0];
    head.append(img);
  }
  if (cfg.first && t["first-msg"] === "1") {
    const tag = document.createElement("span");
    tag.className = "tag"; tag.textContent = "first chat";
    head.append(tag);
    el.classList.add("first");
  }
  if (cfg.pronouns) {
    const p = document.createElement("span");
    p.className = "pronouns"; p.hidden = true;
    head.append(p);
    pronouns(login, p);
  }
  const name = document.createElement("span");
  name.className = "name";
  name.textContent = display;
  name.style.color = color;
  head.append(name);
  if (cfg.colon && !action) {
    const c = document.createElement("span");
    c.className = "colon"; c.textContent = ":";
    c.style.color = color;
    head.append(c);
  }

  const body = document.createElement("span");
  body.className = "text";
  if (action && cfg.me_color) body.style.color = color;
  let onlyEmotes = true, any = false;
  for (const part of twitchParts(text, t.emotes)) {
    if (part.emote) { body.append(emoteImg(part.emote, part.name)); any = true; continue; }
    let buf = "";
    for (const w of part.text.split(/(\s+)/)) {
      const url = w && !/^\s+$/.test(w) ? extraEmote(w) : null;
      if (url) { if (buf) body.append(buf); buf = ""; body.append(emoteImg(url, w)); any = true; continue; }
      if (w.trim()) onlyEmotes = false;
      buf += w;
    }
    if (buf) {
      if (wordRe && cfg.words_mode === "mask") buf = buf.replace(wordRe, w => "*".repeat(Math.max(3, [...w].length)));
      body.append(buf);
    }
  }
  if (any && onlyEmotes && cfg.big_emotes) el.classList.add("big");
  if (cfg.mentions && mentionRe && login !== cfg.channel && login !== cfg.kick && mentionRe.test(text)) el.classList.add("mention");

  el.append(head, body);
  add(el, old);
}

// ------------------------------------------------------------------ the list

let pending = [], scheduled = false, seq = 0;
function add(el, old) {
  if (!old) {
    const name = cfg.anim === "none" ? "" : cfg.anim === "slide" && cfg.order === "down" ? "obspet-slide-down" : "obspet-" + cfg.anim;
    const anims = [];
    if (name) anims.push(`${name} var(--in) cubic-bezier(.2,.8,.2,1) both`);
    if (cfg.fade > 0) anims.push(`obspet-out .6s ease ${cfg.fade}s forwards`);
    el.style.animation = anims.join(", ");
  } else if (cfg.fade > 0) {
    el.style.animation = `obspet-out .6s ease ${cfg.fade}s forwards`;
  }
  pending.push(el);
  if (pending.length > cfg.max) pending.shift();
  if (!scheduled) { scheduled = true; requestAnimationFrame(flush); }
}

// New messages go in once per frame. With the newest at the bottom, the list slides up by what was
// added in one animation instead of every line jumping.
function flush() {
  scheduled = false;
  const added = pending;
  pending = [];
  if (!added.length) return;
  if (cfg.order === "down") list.prepend(...added.reverse());
  else list.append(...added);
  while (list.childElementCount > cfg.max) (cfg.order === "down" ? list.lastElementChild : list.firstElementChild).remove();
  seq += added.length;
  if (cfg.order !== "down" && cfg.anim === "slide") {
    let d = 0;
    const gap = parseFloat(getComputedStyle(list).rowGap) || 0;
    for (const el of added) if (el.isConnected) d += el.offsetHeight + gap;
    d = Math.min(d, 400);
    if (d > 0) list.animate([{ transform: `translateY(${d}px)` }, { transform: "none" }], { duration: 260, easing: "cubic-bezier(.2,.8,.2,1)" });
  }
  report();
}
list.addEventListener("animationend", e => {
  if (e.animationName === "obspet-out" && e.target.parentNode === list) { e.target.remove(); report(); }
});
addEventListener("resize", () => report());

// Where the top of the messages is, in px up from the bottom of the page (layout offsets, so the
// slide-in doesn't matter), and how many messages it has shown. Sent when either changes.
let sent = "";
function report() {
  const first = list.firstElementChild;
  let top = 0;
  if (first) top = Math.round(innerHeight - list.offsetTop - (cfg.bg === "panel" || cfg.order === "down" ? 0 : first.offsetTop));
  const key = top + "/" + seq + "/" + innerHeight;
  if (key === sent) return;
  sent = key;
  post({ kind: "layout", top, h: innerHeight, seq });
}

// ------------------------------------------------------------------ the pet yanks a message

const YANK_MAX = 48;   // characters of the message it carries, an emote counting as 2

function lineText(el) {
  const body = el.querySelector(".text");
  return [...(body?.childNodes || [])].map(n => n.nodeType === 3 ? n.textContent : n.alt || "").join("").replace(/\s+/g, " ").trim();
}

// The line as one strip, in the chat's own look (font, colors, outline, box), as base64 PNG. The
// Pet flings this, so what flies out looks like what was in the chat.
function picture(el, strike = false) {
  const st = getComputedStyle(el), nameEl = el.querySelector(".name");
  const size = parseFloat(st.fontSize) || 24;
  const font = `${st.fontStyle} ${st.fontWeight} ${size}px ${st.fontFamily}`;
  const H = Math.ceil(size * 1.7), EH = Math.round(size * 1.35), out = cfg.outline || 0;
  const pad = Math.ceil(out + 6 + (cfg.bg === "box" ? cfg.padding : 0));
  const c = document.createElement("canvas"), g = c.getContext("2d");
  g.font = font;
  const items = [];
  const nameColor = nameEl ? getComputedStyle(nameEl).color : cfg.name_color;
  items.push(["text", el.dataset.name || "", nameColor]);
  items.push(["text", cfg.colon ? ":  " : "  ", nameColor]);
  let left = YANK_MAX;
  const body = el.querySelector(".text"), textColor = body ? getComputedStyle(body).color : cfg.text_color;
  for (const n of body?.childNodes || []) {
    if (left <= 0) break;
    if (n.nodeType === 3) {
      let t = n.textContent.replace(/\s+/g, " ");
      if (t.length > left) { t = t.slice(0, Math.max(0, left - 1)) + "…"; left = 0; } else left -= t.length;
      if (t) items.push(["text", t, textColor]);
    } else if (n.tagName === "IMG") {
      if (left < 2) { items.push(["text", " …", textColor]); left = 0; break; }
      left -= 2;
      if (n.complete && n.naturalWidth) items.push(["img", n]);
      else items.push(["text", n.alt, textColor]);
    }
  }
  let w = pad;
  for (const it of items) {
    it.x = w;
    it.w = it[0] === "img" ? Math.round(EH * it[1].naturalWidth / Math.max(1, it[1].naturalHeight)) + 4 : g.measureText(it[1]).width;
    w += it.w;
  }
  c.width = Math.ceil(w + pad); c.height = H + 2 * Math.max(0, pad - 6);
  const mid = c.height / 2 + 1;
  if (cfg.bg === "box") {
    g.fillStyle = cfg.bg_color;
    g.beginPath();
    g.roundRect ? g.roundRect(0.5, 0.5, c.width - 1, c.height - 1, cfg.radius) : g.rect(0, 0, c.width, c.height);
    g.fill();
  }
  g.font = font; g.textBaseline = "middle"; g.lineJoin = "round";
  if (out > 0 || cfg.shadow) {
    g.save();
    g.lineWidth = out * 2; g.strokeStyle = cfg.outline_color;
    if (cfg.shadow) { g.shadowColor = "rgba(0,0,0,.8)"; g.shadowBlur = 6; g.shadowOffsetY = 2; }
    for (const it of items) if (it[0] === "text") {
      if (out > 0) g.strokeText(it[1], it.x, mid);
      else { g.fillStyle = "rgba(0,0,0,0)"; g.fillText(it[1], it.x, mid); }
    }
    g.restore();
  }
  for (const it of items) {
    if (it[0] === "text") { g.fillStyle = it[2]; g.fillText(it[1], it.x, mid); }
    else g.drawImage(it[1], it.x + 2, mid - EH / 2, it.w - 4, EH);
  }
  // Auto mod deleted it: a line through the message (not the name), like the crossed-out line in the list
  const from = items[2]?.x;
  if (strike && from !== undefined && w > from) {
    const th = Math.max(2, Math.round(size * .09)), y = Math.round(mid - th / 2);
    g.fillStyle = "rgba(0,0,0,.55)";
    g.fillRect(from - 1, y - 1, w - from + 2, th + 2);
    g.fillStyle = STRIKE;
    g.fillRect(from, y, w - from, th);
  }
  try { return c.toDataURL("image/png").split(",")[1] || ""; }
  catch { return ""; }   // an emote host without CORS: the Pet shows the text instead
}

function findLine(id, user) {
  const kids = [...list.children].filter(e => !e.dataset.yanked);
  // With an id, only that message: if it's gone (deleted by a mod, a ban, scrolled off), the Pet
  // must not rip out another message of the same chatter instead.
  if (id) return kids.find(e => e.dataset.id === id) || null;
  const u = (user || "").toLowerCase();
  if (!u) return null;
  const mine = kids.filter(e => e.dataset.login === u || e.dataset.name.toLowerCase() === u);
  return cfg.order === "down" ? mine[0] : mine[mine.length - 1];
}

addEventListener("obspet_yank", e => {
  const { req, id, user, strike } = e.detail || {};
  if (scheduled) flush();   // (a line that came in this frame, like the Pet's test line, is still waiting)
  const el = findLine(id, user);
  if (!el) { post({ kind: "yank", req, found: false }); return; }
  el.dataset.yanked = String(req);
  const r = el.getBoundingClientRect();
  let text = lineText(el);
  if (text.length > YANK_MAX) text = text.slice(0, YANK_MAX - 1) + "…";
  post({
    kind: "yank", req, found: true, text, page_w: innerWidth,
    fx: Math.min(1, Math.max(0, (r.left + r.width / 2) / innerWidth)),
    fy: Math.min(1, Math.max(0, (r.top + r.height / 2) / innerHeight)),
    img: picture(el, !!strike || el.classList.contains("struck")),
  });
  // the Pet may never get to it (an alert came first): then the line just stays (or goes, if it's struck)
  setTimeout(() => { if (el.dataset.yanked === String(req)) letGo(el); }, 25000);
});

// The Pet isn't taking it after all. A struck line is deleted on its platform, so it goes.
function letGo(el) {
  delete el.dataset.yanked;
  if (el.classList.contains("struck") && el.parentNode) { el.remove(); report(); }
}

addEventListener("obspet_yank_done", e => {
  const { req, took } = e.detail || {};
  const el = [...list.children].find(x => x.dataset.yanked === String(req));
  if (!el) return;
  if (took) { el.remove(); report(); }
  else letGo(el);
});

// Auto mod deleted it on its platform: crossed out at once, and kept for the Pet. If no Pet comes
// for it (none acts out moods, or it can't get to the chat), it goes by itself.
const STRIKE = "#e0483c";
const STRUCK_FOR = 20000;
addEventListener("obspet_strike", e => {
  const { id } = e.detail || {};
  const el = id && [...list.children].find(x => x.dataset.id === id);
  if (!el) return;
  el.classList.add("struck");
  setTimeout(() => { if (el.parentNode && !el.dataset.yanked) { el.remove(); report(); } }, STRUCK_FOR);
});

addEventListener("obspet_line", e => {
  const { id, user, text, color } = e.detail || {};
  show({ nick: user, text, tags: { "display-name": user, color, id, badges: "" } }, false);
});

addEventListener("obspet_config", e => { if (e.detail) apply(e.detail); });

// ------------------------------------------------------------------ test messages

function runTests() {
  const me = cfg.channel || cfg.kick || "you";
  const who = [
    ["Nightowl", "#1E90FF", ""], ["pixelpanda", "#FF4500", "subscriber/12"], ["cozy_cactus", "#9ACD32", ""],
    [me, "#B22222", "broadcaster/1"], ["ModMatt", "#8A2BE2", "moderator/1"], ["lurkingLuna", "", "vip/1"],
    ["toastyboi", "#DAA520", "subscriber/3"], ["quietfern", "#2E8B57", ""],
  ];
  const lines = [
    { text: "hello chat!" },
    { text: "Kappa", emotes: "25:0-4" },
    { text: "gg" },
    { text: "how long have you been playing this game? you're getting really good at it" },
    { text: "\x01ACTION grabs a snack and settles in\x01" },
    { text: `hey @${me} how's your day going?` },
    { text: "first time here, hi everyone!", first: true },
    { text: "welcome! enjoy the stream", reply: ["quietfern", "quietfern", "first time here, hi everyone!"] },
    { text: "this message was highlighted with channel points", hl: true },
    { text: "LUL LUL", emotes: "425618:0-2,4-6" },
    { text: "brb getting water" },
    { text: "that was close" },
    { text: "what's the song playing right now?" },
    { text: "a really long message to see how wrapping looks: lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore" },
    { text: "lol" },
    { text: "thanks for the stream, see you next time" },
  ];
  let i = 0;
  const one = () => {
    const [n, c, b] = who[i % who.length], l = lines[i % lines.length];
    const tags = { "display-name": n, color: c, badges: b, emotes: l.emotes || "", id: "t" + i, "user-id": "u" + (i % who.length) };
    let text = l.text;
    if (l.first) tags["first-msg"] = "1";
    if (l.hl) tags["msg-id"] = "highlighted-message";
    if (l.reply) {
      tags["reply-parent-display-name"] = l.reply[0];
      tags["reply-parent-user-login"] = l.reply[1];
      tags["reply-parent-msg-body"] = l.reply[2];
      text = "@" + l.reply[1] + " " + text;
    }
    show({ nick: n.toLowerCase(), text, tags }, false);
    i++;
  };
  for (let k = 0; k < Math.min(4, cfg.max); k++) one();
  testTimer = setInterval(one, 1400);
}

// ------------------------------------------------------------------ go

fetch(api("config")).then(r => r.ok ? r.json() : Promise.reject(r.status)).then(apply).catch(() => {
  note.textContent = "OBSPETS Chat couldn't reach OBS. Restart OBS if this stays.";
});
