const SAMPLE_SESSIONS = [
  {
    id: 1, device_id: 'NM-0001',
    started_at: '2026-10-20T18:00:00Z', duration_s: 600,
    location: { lat: 53.5701, lon: -113.5062 },
    summary: { leq: 58.4, lmax: 74.9, lmin: 44.1, l10: 63.0, l50: 55.2, l90: 47.0 },
    moved: false, clipped_count: 0,
  },
  {
    id: 2, device_id: 'NM-0001',
    started_at: '2026-10-20T19:30:00Z', duration_s: 480,
    location: null,
    summary: { leq: 62.1, lmax: 81.3, lmin: 48.0, l10: 67.5, l50: 59.0, l90: 51.2 },
    moved: false, clipped_count: 0,
  },
  {
    id: 3, device_id: 'NM-0001',
    started_at: '2026-10-21T15:10:00Z', duration_s: 360,
    location: { lat: 53.5444, lon: -113.4909 },
    summary: { leq: 71.8, lmax: 96.2, lmin: 52.3, l10: 78.0, l50: 66.5, l90: 56.0 },
    moved: true, clipped_count: 6,
  },
];

// Makes fake one-per-second readings for a session. The real ones will come from the server.
function sampleReadings(session) {
  const start = Date.parse(session.started_at);
  const readings = [];
  for (let i = 0; i < session.duration_s; i++) {
    const wobble = Math.sin(i / 7) * 3 + Math.sin(i / 2.3) * 2;
    const clipped = session.clipped_count > 0 && i % 60 === 30;
    readings.push({
      ts: new Date(start + i * 1000).toISOString(),
      db: clipped ? 96.2 : Math.round((session.summary.l50 + wobble) * 10) / 10,
      clipped,
    });
  }
  return readings;
}