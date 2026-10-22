declare module 'xbee-frame' {
    export const FrameType: {
        BLE_UNLOCK_REQUEST: number;
        BLE_UNLOCK_RESPONSE: number;
        USER_DATA_RELAY_INPUT: number;
        USER_DATA_RELAY_OUTPUT: number;
    };

    export const Interface: {
        MICROPYTHON: number;
    };

    export type ApiFrame =
        | {
            type: number;
            step: 1;
            clientEphemeral: Uint8Array;
        }
        | {
            type: number;
            step: 3;
            clientSessionProof: Uint8Array;
        }
        | {
            type: number;
            id: number;
            destination: number;
            data: Uint8Array;
        };

    export interface DecodedFrame {
        type: number;
        step?: number;
        salt?: Uint8Array;
        serverEphemeral?: Uint8Array;
        serverSessionProof?: Uint8Array;
        txNonce?: Uint8Array;
        rxNonce?: Uint8Array;
        data?: Uint8Array;
        source?: number;
        status?: number;
    }

    export function encode(frame: ApiFrame): Uint8Array;
    export function decode(frame: Uint8Array): DecodedFrame;
}

declare module '@kapetan/secure-remote-password/parameters' {
    export interface SRPInteger {
        add(value: SRPInteger): SRPInteger;
        equals(value: SRPInteger): boolean;
        mod(value: SRPInteger): SRPInteger;
        modPow(exponent: SRPInteger, modulus: SRPInteger): SRPInteger;
        multiply(value: SRPInteger): SRPInteger;
        subtract(value: SRPInteger): SRPInteger;
        toHex(): string;
        xor(value: SRPInteger): SRPInteger;
    }

    export interface SRPParameters {
        N: SRPInteger;
        g: SRPInteger;
        k: SRPInteger;
        H(...values: (string | SRPInteger)[]): SRPInteger;
        hashOutputBytes: number;
    }

    export default function parameters(group?: number): SRPParameters;
}

declare module '@kapetan/secure-remote-password/client' {
    import type { SRPParameters } from '@kapetan/secure-remote-password/parameters';

    export function generateEphemeral(parameters: SRPParameters): {
        secret: string;
        public: string;
    };

    export function derivePrivateKey(
        salt: string,
        username: string,
        password: string,
        parameters: SRPParameters
    ): string;

    export function deriveSession(
        clientSecretEphemeral: string,
        serverPublicEphemeral: string,
        salt: string,
        username: string,
        privateKey: string,
        parameters: SRPParameters
    ): { key: string; proof: string };

    export function verifySession(
        clientPublicEphemeral: string,
        clientSession: { key: string; proof: string },
        serverSessionProof: string,
        parameters: SRPParameters
    ): void;
}
