import 'react-native-get-random-values';
import { ctr } from '@noble/ciphers/aes.js';
import { Buffer } from 'buffer';
import { PermissionsAndroid, Platform } from 'react-native';
import {
    BleManager as NativeBleManager,
    State as BleState,
    type Device as BleDevice,
    type Subscription as BleSubscription,
} from 'react-native-ble-plx';
import * as srpClient from '@kapetan/secure-remote-password/client';
import srpParameters from '@kapetan/secure-remote-password/parameters';
import * as xbeeFrame from 'xbee-frame';

const XBEE_SERVICE_UUID = '53da53b9-0447-425a-b9ea-9837505eb59a';
const XBEE_REQUEST_CHARACTERISTIC_UUID = '7dddca00-3e05-4651-9254-44074792c590';
const XBEE_RESPONSE_CHARACTERISTIC_UUID = 'f9279ee9-2cd0-410c-81cc-adf11e4e5aea';
const BLE_USERNAME = 'apiservice';
const SCAN_TIMEOUT_MS = 15_000;
const BLUETOOTH_STATE_TIMEOUT_MS = 10_000;
const FRAME_TIMEOUT_MS = 10_000;
const MAX_FRAME_LENGTH = 0xffff + 4;

type ParsedXBeeFrame = xbeeFrame.DecodedFrame;

class CtrStream {
    private readonly counter: Uint8Array;
    private blockOffset = 16;
    private keyStream: Uint8Array<ArrayBufferLike> = new Uint8Array(16);

    constructor(key: Uint8Array, nonce: Uint8Array) {
        this.counter = new Uint8Array(16);
        this.counter.set(nonce);
        this.counter[15] = 1;
        this.key = key;
    }

    private readonly key: Uint8Array;

    transform(input: Uint8Array): Uint8Array {
        const output = new Uint8Array(input.length);

        for (let index = 0; index < input.length; index += 1) {
            if (this.blockOffset === 16) {
                this.keyStream = ctr(this.key, this.counter).encrypt(new Uint8Array(16));
                this.incrementCounter();
                this.blockOffset = 0;
            }
            output[index] = input[index] ^ this.keyStream[this.blockOffset];
            this.blockOffset += 1;
        }

        return output;
    }

    private incrementCounter(): void {
        for (let index = this.counter.length - 1; index >= 12; index -= 1) {
            this.counter[index] = (this.counter[index] + 1) & 0xff;
            if (this.counter[index] !== 0) {
                break;
            }
        }
    }
}

interface FrameWaiter {
    resolve: (frame: ParsedXBeeFrame) => void;
    reject: (error: Error) => void;
    timer: ReturnType<typeof setTimeout>;
}

class FrameReader {
    private buffer: Uint8Array<ArrayBufferLike> = new Uint8Array();
    private readonly frames: ParsedXBeeFrame[] = [];
    private readonly waiters: FrameWaiter[] = [];
    private failure: Error | undefined;
    private decryptor: CtrStream | undefined;
    private streaming = false;

    constructor(private readonly onFrame: (frame: ParsedXBeeFrame) => void) { }

    get hasFailed(): boolean {
        return this.failure !== undefined;
    }

    setDecryptor(decryptor: CtrStream): void {
        this.decryptor = decryptor;
    }

    startStreaming(): void {
        this.streaming = true;
        this.frames.length = 0;
    }

    push(bytes: Uint8Array): void {
        if (this.failure) {
            return;
        }

        const plaintext = this.decryptor ? this.decryptor.transform(bytes) : bytes;
        this.buffer = concatBytes(this.buffer, plaintext);

        try {
            while (this.buffer.length >= 3) {
                if (this.buffer[0] !== 0x7e) {
                    throw new Error('XBee BLE frame is missing its start delimiter');
                }

                const totalLength = ((this.buffer[1] << 8) | this.buffer[2]) + 4;
                if (totalLength > MAX_FRAME_LENGTH) {
                    throw new Error('XBee BLE frame exceeds the maximum API frame size');
                }
                if (this.buffer.length < totalLength) {
                    break;
                }

                const frameBytes = this.buffer.slice(0, totalLength);
                this.buffer = this.buffer.slice(totalLength);
                const frame = xbeeFrame.decode(frameBytes);
                this.onFrame(frame);

                if (!this.streaming) {
                    const waiter = this.waiters.shift();
                    if (waiter) {
                        clearTimeout(waiter.timer);
                        waiter.resolve(frame);
                    } else {
                        this.frames.push(frame);
                    }
                }
            }
        } catch (error) {
            this.fail(asError(error));
            throw error;
        }
    }

    nextFrame(): Promise<ParsedXBeeFrame> {
        if (this.failure) {
            return Promise.reject(this.failure);
        }
        const frame = this.frames.shift();
        if (frame) {
            return Promise.resolve(frame);
        }

        return new Promise((resolve, reject) => {
            const waiter: FrameWaiter = {
                resolve,
                reject,
                timer: setTimeout(() => {
                    const index = this.waiters.indexOf(waiter);
                    if (index >= 0) {
                        this.waiters.splice(index, 1);
                    }
                    reject(new Error('Timed out waiting for an XBee BLE API frame'));
                }, FRAME_TIMEOUT_MS),
            };
            this.waiters.push(waiter);
        });
    }

    fail(error: Error): void {
        if (this.failure) {
            return;
        }
        this.failure = error;
        for (const waiter of this.waiters.splice(0)) {
            clearTimeout(waiter.timer);
            waiter.reject(error);
        }
    }
}

interface XBeeSession {
    device: BleDevice;
    subscription: BleSubscription;
    reader: FrameReader;
    txCipher: CtrStream;
}

let manager: NativeBleManager | undefined;
let activeSession: XBeeSession | undefined;
let nextFrameId = 1;

function getManager(): NativeBleManager {
    if (!manager) {
        manager = new NativeBleManager();
    }
    return manager;
}

function concatBytes(
    first: Uint8Array,
    second: Uint8Array
): Uint8Array<ArrayBufferLike> {
    const result = new Uint8Array(first.length + second.length);
    result.set(first);
    result.set(second, first.length);
    return result;
}

function bytesFromHex(hex: string): Uint8Array {
    const bytes = new Uint8Array(hex.length / 2);
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = Number.parseInt(hex.slice(index * 2, index * 2 + 2), 16);
    }
    return bytes;
}

function asError(error: unknown): Error {
    return error instanceof Error ? error : new Error(String(error));
}

async function requestBluetoothPermissions(): Promise<void> {
    if (Platform.OS !== 'android') {
        return;
    }

    const permissions =
        Number(Platform.Version) >= 31
            ? [
                PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
                PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
            ]
            : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];

    const results = await PermissionsAndroid.requestMultiple(permissions);
    if (permissions.some((permission) => results[permission] !== PermissionsAndroid.RESULTS.GRANTED)) {
        throw new Error('Bluetooth permissions are required to find and connect to the XBee 3.');
    }
}

function bluetoothStateError(state: string): Error {
    switch (state) {
        case BleState.PoweredOff:
            return new Error('Bluetooth is turned off. Turn on Bluetooth and try again.');
        case BleState.Unauthorized:
            return new Error('Bluetooth access is not allowed. Enable Bluetooth permission for this app in Settings.');
        case BleState.Unsupported:
            return new Error('This device does not support Bluetooth Low Energy.');
        default:
            return new Error(`Bluetooth is unavailable (${state}). Check Bluetooth and try again.`);
    }
}

async function waitForBluetoothPoweredOn(): Promise<void> {
    const bleManager = getManager();
    const currentState = await bleManager.state();
    if (currentState === BleState.PoweredOn) {
        return;
    }
    if (
        currentState === BleState.PoweredOff ||
        currentState === BleState.Unauthorized ||
        currentState === BleState.Unsupported
    ) {
        throw bluetoothStateError(currentState);
    }

    await new Promise<void>((resolve, reject) => {
        let subscription: BleSubscription | undefined;
        const timeout = setTimeout(() => {
            subscription?.remove();
            reject(new Error('Bluetooth is still initializing. Wait a moment, then try again.'));
        }, BLUETOOTH_STATE_TIMEOUT_MS);

        subscription = bleManager.onStateChange((state) => {
            if (state === BleState.PoweredOn) {
                clearTimeout(timeout);
                subscription?.remove();
                resolve();
            } else if (
                state === BleState.PoweredOff ||
                state === BleState.Unauthorized ||
                state === BleState.Unsupported
            ) {
                clearTimeout(timeout);
                subscription?.remove();
                reject(bluetoothStateError(state));
            }
        }, true);
    });
}

async function findXBeeDevice(): Promise<BleDevice> {
    return new Promise((resolve, reject) => {
        let settled = false;
        const finish = (device: BleDevice | null, error?: Error): void => {
            if (settled) {
                return;
            }
            settled = true;
            clearTimeout(timeout);
            void getManager()
                .stopDeviceScan()
                .then(() => {
                    if (error) {
                        reject(error);
                    } else if (device) {
                        resolve(device);
                    } else {
                        reject(new Error('XBee BLE scan ended without finding a device.'));
                    }
                })
                .catch((stopError: unknown) => {
                    reject(new Error(`Could not stop the Bluetooth scan: ${String(stopError)}`));
                });
        };

        const timeout = setTimeout(() => {
            finish(null, new Error('No XBee 3 found. Make sure it is powered on and Bluetooth is enabled.'));
        }, SCAN_TIMEOUT_MS);

        void getManager()
            .startDeviceScan(null, null, (error, device) => {
                if (error) {
                    finish(null, error);
                } else if (
                    device &&
                    (device.serviceUUIDs?.some((uuid) => uuid.toLowerCase() === XBEE_SERVICE_UUID) ||
                        device.name?.toLowerCase().includes('xbee'))
                ) {
                    finish(device);
                }
            })
            .catch((error: unknown) => finish(null, asError(error)));
    });
}

async function writeBytes(device: BleDevice, bytes: Uint8Array): Promise<void> {
    const mtu = Number.isFinite(device.mtu) && device.mtu >= 23 ? device.mtu : 23;
    const maxWriteLength = Math.max(20, mtu - 3);
    for (let offset = 0; offset < bytes.length; offset += maxWriteLength) {
        const chunk = bytes.slice(offset, offset + maxWriteLength);
        await device.writeCharacteristicWithResponseForService(
            XBEE_SERVICE_UUID,
            XBEE_REQUEST_CHARACTERISTIC_UUID,
            Buffer.from(chunk).toString('base64')
        );
    }
}

function requireUnlockResponse(frame: ParsedXBeeFrame, expectedStep: number): void {
    if (frame.type !== xbeeFrame.FrameType.BLE_UNLOCK_RESPONSE) {
        throw new Error(`Unexpected XBee BLE frame type: 0x${frame.type.toString(16)}`);
    }
    if (frame.step !== undefined && frame.step >= 0x80) {
        const reason =
            frame.step === 0x82
                ? 'The XBee rejected the BLE password.'
                : `The XBee returned unlock error 0x${frame.step.toString(16)}.`;
        throw new Error(reason);
    }
    if (frame.step !== expectedStep) {
        throw new Error(`Unexpected XBee BLE unlock step: ${String(frame.step)}`);
    }
}

async function authenticate(
    device: BleDevice,
    reader: FrameReader,
    password: string
): Promise<{ rxCipher: CtrStream; txCipher: CtrStream }> {
    const parameters = srpParameters(1024);
    const clientEphemeral = srpClient.generateEphemeral(parameters);

    await writeBytes(
        device,
        xbeeFrame.encode({
            type: xbeeFrame.FrameType.BLE_UNLOCK_REQUEST,
            step: 1,
            clientEphemeral: bytesFromHex(clientEphemeral.public),
        })
    );

    const step2 = await reader.nextFrame();
    requireUnlockResponse(step2, 2);
    if (!step2.salt || !step2.serverEphemeral) {
        throw new Error('XBee BLE unlock response is missing its salt or server key.');
    }

    const salt = Buffer.from(step2.salt).toString('hex');
    const privateKey = srpClient.derivePrivateKey(salt, BLE_USERNAME, password, parameters);
    const clientSession = srpClient.deriveSession(
        clientEphemeral.secret,
        Buffer.from(step2.serverEphemeral).toString('hex'),
        salt,
        BLE_USERNAME,
        privateKey,
        parameters
    );

    await writeBytes(
        device,
        xbeeFrame.encode({
            type: xbeeFrame.FrameType.BLE_UNLOCK_REQUEST,
            step: 3,
            clientSessionProof: bytesFromHex(clientSession.proof),
        })
    );

    const step4 = await reader.nextFrame();
    requireUnlockResponse(step4, 4);
    if (!step4.serverSessionProof || !step4.txNonce || !step4.rxNonce) {
        throw new Error('XBee BLE unlock response is missing session proof or nonces.');
    }

    srpClient.verifySession(
        clientEphemeral.public,
        clientSession,
        Buffer.from(step4.serverSessionProof).toString('hex'),
        parameters
    );

    const key = bytesFromHex(clientSession.key);
    return {
        txCipher: new CtrStream(key, step4.txNonce),
        rxCipher: new CtrStream(key, step4.rxNonce),
    };
}

async function disconnectSession(session: XBeeSession): Promise<void> {
    if (activeSession === session) {
        activeSession = undefined;
    }
    session.subscription.remove();
    await session.device.cancelConnection();
}

async function connectAndAuthenticate(password: string): Promise<XBeeSession> {
    await requestBluetoothPermissions();
    await waitForBluetoothPoweredOn();
    const foundDevice = await findXBeeDevice();
    const device = await foundDevice.connect();
    let subscription: BleSubscription | undefined;

    try {
        await device.discoverAllServicesAndCharacteristics();
        const services = await device.services();
        if (!services.some((service) => service.uuid.toLowerCase() === XBEE_SERVICE_UUID)) {
            throw new Error('The selected Bluetooth device does not expose the XBee BLE API service.');
        }
        const characteristics = await device.characteristicsForService(XBEE_SERVICE_UUID);
        if (
            !characteristics.some(
                (characteristic) =>
                    characteristic.uuid.toLowerCase() === XBEE_REQUEST_CHARACTERISTIC_UUID
            ) ||
            !characteristics.some(
                (characteristic) =>
                    characteristic.uuid.toLowerCase() === XBEE_RESPONSE_CHARACTERISTIC_UUID
            )
        ) {
            throw new Error('The XBee BLE API request or response characteristic is unavailable.');
        }

        const reader = new FrameReader((frame) => {
            if (frame.type === xbeeFrame.FrameType.USER_DATA_RELAY_OUTPUT && frame.data) {
                console.log('XBee response:', Buffer.from(frame.data).toString('utf8'));
            } else {
                console.log('Received XBee BLE API frame:', frame);
            }
        });

        subscription = device.monitorCharacteristicForService(
            XBEE_SERVICE_UUID,
            XBEE_RESPONSE_CHARACTERISTIC_UUID,
            (error, characteristic) => {
                if (error) {
                    console.error('XBee BLE indication failed:', error);
                    reader.fail(error);
                } else if (characteristic?.value) {
                    try {
                        reader.push(Buffer.from(characteristic.value, 'base64'));
                    } catch (frameError) {
                        console.error('Could not decode XBee BLE API frame:', frameError);
                    }
                }
            }
        );

        const { txCipher, rxCipher } = await authenticate(device, reader, password);
        reader.setDecryptor(rxCipher);
        reader.startStreaming();
        const session = { device, subscription, reader, txCipher };
        activeSession = session;
        return session;
    } catch (error) {
        subscription?.remove();
        try {
            await device.cancelConnection();
        } catch (disconnectError) {
            console.error('Failed to close XBee connection after authentication error:', disconnectError);
        }
        throw error;
    }
}

export async function sendStatusToXBee(password: string): Promise<void> {
    if (activeSession) {
        const existingSession = activeSession;
        const connected = await existingSession.device.isConnected();
        if (!connected || existingSession.reader.hasFailed) {
            activeSession = undefined;
            existingSession.subscription.remove();
            if (connected) {
                await existingSession.device.cancelConnection();
            }
        }
    }

    const session = activeSession ?? (await connectAndAuthenticate(password));
    const frameId = nextFrameId;
    nextFrameId = nextFrameId === 0xff ? 1 : nextFrameId + 1;

    const frame = xbeeFrame.encode({
        type: xbeeFrame.FrameType.USER_DATA_RELAY_INPUT,
        id: frameId,
        destination: xbeeFrame.Interface.MICROPYTHON,
        data: Buffer.from('STATUS', 'utf8'),
    });

    try {
        await writeBytes(session.device, session.txCipher.transform(frame));
        console.log('Sent encrypted XBee BLE API frame:', frameId);
    } catch (error) {
        try {
            await disconnectSession(session);
        } catch (disconnectError) {
            console.error('Failed to close XBee connection after send error:', disconnectError);
        }
        throw error;
    }
}
