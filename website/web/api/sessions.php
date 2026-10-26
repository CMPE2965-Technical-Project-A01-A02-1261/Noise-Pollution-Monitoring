<?php
// GET api/sessions.php               -> the list of sessions
// GET api/sessions.php?id=3          -> one session with readings
// GET api/sessions.php?id=3&format=csv -> the raw readings as a CSV download
require __DIR__ . '/db.php';
require __DIR__ . '/lib.php';
require __DIR__ . '/summary.php';

try {
  if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    header('Allow: GET');
    send_json(405, ['error' => 'use GET']);
  }

  if (!isset($_GET['id'])) {
    send_json(200, ['sessions' => list_sessions(pdo())]);
  }

  if (!is_string($_GET['id']) || !ctype_digit($_GET['id'])) {
    send_json(400, ['error' => 'id must be a whole number']);
  }
  $id = (int) $_GET['id'];

  if (($_GET['format'] ?? '') === 'csv') {
    $csv = session_csv(pdo(), $id);
    if ($csv === null) send_json(404, ['error' => 'session not found']);
    header('Content-Type: text/csv; charset=utf-8');
    header("Content-Disposition: attachment; filename=\"session-$id.csv\"");
    echo $csv;
    exit;
  }

  $session = get_session(pdo(), $id);
  if ($session === null) send_json(404, ['error' => 'session not found']);
  send_json(200, $session);
} catch (Throwable $e) {
  error_log($e->getMessage());
  send_json(500, ['error' => 'server error']);
}