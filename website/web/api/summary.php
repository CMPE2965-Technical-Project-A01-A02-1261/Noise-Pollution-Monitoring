<?php
declare(strict_types=1);

// Reading data back out: summaries (Leq, L10/L50/L90...), per-minute values, sessions as JSON.
// Ported from the Node version (server/sessions.js).

function iso_ms(int $ms): string {
  return gmdate('Y-m-d\TH:i:s', intdiv($ms, 1000)) . sprintf('.%03dZ', $ms % 1000);
}

function leq(array $values): float {
  $sum = 0.0;
  foreach ($values as $v) $sum += 10 ** ($v / 10);      // dB -> energy
  return 10 * log10($sum / count($values));              // average energy -> dB
}

// Level exceeded for $pctTime percent of the time (L10 = exceeded 10% of the time).
function exceeded(array $sortedAsc, int $pctTime): float {
  $n = count($sortedAsc);
  $p = 100 - $pctTime;
  $idx = min($n - 1, max(0, (int) ceil(($p / 100) * $n) - 1));
  return $sortedAsc[$idx];
}

// $rows: [['ts_ms' => int, 'db' => float], ...]. Returns null when there is nothing to summarise.
function summarize(array $rows): ?array {
  if (count($rows) === 0) return null;
  $values = array_column($rows, 'db');
  $sorted = $values;
  sort($sorted);

  $minutes = [];
  foreach ($rows as $r) {
    $key = intdiv($r['ts_ms'], 60000) * 60000;
    $minutes[$key][] = $r['db'];
  }
  ksort($minutes);
  $perMinute = [];
  foreach ($minutes as $minuteMs => $vals) {
    $perMinute[] = [
      'minute_start' => iso_ms($minuteMs),
      'leq' => round(leq($vals), 1),
      'lmax' => round(max($vals), 1),
      'n' => count($vals),
    ];
  }

  return [
    'leq' => round(leq($values), 1),
    'lmax' => round($sorted[count($sorted) - 1], 1),
    'lmin' => round($sorted[0], 1),
    'l10' => round(exceeded($sorted, 10), 1),
    'l50' => round(exceeded($sorted, 50), 1),
    'l90' => round(exceeded($sorted, 90), 1),
    'perMinute' => $perMinute,
  ];
}

function warnings_for(array $s): array {
  $w = [];
  if ((int) $s['moved'] === 1) {
    $w[] = 'The device appears to have been moved during this recording (position changed by more than ' . MOVE_THRESHOLD_M . ' m). The data may not be accurate.';
  }
  $clipped = (int) $s['clipped_count'];
  if ($clipped > 0) {
    $w[] = "$clipped reading" . ($clipped === 1 ? '' : 's') . " hit the microphone's maximum, so the real level at those moments was at least the value shown.";
  }
  if ($s['lat'] === null) $w[] = 'No GPS position was received during this session.';
  return $w;
}

function session_json(array $s, ?array $summary): array {
  $started = (int) $s['started_ms'];
  $ended = (int) $s['ended_ms'];
  return [
    'id' => (int) $s['id'],
    'device_id' => $s['device_id'],
    'started_at' => iso_ms($started),
    'ended_at' => iso_ms($ended),
    'duration_s' => (int) round(($ended - $started) / 1000),
    'location' => $s['lat'] === null ? null : ['lat' => (float) $s['lat'], 'lon' => (float) $s['lon']],
    'moved' => (int) $s['moved'] === 1,
    'reading_count' => (int) $s['reading_count'],
    'clipped_count' => (int) $s['clipped_count'],
    'warnings' => warnings_for($s),
    'summary' => $summary,
  ];
}

function level_rows(PDO $pdo, int $sessionId): array {
  $stmt = $pdo->prepare('SELECT ts_ms, level_db FROM readings WHERE session_id = ? ORDER BY ts_ms');
  $stmt->execute([$sessionId]);
  return array_map(fn($r) => ['ts_ms' => (int) $r['ts_ms'], 'db' => (float) $r['level_db']], $stmt->fetchAll());
}

// All sessions, newest first, each with its headline numbers.
function list_sessions(PDO $pdo): array {
  $out = [];
  foreach ($pdo->query('SELECT * FROM sessions ORDER BY started_ms DESC')->fetchAll() as $s) {
    $summary = summarize(level_rows($pdo, (int) $s['id']));
    if ($summary) unset($summary['perMinute']);          // the list only needs the headline numbers
    $out[] = session_json($s, $summary);
  }
  return $out;
}

// One session with its summary, per-minute values and every raw reading. Null if it does not exist.
function get_session(PDO $pdo, int $id): ?array {
  $stmt = $pdo->prepare('SELECT * FROM sessions WHERE id = ?');
  $stmt->execute([$id]);
  $s = $stmt->fetch();
  if (!$s) return null;

  $stmt = $pdo->prepare('SELECT ts_ms, level_db, lat, lon, clipped FROM readings WHERE session_id = ? ORDER BY ts_ms');
  $stmt->execute([$id]);
  $rows = $stmt->fetchAll();

  $summary = summarize(array_map(fn($r) => ['ts_ms' => (int) $r['ts_ms'], 'db' => (float) $r['level_db']], $rows));
  $perMinute = $summary ? $summary['perMinute'] : [];
  if ($summary) unset($summary['perMinute']);

  return session_json($s, $summary) + [
    'per_minute' => $perMinute,
    'readings' => array_map(fn($r) => [
      'ts' => iso_ms((int) $r['ts_ms']),
      'db' => (float) $r['level_db'],
      'lat' => $r['lat'] === null ? null : (float) $r['lat'],
      'lon' => $r['lon'] === null ? null : (float) $r['lon'],
      'clipped' => (int) $r['clipped'] === 1,
    ], $rows),
  ];
}

// The raw readings as CSV text. Null if the session does not exist.
function session_csv(PDO $pdo, int $id): ?string {
  $stmt = $pdo->prepare('SELECT id FROM sessions WHERE id = ?');
  $stmt->execute([$id]);
  if ($stmt->fetchColumn() === false) return null;

  $stmt = $pdo->prepare('SELECT ts_ms, level_db, lat, lon, clipped FROM readings WHERE session_id = ? ORDER BY ts_ms');
  $stmt->execute([$id]);
  $lines = ['timestamp_utc,db,latitude,longitude,clipped'];
  foreach ($stmt->fetchAll() as $r) {
    $lines[] = implode(',', [
      iso_ms((int) $r['ts_ms']), (float) $r['level_db'],
      $r['lat'] ?? '', $r['lon'] ?? '', (int) $r['clipped'] === 1 ? 'true' : 'false',
    ]);
  }
  return implode("\r\n", $lines) . "\r\n";
}