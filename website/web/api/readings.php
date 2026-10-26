<?php
// POST: the device sends its readings here.
require __DIR__ . '/db.php';
require __DIR__ . '/lib.php';

const MAX_BODY_BYTES = 2 * 1024 * 1024;
const MAX_READINGS_PER_REQUEST = 10000;

try {
  if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    send_json(405, ['error' => 'use POST']);
  }

  // ----- who is asking? The device sends its key in the X-Device-Key header. -----
  $expected = (string) (config()['device_key'] ?? '');
  if ($expected === '' || str_starts_with($expected, 'TYPE-')) {
    error_log('noise-config.php: device_key is not set');
    send_json(500, ['error' => 'server is not configured']);
  }
  $presented = $_SERVER['HTTP_X_DEVICE_KEY'] ?? '';
  if (!hash_equals($expected, (string) $presented)) {
    send_json(401, ['error' => 'missing or wrong device key']);
  }

  // ----- read the body -----
  $raw = file_get_contents('php://input');
  if (strlen($raw) > MAX_BODY_BYTES) send_json(413, ['error' => 'request body too large']);
  $body = json_decode($raw, true);
  if (json_last_error() !== JSON_ERROR_NONE || !is_array($body) || array_is_list($body)) {
    send_json(400, ['error' => 'body is not valid JSON']);
  }
  if (!isset($body['device_id']) || !is_string($body['device_id'])) {
    send_json(400, ['error' => 'device_id is required']);
  }

  if (isset($body['readings']) && is_array($body['readings']) && array_is_list($body['readings'])) {
    $readings = $body['readings'];
  } elseif (array_key_exists('ts', $body)) {
    $single = $body;                       // a single reading sent without the wrapper
    unset($single['device_id']);
    $readings = [$single];
  } else {
    send_json(400, ['error' => 'send a readings array or a single reading']);
  }
  if (count($readings) > MAX_READINGS_PER_REQUEST) {
    send_json(413, ['error' => 'at most ' . MAX_READINGS_PER_REQUEST . ' readings per request']);
  }

  $result = ingest(pdo(), $body['device_id'], $readings);
  $allBad = $result['accepted'] === 0 && $result['duplicates'] === 0 && count($result['rejected']) > 0;
  send_json($allBad ? 400 : 200, [
    'accepted'   => $result['accepted'],
    'duplicates' => $result['duplicates'],
    'rejected'   => array_slice($result['rejected'], 0, 20),
    'sessions'   => $result['sessions'],
  ]);
} catch (HttpError $e) {
  send_json($e->status, ['error' => $e->getMessage()]);
} catch (Throwable $e) {
  error_log($e->getMessage());
  send_json(500, ['error' => 'server error']);
}