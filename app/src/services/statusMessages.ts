/**
 * Parses the text status messages the device sends over BLE (see
 * documents/BluetoothAPI.md). Accepts both the documented form ("BATT 85")
 * and what the firmware currently sends ("BATT: %85").
 */

export interface DeviceStatus {
    /** Battery state of charge, 0-100. */
    battery?: number;
    /** Number of cellular signal bars. */
    cellBars?: number;
    /** Number of GNSS satellites providing a lock. */
    satellites?: number;
    /** Recording duration as HH:MM:SS, or null when the device is not recording. */
    recordingTime?: string | null;
}

const STATUS_PATTERN = /^\s*(BATT|CELL|GNSS|TIME)\s*:?\s*(.*?)\s*$/i;

function parseCount(value: string): number | undefined {
    const match = /\d+/.exec(value);
    return match ? Number.parseInt(match[0], 10) : undefined;
}

/** Returns the status fields contained in a message, or null if it is not a status message. */
export function parseStatusMessage(message: string): Partial<DeviceStatus> | null {
    const update: Partial<DeviceStatus> = {};

    for (const line of message.split(/[\r\n]+/)) {
        const match = STATUS_PATTERN.exec(line);
        if (!match) {
            continue;
        }
        const value = match[2];

        switch (match[1].toUpperCase()) {
            case 'BATT': {
                const battery = parseCount(value);
                if (battery !== undefined) {
                    update.battery = Math.min(100, battery);
                }
                break;
            }
            case 'CELL': {
                const bars = parseCount(value);
                if (bars !== undefined) {
                    update.cellBars = bars;
                }
                break;
            }
            case 'GNSS': {
                const satellites = parseCount(value);
                if (satellites !== undefined) {
                    update.satellites = satellites;
                }
                break;
            }
            case 'TIME':
                update.recordingTime = /^0*$/.test(value.replace(/[:\s]/g, '')) ? null : value;
                break;
        }
    }

    return Object.keys(update).length > 0 ? update : null;
}
