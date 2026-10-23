'use strict';

const app = document.getElementById('app');

// ---------- data (the only part that changes when the server exists) ----------

async function loadSessions() {
  return SAMPLE_SESSIONS;
}

async function loadSession(id) {
  const s = SAMPLE_SESSIONS.find((x) => x.id === id);
  if (!s) throw new Error('session not found');
  return { ...s, readings: sampleReadings(s) };
}

// ---------- functions ----------

function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v);
  for (const kid of kids.flat()) {
    if (kid === null || kid === undefined || kid === false) continue;
    el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  }
  return el;
}

const SVG_NS = 'http://www.w3.org/2000/svg';

// Like h(), but for SVG shapes (they need a special namespace).
function svg(tag, attrs) {
  const el = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v);
  return el;
}

// Replaces the page content, skipping empty values.
function show(...nodes) {
  app.replaceChildren(...nodes.flat().filter((n) => n !== null && n !== undefined && n !== false));
}

function formatDuration(seconds) {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return m > 0 ? `${m} min ${s} s` : `${s} s`;
}

function formatLocation(loc) {
  return loc ? `${loc.lat.toFixed(5)}, ${loc.lon.toFixed(5)}` : 'No GPS';
}

function notes(session) {
  const out = [];
  if (session.moved) out.push(h('span', { class: 'badge' }, 'Moved'));
  if (session.clipped_count > 0) out.push(h('span', { class: 'badge' }, 'Mic maxed out'));
  return out;
}

function warnings(session) {
  const out = [];
  if (!session.location) out.push('No GPS fix was received during this session.');
  if (session.moved) out.push('The device moved during this session, so one location does not describe all of it.');
  if (session.clipped_count > 0) {
    out.push(`The microphone reached its maximum for ${session.clipped_count} readings. Those levels are lower bounds.`);
  }
  return out;
}

function stat(label, value, unit) {
  return h('div', { class: 'stat' },
    h('div', { class: 'stat-label' }, label),
    h('div', { class: 'stat-value' }, value, unit ? h('span', { class: 'unit' }, ` ${unit}`) : null));
}

function buildChart(readings) {
  const W = 800, H = 300;                       // size of the drawing
  const left = 44, right = 12, top = 12, bottom = 28;
  const plotW = W - left - right;
  const plotH = H - top - bottom;

  // Round the vertical range out to whole tens of dB.
  const values = readings.map((r) => r.db);
  const yMin = Math.floor(Math.min(...values) / 10) * 10;
  let yMax = Math.ceil(Math.max(...values) / 10) * 10;
  if (yMax === yMin) yMax += 10;

  // Turn a reading number / a dB value into a position in the drawing.
  const x = (i) => left + (readings.length > 1 ? i / (readings.length - 1) : 0) * plotW;
  const y = (db) => top + (1 - (db - yMin) / (yMax - yMin)) * plotH;

  const chart = svg('svg', {
    viewBox: `0 0 ${W} ${H}`, class: 'chart', role: 'img', 'aria-label': 'Sound level over time',
  });

  // Horizontal grid lines with dB labels
  for (let v = yMin; v <= yMax; v += 10) {
    chart.append(svg('line', { x1: left, x2: W - right, y1: y(v), y2: y(v), class: 'grid' }));
    const label = svg('text', { x: left - 6, y: y(v) + 4, 'text-anchor': 'end', class: 'axis-label' });
    label.textContent = String(v);
    chart.append(label);
  }

  // The level line
  const d = readings.map((r, i) => `${i === 0 ? 'M' : 'L'}${x(i).toFixed(1)},${y(r.db).toFixed(1)}`).join(' ');
  chart.append(svg('path', { d, class: 'line' }));

  // A red dot on every reading where the microphone maxed out
  readings.forEach((r, i) => {
    if (r.clipped) chart.append(svg('circle', { cx: x(i), cy: y(r.db), r: 4, class: 'clip' }));
  });

  // Start and end times under the line
  const timeLabel = (iso, xPos, anchor) => {
    const t = svg('text', { x: xPos, y: H - 8, 'text-anchor': anchor, class: 'axis-label' });
    t.textContent = new Date(iso).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
    chart.append(t);
  };
  timeLabel(readings[0].ts, left, 'start');
  timeLabel(readings[readings.length - 1].ts, W - right, 'end');

  return chart;
}

// ---------- pages ----------

async function showList() {
  const sessions = await loadSessions();
  document.title = 'Noise Monitor';

  const header = h('tr', {},
    h('th', {}, 'Started'), h('th', {}, 'Device'), h('th', {}, 'Duration'),
    h('th', {}, 'Location'), h('th', {}, 'Leq (dB)'), h('th', {}, 'Lmax (dB)'),
    h('th', {}, 'Notes'));

  const rows = sessions.map((s) =>
    h('tr', {},
      h('td', {}, h('a', { href: `#/session/${s.id}` }, new Date(s.started_at).toLocaleString())),
      h('td', {}, s.device_id),
      h('td', {}, formatDuration(s.duration_s)),
      h('td', {}, formatLocation(s.location)),
      h('td', {}, s.summary.leq.toFixed(1)),
      h('td', {}, s.summary.lmax.toFixed(1)),
      h('td', {}, notes(s))));

  show(h('table', {}, h('thead', {}, header), h('tbody', {}, rows)));
}

const RAW_ROWS_SHOWN = 30;

async function showSession(id) {
  const s = await loadSession(id);
  const sum = s.summary;
  document.title = `Session ${s.id} - Noise Monitor`;

  const rawRows = s.readings.slice(0, RAW_ROWS_SHOWN).map((r) =>
    h('tr', {},
      h('td', {}, new Date(r.ts).toLocaleTimeString()),
      h('td', {}, r.db.toFixed(1)),
      h('td', {}, r.clipped ? 'yes' : '')));

  show(
    h('p', {}, h('a', { href: '#/' }, '← All sessions')),
    h('h1', {}, `Session ${s.id}`),
    h('p', { class: 'meta' },
      `${s.device_id} · ${new Date(s.started_at).toLocaleString()} · ${formatDuration(s.duration_s)} · ${formatLocation(s.location)}`),

    warnings(s).map((w) => h('div', { class: 'warning' }, w)),

    h('div', { class: 'stats' },
      stat('Leq', sum.leq.toFixed(1), 'dB'), stat('Lmax', sum.lmax.toFixed(1), 'dB'),
      stat('Lmin', sum.lmin.toFixed(1), 'dB'), stat('L10', sum.l10.toFixed(1), 'dB'),
      stat('L50', sum.l50.toFixed(1), 'dB'), stat('L90', sum.l90.toFixed(1), 'dB'),
      stat('Readings', String(s.readings.length))),
    h('p', { class: 'meta' },
      'Leq is the average level (energy basis). L10, L50 and L90 are the levels exceeded 10%, 50% and 90% of the time.'),

    h('h2', {}, 'Sound level over time (dB)'),
    buildChart(s.readings),

    h('h2', {}, 'Raw data'),
    h('p', { class: 'meta' }, `First ${RAW_ROWS_SHOWN} of ${s.readings.length} readings.`),
    h('table', {},
      h('thead', {}, h('tr', {}, h('th', {}, 'Time'), h('th', {}, 'dB'), h('th', {}, 'Mic maxed out'))),
      h('tbody', {}, rawRows))
  );
}

// ---------- routing: #/ is the list, #/session/3 is session 3 ----------

async function route() {
  try {
    const m = location.hash.match(/^#\/session\/(\d+)$/);
    if (m) await showSession(Number(m[1]));
    else await showList();
  } catch (err) {
    show(h('p', { class: 'error' }, `Could not load data: ${err.message}`));
  }
}

window.addEventListener('hashchange', route);
route();