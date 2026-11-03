<?php
// POST api/delete_session.php   body: {"id": 3}   header: X-Admin-Key
// Deletes one session and its readings. Needs the admin key from noise-config.php.
require __DIR__ . '/db.php';
require __DIR__ . '/lib.php';

try {
  if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    send_json(405, ['error' => 'use POST']);
  }

  // ----- who is asking? Not the device: deleting needs its own admin key. -----
  $expected = (string) (config()['admin_key'] ?? '');
  if ($expected === '' || str_starts_with($expected, 'TYPE-')) {
    error_log('noise-config.php: admin_key is not set');
    send_json(500, ['error' => 'deleting is not set up on the server']);
  }
  $presented = $_SERVER['HTTP_X_ADMIN_KEY'] ?? '';
  if (!hash_equals($expected, (string) $presented)) {
    send_json(401, ['error' => 'missing or wrong admin key']);
  }

  // ----- which session? -----
  $raw = file_get_contents('php://input');
  if (strlen($raw) > 1024) send_json(413, ['error' => 'request body too large']);
  $body = json_decode($raw, true);
  if (json_last_error() !== JSON_ERROR_NONE || !is_array($body) || !isset($body['id']) || !is_int($body['id']) || $body['id'] < 1) {
    send_json(400, ['error' => 'send {"id": <session number>}']);
  }

  $removed = delete_session(pdo(), $body['id']);
  if ($removed === null) send_json(404, ['error' => 'session not found']);
  send_json(200, ['deleted_session' => $body['id'], 'deleted_readings' => $removed]);
} catch (Throwable $e) {
  error_log($e->getMessage());
  send_json(500, ['error' => 'server error']);
}