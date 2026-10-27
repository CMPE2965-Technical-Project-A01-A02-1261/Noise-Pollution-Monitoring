<?php
// Shared functions for every endpoint.

function config(): array {
  static $c = null;
  if ($c === null) {
    $c = require __DIR__ . '/../../../noise-config.php';   // home folder, above public_html
  }
  return $c;
}

function pdo(): PDO {
  static $pdo = null;
  if ($pdo === null) {
    $c = config();
    $pdo = new PDO(
      "mysql:host={$c['db_host']};dbname={$c['db_name']};charset=utf8mb4",
      $c['db_user'],
      $c['db_pass'],
      [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
      ]
    );
  }
  return $pdo;
}

function send_json(int $status, $body): void {
  http_response_code($status);
  header('Content-Type: application/json; charset=utf-8');
  echo json_encode($body);
  exit;
}