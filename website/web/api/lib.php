<?php
declare(strict_types=1);

// The rules for storing readings: validation, sessions, GPS fill-in, moved flag.
// Ported from the Node version (server/sessions.js). Nothing here touches HTTP.

const SESSION_GAP_MS   = 60 * 1000;   // a gap longer than this starts a new session
const MOVE_THRESHOLD_M = 50;          // placeholder until real GPS drift is measured

class HttpError extends RuntimeException {
  public int $status;
  public function __construct(int $status, string $message) {
    parent::__construct($message);
    $this->status = $status;
  }
}

// Distance in metres between two lat/lon points.
function haversine(float $lat1, float $lon1, float $lat2, float $lon2): float {
  $r = 6371000.0;
  $rad = M_PI / 180;
  $dLat = ($lat2 - $lat1) * $rad;
  $dLon = ($lon2 - $lon1) * $rad;
  $a = sin($dLat / 2) ** 2 + cos($lat1 * $rad) * cos($lat2 * $rad) * sin($dLon / 2) ** 2;
  return 2 * $r * asin(min(1.0, sqrt($a)));
}

function is_number(mixed $v): bool {
  return is_int($v) || is_float($v);
}

// Checks one reading. Returns ['value' => [...]] or ['error' => '...'].
function validate_reading(mixed $r): array {
  if (!is_array($r) || array_is_list($r)) {
    return ['error' => 'reading must be an object'];
  }

  $ts = $r['ts'] ?? null;
  if (!is_string($ts) || !preg_match('/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$/', $ts)) {
    return ['error' => 'ts must be an ISO 8601 string with a timezone, e.g. 2026-10-14T18:00:00Z'];
  }
  $dt = date_create_immutable($ts);
  $problems = DateTimeImmutable::getLastErrors();
  if ($dt === false || ($problems && ($problems['warning_count'] > 0 || $problems['error_count'] > 0))) {
    return ['error' => 'ts is not a valid date'];
  }
  $tsMs = (int) $dt->format('U') * 1000 + intdiv((int) $dt->format('u'), 1000);

  $db = $r['db'] ?? null;
  if (!is_number($db) || $db < 0 || $db > 200) {
    return ['error' => 'db must be a number between 0 and 200'];
  }

  $lat = $r['lat'] ?? null;
  $lon = $r['lon'] ?? null;
  if (($lat === null) !== ($lon === null)) {
    return ['error' => 'lat and lon must both be present or both be absent'];
  }
  if ($lat !== null) {
    if (!is_number($lat) || !is_number($lon) || $lat < -90 || $lat > 90 || $lon < -180 || $lon > 180) {
      return ['error' => 'lat/lon must be numbers within valid ranges'];
    }
    $lat = (float) $lat;
    $lon = (float) $lon;
  }

  if (array_key_exists('clipped', $r) && !is_bool($r['clipped'])) {
    return ['error' => 'clipped must be true or false'];
  }

  return ['value' => [
    'ts_ms'   => $tsMs,
    'db'      => (float) $db,
    'lat'     => $lat,
    'lon'     => $lon,
    'clipped' => ($r['clipped'] ?? false) === true ? 1 : 0,
  ]];
}

// Stores readings from one device, putting each into the right session.
// Returns ['accepted' => n, 'duplicates' => n, 'rejected' => [...], 'sessions' => [ids]].
function ingest(PDO $pdo, string $deviceId, array $readings): array {
  $stmt = $pdo->prepare('SELECT id FROM devices WHERE id = ?');
  $stmt->execute([$deviceId]);
  if ($stmt->fetchColumn() === false) {
    throw new HttpError(404, "unknown device_id: $deviceId");
  }

  $valid = [];
  $rejected = [];
  foreach ($readings as $index => $r) {
    $v = validate_reading($r);
    if (isset($v['error'])) $rejected[] = ['index' => $index, 'reason' => $v['error']];
    else $valid[] = $v['value'];
  }
  usort($valid, fn($a, $b) => $a['ts_ms'] <=> $b['ts_ms']);

  $findSession   = $pdo->prepare('SELECT * FROM sessions WHERE device_id = ? AND started_ms <= ? AND ended_ms >= ? ORDER BY started_ms LIMIT 1');
  $createSession = $pdo->prepare('INSERT INTO sessions (device_id, started_ms, ended_ms) VALUES (?, ?, ?)');
  $exists        = $pdo->prepare('SELECT 1 FROM readings WHERE device_id = ? AND ts_ms = ?');
  $insertReading = $pdo->prepare('INSERT INTO readings (session_id, device_id, ts_ms, level_db, lat, lon, clipped) VALUES (?, ?, ?, ?, ?, ?, ?)');
  $backfill      = $pdo->prepare('UPDATE readings SET lat = ?, lon = ? WHERE session_id = ? AND lat IS NULL');
  $saveSession   = $pdo->prepare('UPDATE sessions SET started_ms = ?, ended_ms = ?, lat = ?, lon = ?, moved = ?, reading_count = ?, clipped_count = ? WHERE id = ?');

  $accepted = 0;
  $duplicates = 0;
  $touched = [];

  $pdo->beginTransaction();
  try {
    foreach ($valid as $r) {
      // A reading joins a session if it is within the gap threshold of that session's span.
      $findSession->execute([$deviceId, $r['ts_ms'] + SESSION_GAP_MS, $r['ts_ms'] - SESSION_GAP_MS]);
      $row = $findSession->fetch();
      if ($row) {
        $s = [
          'id' => (int) $row['id'], 'started_ms' => (int) $row['started_ms'], 'ended_ms' => (int) $row['ended_ms'],
          'lat' => $row['lat'] === null ? null : (float) $row['lat'],
          'lon' => $row['lon'] === null ? null : (float) $row['lon'],
          'moved' => (int) $row['moved'], 'reading_count' => (int) $row['reading_count'],
          'clipped_count' => (int) $row['clipped_count'],
        ];
      } else {
        $createSession->execute([$deviceId, $r['ts_ms'], $r['ts_ms']]);
        $s = [
          'id' => (int) $pdo->lastInsertId(), 'started_ms' => $r['ts_ms'], 'ended_ms' => $r['ts_ms'],
          'lat' => null, 'lon' => null, 'moved' => 0, 'reading_count' => 0, 'clipped_count' => 0,
        ];
      }

      // The same device and time again means a retry: ignore it.
      $exists->execute([$deviceId, $r['ts_ms']]);
      if ($exists->fetchColumn() !== false) {
        $duplicates++;
        continue;
      }
      $insertReading->execute([$s['id'], $deviceId, $r['ts_ms'], $r['db'], $r['lat'], $r['lon'], $r['clipped']]);
      $accepted++;

      $s['started_ms'] = min($s['started_ms'], $r['ts_ms']);
      $s['ended_ms'] = max($s['ended_ms'], $r['ts_ms']);
      $s['reading_count'] += 1;
      $s['clipped_count'] += $r['clipped'];

      if ($r['lat'] !== null) {
        if ($s['lat'] === null) {
          // First fix of the session: it becomes the session location, and any readings
          // taken before the fix get that location too.
          $s['lat'] = $r['lat'];
          $s['lon'] = $r['lon'];
          $backfill->execute([$r['lat'], $r['lon'], $s['id']]);
        } elseif (haversine($s['lat'], $s['lon'], $r['lat'], $r['lon']) > MOVE_THRESHOLD_M) {
          $s['moved'] = 1;
        }
      }

      $saveSession->execute([$s['started_ms'], $s['ended_ms'], $s['lat'], $s['lon'], $s['moved'],
        $s['reading_count'], $s['clipped_count'], $s['id']]);
      $touched[$s['id']] = true;
    }
    $pdo->commit();
  } catch (Throwable $e) {
    $pdo->rollBack();
    throw $e;
  }

  return [
    'accepted' => $accepted,
    'duplicates' => $duplicates,
    'rejected' => $rejected,
    'sessions' => array_keys($touched),
  ];
}