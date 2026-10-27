<?php
require __DIR__ . '/db.php';

try {
  $devices = (int) pdo()->query('SELECT COUNT(*) FROM devices')->fetchColumn();
  send_json(200, ['ok' => true, 'devices' => $devices]);
} catch (Throwable $e) {
  error_log($e->getMessage());                       // details go to the server log only
  send_json(500, ['ok' => false, 'error' => 'database unavailable']);
}