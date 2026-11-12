/* Sends the one second sound readings to the server over the XBee's cellular
 * connection.
 *
 * Readings are buffered with the HAL tick they were taken at and only sent
 * once the clock has a real UTC time (from GNSS or the phone), so readings
 * taken before that get correct timestamps too.
 *
 * Every UPLINK_INTERVAL_MS the oldest readings go out as one HTTP POST,
 * using XBee API socket frames:
 *   0x40 Socket Create (TCP)        -> 0xC0 response gives the socket ID
 *   0x42 Socket Connect (hostname)  -> 0xC2 response, then 0xCF state
 *                                      "connected" once TCP is up
 *   0x44 Socket Send (request)      -> 0x89 TX status for the last chunk
 *   0xCD Socket Receive (response)  -> look for "200" and "accepted"
 *   0x43 Socket Close               -> 0xC3 response
 * Readings leave the buffer only after the server accepted them; on any
 * failure they are sent again later. The server ignores readings it already
 * has (same device and timestamp), so a resend after a lost response is
 * harmless.
 *
 * Nothing here blocks except the UART writes: Uplink_Process advances the
 * request from the main loop and Uplink_HandleFrame feeds it the responses. */

#include "uplink.h"

#include "clock.h"
#include "device_secrets.h"
#include "message.h"

#include <stdio.h>
#include <string.h>

/* Readings held while waiting for a time or a connection: 30 minutes. */
#define UPLINK_BUFFER_SIZE 1800u
/* Readings per request. Also how far a backlog is worked off per request. */
#define UPLINK_MAX_BATCH 60u
/* Longest JSON for one reading, plus the wrapper. */
#define UPLINK_READING_MAX_JSON 112u
#define UPLINK_BODY_SIZE (64u + UPLINK_MAX_BATCH * UPLINK_READING_MAX_JSON)
/* Socket Send payload per API frame (frame length is limited to 255). */
#define UPLINK_CHUNK 240u

/* Give up on a request after this long (LTE-M connections are slow). */
#define UPLINK_REQUEST_TIMEOUT_MS 60000u
#define UPLINK_CLOSE_TIMEOUT_MS 5000u
/* Wait before trying again after a failure. */
#define UPLINK_RETRY_MS 30000u

/* XBee API frame types and values used here. */
#define FRAME_SOCKET_CREATE 0x40
#define FRAME_SOCKET_CONNECT 0x42
#define FRAME_SOCKET_CLOSE 0x43
#define FRAME_SOCKET_SEND 0x44
#define FRAME_TX_STATUS 0x89
#define FRAME_SOCKET_CREATE_RESP 0xC0
#define FRAME_SOCKET_CONNECT_RESP 0xC2
#define FRAME_SOCKET_CLOSE_RESP 0xC3
#define FRAME_SOCKET_RECEIVE 0xCD
#define FRAME_SOCKET_STATE 0xCF
#define SOCKET_PROTOCOL_TCP 0x01
#define SOCKET_ADDR_HOSTNAME 0x01
#define SOCKET_STATE_CONNECTED 0x00
#define SOCKET_STATE_TRANSPORT_CLOSED 0x03

#define READING_CLIPPED 0x01u
#define READING_HAS_POS 0x02u

typedef struct {
	uint32_t tick;
	int32_t lat_e7;
	int32_t lon_e7;
	int16_t db_x10;
	uint8_t flags;
} Uplink_Reading;

typedef enum {
	UPLINK_IDLE,
	UPLINK_CREATING,     /* waiting for 0xC0 */
	UPLINK_CONNECTING,   /* waiting for 0xC2 */
	UPLINK_HANDSHAKE,    /* waiting for 0xCF connected */
	UPLINK_RESPONSE,     /* request sent, reading the reply */
	UPLINK_CLOSING       /* waiting for 0xC3 */
} Uplink_State;

static Uplink_Reading readings[UPLINK_BUFFER_SIZE];
static uint32_t readings_tail;  /* oldest unsent */
static uint32_t readings_count;
static uint32_t dropped_count;
static uint32_t inflight;       /* readings from the tail in the current request */

static Uplink_State state;
static uint8_t socket_id;
static uint8_t pending_frame_id;
static uint8_t send_frame_id;
static uint32_t request_start;
static uint32_t close_start;
static uint32_t next_attempt;
static bool result_ok;

/* Response matching, across Socket Receive frames. */
static char status_line[13];
static uint8_t status_line_len;
static uint8_t accepted_match;
static bool accepted_found;

static char body[UPLINK_BODY_SIZE];

void Uplink_AddReading(const Sound_Reading *reading)
{
	if (!reading->valid) {
		return;
	}
	if (readings_count >= UPLINK_BUFFER_SIZE) {
		dropped_count++;
		return;
	}

	Uplink_Reading *r = &readings[(readings_tail + readings_count) % UPLINK_BUFFER_SIZE];
	float db = reading->db;
	if (db < 0.0f) db = 0.0f;
	if (db > 200.0f) db = 200.0f;
	r->tick = reading->tick;
	r->db_x10 = (int16_t)(db * 10.0f + 0.5f);
	r->flags = reading->clipped ? READING_CLIPPED : 0u;
	if (LatSet && LngSet) {
		r->lat_e7 = LatitudeE7;
		r->lon_e7 = LongitudeE7;
		r->flags |= READING_HAS_POS;
	}
	readings_count++;
}

uint32_t Uplink_GetPendingCount(void)
{
	return readings_count;
}

uint32_t Uplink_GetDroppedCount(void)
{
	return dropped_count;
}

bool Uplink_IsIdle(void)
{
	return state == UPLINK_IDLE;
}

/* Degrees * 10^7 as a JSON number with 7 decimals. */
static int FormatE7(char *buf, size_t size, int32_t v)
{
	uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
	return snprintf(buf, size, "%s%lu.%07lu", v < 0 ? "-" : "",
	                (unsigned long)(a / 10000000u), (unsigned long)(a % 10000000u));
}

/* Builds the JSON body from the oldest readings. Returns its length and sets
 * inflight to the number of readings included. */
static size_t BuildBody(void)
{
	size_t len = (size_t)snprintf(body, sizeof(body),
	                              "{\"device_id\":\"%s\",\"readings\":[", UPLINK_DEVICE_ID);
	uint32_t n = 0;
	while (n < readings_count && n < UPLINK_MAX_BATCH &&
	       len + UPLINK_READING_MAX_JSON + 3u < sizeof(body)) {
		const Uplink_Reading *r = &readings[(readings_tail + n) % UPLINK_BUFFER_SIZE];
		int64_t unix_ms;
		char ts[32];
		if (!Clock_TickToUnixMs(r->tick, &unix_ms) ||
		    Clock_FormatIso(unix_ms, ts, sizeof(ts)) == 0) {
			break;
		}

		char *p = body + len;
		size_t room = sizeof(body) - len;
		int w = snprintf(p, room, "%s{\"ts\":\"%s\",\"db\":%d.%d",
		                 n > 0 ? "," : "", ts, r->db_x10 / 10, r->db_x10 % 10);
		if (r->flags & READING_HAS_POS) {
			w += snprintf(p + w, room - (size_t)w, ",\"lat\":");
			w += FormatE7(p + w, room - (size_t)w, r->lat_e7);
			w += snprintf(p + w, room - (size_t)w, ",\"lon\":");
			w += FormatE7(p + w, room - (size_t)w, r->lon_e7);
		}
		w += snprintf(p + w, room - (size_t)w, ",\"clipped\":%s}",
		              (r->flags & READING_CLIPPED) ? "true" : "false");
		len += (size_t)w;
		n++;
	}
	len += (size_t)snprintf(body + len, sizeof(body) - len, "]}");
	inflight = n;
	return len;
}

/* Sends bytes on the socket as one or more Socket Send frames. Only the last
 * frame asks for a TX status, so the status queue is not flooded. */
static bool SocketSend(const char *data, size_t length, bool last)
{
	uint8_t frame[4 + UPLINK_CHUNK];
	while (length > 0u) {
		size_t chunk = length > UPLINK_CHUNK ? UPLINK_CHUNK : length;
		bool final = last && chunk == length;
		frame[0] = FRAME_SOCKET_SEND;
		frame[1] = final ? API_GetNextFrameId() : 0u;
		frame[2] = socket_id;
		frame[3] = 0x00; /* transmit options */
		memcpy(&frame[4], data, chunk);
		if (final) {
			send_frame_id = frame[1];
		}
		if (!Frame(frame, (uint8_t)(4u + chunk))) {
			return false;
		}
		data += chunk;
		length -= chunk;
	}
	return true;
}

static bool SendRequest(void)
{
	size_t body_len = BuildBody();
	if (inflight == 0u) {
		return false;
	}
	char header[256];
	int header_len = snprintf(header, sizeof(header),
	                          "POST %s HTTP/1.1\r\n"
	                          "Host: %s\r\n"
	                          "X-Device-Key: %s\r\n"
	                          "Content-Type: application/json\r\n"
	                          "Content-Length: %u\r\n"
	                          "Connection: close\r\n"
	                          "\r\n",
	                          UPLINK_PATH, UPLINK_HOST, UPLINK_DEVICE_KEY,
	                          (unsigned)body_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		return false;
	}
	return SocketSend(header, (size_t)header_len, false) &&
	       SocketSend(body, body_len, true);
}

static void StartClose(void)
{
	uint8_t frame[3];
	frame[0] = FRAME_SOCKET_CLOSE;
	frame[1] = API_GetNextFrameId();
	frame[2] = socket_id;
	pending_frame_id = frame[1];
	close_start = HAL_GetTick();
	state = UPLINK_CLOSING;
	Frame(frame, sizeof(frame));
}

/* Ends the request. Readings are removed only when the server accepted
 * them. */
static void Finish(void)
{
	uint32_t now = HAL_GetTick();
	if (result_ok) {
		readings_tail = (readings_tail + inflight) % UPLINK_BUFFER_SIZE;
		readings_count -= inflight;
		/* Work off a backlog right away, otherwise wait for the interval. */
		next_attempt = readings_count >= UPLINK_MAX_BATCH ? now
		                                                  : request_start + UPLINK_INTERVAL_MS;
	} else {
		next_attempt = now + UPLINK_RETRY_MS;
	}
	inflight = 0;
	state = UPLINK_IDLE;
}

/* Abandons the request; closes the socket if one was created. */
static void Fail(void)
{
	result_ok = false;
	if (state == UPLINK_IDLE || state == UPLINK_CREATING) {
		Finish();
	} else if (state != UPLINK_CLOSING) {
		StartClose();
	}
}

static void StartRequest(void)
{
	uint8_t frame[3];
	frame[0] = FRAME_SOCKET_CREATE;
	frame[1] = API_GetNextFrameId();
	frame[2] = SOCKET_PROTOCOL_TCP;
	pending_frame_id = frame[1];
	request_start = HAL_GetTick();
	result_ok = false;
	inflight = 0;
	status_line_len = 0;
	accepted_match = 0;
	accepted_found = false;
	state = UPLINK_CREATING;
	if (!Frame(frame, sizeof(frame))) {
		Fail();
	}
}

void Uplink_Process(void)
{
	uint32_t now = HAL_GetTick();
	switch (state) {
	case UPLINK_IDLE:
		/* GNSS takes the XBee off the cellular network while it searches. */
		if (readings_count > 0u && Clock_IsSet() && !GNSSPending &&
		    (int32_t)(now - next_attempt) >= 0) {
			StartRequest();
		}
		break;
	case UPLINK_CLOSING:
		if (now - close_start > UPLINK_CLOSE_TIMEOUT_MS) {
			Finish();
		}
		break;
	default:
		if (now - request_start > UPLINK_REQUEST_TIMEOUT_MS) {
			Fail();
		}
		break;
	}
}

/* Watches the response stream for the status line and "accepted". */
static void ReadResponse(const char *data, uint16_t length)
{
	static const char needle[] = "\"accepted\"";
	for (uint16_t i = 0; i < length; i++) {
		char c = data[i];
		if (status_line_len < sizeof(status_line) - 1u) {
			status_line[status_line_len++] = c;
			status_line[status_line_len] = '\0';
		}
		if (!accepted_found) {
			if (c == needle[accepted_match]) {
				if (++accepted_match == sizeof(needle) - 1u) {
					accepted_found = true;
				}
			} else {
				accepted_match = (c == needle[0]) ? 1u : 0u;
			}
		}
	}
}

/* "HTTP/1.1 200" (or 1.0). */
static bool ResponseIsOk(void)
{
	return status_line_len >= 12u && strncmp(status_line, "HTTP/1.", 7) == 0 &&
	       strncmp(status_line + 8, " 200", 4) == 0;
}

static void SendConnect(void)
{
	uint8_t frame[6 + sizeof(UPLINK_HOST)];
	frame[0] = FRAME_SOCKET_CONNECT;
	frame[1] = API_GetNextFrameId();
	frame[2] = socket_id;
	frame[3] = (uint8_t)(UPLINK_PORT >> 8);
	frame[4] = (uint8_t)(UPLINK_PORT & 0xFFu);
	frame[5] = SOCKET_ADDR_HOSTNAME;
	memcpy(&frame[6], UPLINK_HOST, sizeof(UPLINK_HOST) - 1u);
	pending_frame_id = frame[1];
	state = UPLINK_CONNECTING;
	if (!Frame(frame, (uint8_t)(6u + sizeof(UPLINK_HOST) - 1u))) {
		Fail();
	}
}

void Uplink_HandleFrame(const API_Status *status)
{
	const uint8_t *d = (const uint8_t *)status->data;
	uint16_t len = status->data_length;

	switch (status->frame_type) {
	case FRAME_SOCKET_CREATE_RESP: /* socket ID, status */
		if (state == UPLINK_CREATING && status->frame_id == pending_frame_id && len >= 2u) {
			if (d[1] == 0x00) {
				socket_id = d[0];
				SendConnect();
			} else {
				/* e.g. 0x22: not registered to the cell network yet */
				Fail();
			}
		}
		break;

	case FRAME_SOCKET_CONNECT_RESP: /* socket ID, status */
		if (state == UPLINK_CONNECTING && status->frame_id == pending_frame_id &&
		    len >= 2u && d[0] == socket_id) {
			if (d[1] == 0x00) {
				state = UPLINK_HANDSHAKE; /* connection started */
			} else {
				Fail();
			}
		}
		break;

	case FRAME_SOCKET_STATE: /* socket ID, state (no frame ID) */
		if (len < 2u || d[0] != socket_id) {
			break;
		}
		if ((state == UPLINK_CONNECTING || state == UPLINK_HANDSHAKE) &&
		    d[1] == SOCKET_STATE_CONNECTED) {
			state = UPLINK_RESPONSE;
			if (!SendRequest()) {
				Fail();
			}
		} else if (state == UPLINK_RESPONSE && d[1] == SOCKET_STATE_TRANSPORT_CLOSED) {
			/* Server finished its reply (Connection: close). */
			result_ok = ResponseIsOk() && accepted_found;
			StartClose();
		} else if (state != UPLINK_IDLE && state != UPLINK_CLOSING &&
		           d[1] != SOCKET_STATE_CONNECTED) {
			Fail(); /* DNS failure, refused, timed out, lost, ... */
		}
		break;

	case FRAME_SOCKET_RECEIVE: /* socket ID, status, payload */
		if (state == UPLINK_RESPONSE && len >= 2u && d[0] == socket_id) {
			ReadResponse(status->data + 2, (uint16_t)(len - 2u));
			if (ResponseIsOk() && accepted_found) {
				result_ok = true;
				StartClose();
			}
		}
		break;

	case FRAME_TX_STATUS: /* status */
		if (state == UPLINK_RESPONSE && status->frame_id == send_frame_id &&
		    len >= 1u && d[0] != 0x00) {
			Fail();
		}
		break;

	case FRAME_SOCKET_CLOSE_RESP: /* socket ID, status (any status ends it) */
		if (state == UPLINK_CLOSING && status->frame_id == pending_frame_id) {
			Finish();
		}
		break;

	default:
		break;
	}
}
