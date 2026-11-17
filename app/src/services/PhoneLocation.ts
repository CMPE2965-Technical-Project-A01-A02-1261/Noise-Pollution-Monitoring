import * as Location from 'expo-location';

/** A recent fix is good enough: the device only needs to know roughly where it is. */
const LAST_KNOWN_MAX_AGE_MS = 2 * 60 * 1000;

export interface PhoneLocation {
    latitude: number;
    longitude: number;
}

/**
 * Returns the phone's position, asking for location permission if needed.
 * Throws if permission is denied or no position is available.
 */
export async function getPhoneLocation(): Promise<PhoneLocation> {
    const { granted } = await Location.requestForegroundPermissionsAsync();
    if (!granted) {
        throw new Error(
            'Location permission is needed to send the device its position. Allow location access for this app in Settings.'
        );
    }

    const position =
        (await Location.getLastKnownPositionAsync({ maxAge: LAST_KNOWN_MAX_AGE_MS })) ??
        (await Location.getCurrentPositionAsync({ accuracy: Location.Accuracy.Balanced }));

    return { latitude: position.coords.latitude, longitude: position.coords.longitude };
}
