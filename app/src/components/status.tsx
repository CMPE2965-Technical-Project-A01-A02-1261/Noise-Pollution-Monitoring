import { StyleSheet, Text, View } from 'react-native';

export type StatusTone = 'good' | 'warning' | 'bad' | 'neutral';

interface StatusProps {
    label: string;
    /** Main value. Omit while waiting for the device to report it. */
    value?: string;
    detail?: string;
    tone?: StatusTone;
    /** Optional meter shown under the value. */
    meter?: { kind: 'fill'; fraction: number } | { kind: 'bars'; filled: number; total: number };
}

const TONE_COLORS: Record<StatusTone, string> = {
    good: '#1E9E5A',
    warning: '#D98E04',
    bad: '#D64545',
    neutral: '#208AEF',
};

export default function Status({ label, value, detail, tone = 'neutral', meter }: StatusProps) {
    const color = TONE_COLORS[tone];
    const hasValue = value !== undefined;

    return (
        <View
            accessible
            accessibilityLabel={`${label}: ${hasValue ? value : 'waiting'}${detail ? `, ${detail}` : ''}`}
            style={styles.card}
        >
            <Text style={styles.label}>{label}</Text>
            <Text style={[styles.value, !hasValue && styles.placeholder]}>
                {hasValue ? value : '—'}
            </Text>

            {hasValue && meter?.kind === 'fill' && (
                <View style={styles.track}>
                    <View
                        style={[
                            styles.fill,
                            {
                                backgroundColor: color,
                                width: `${Math.round(Math.max(0, Math.min(1, meter.fraction)) * 100)}%`,
                            },
                        ]}
                    />
                </View>
            )}

            {hasValue && meter?.kind === 'bars' && (
                <View style={styles.bars}>
                    {Array.from({ length: meter.total }, (_, index) => (
                        <View
                            key={index}
                            style={[
                                styles.bar,
                                { height: 6 + index * 4 },
                                index < meter.filled && { backgroundColor: color },
                            ]}
                        />
                    ))}
                </View>
            )}

            <Text style={[styles.detail, hasValue && tone !== 'neutral' && { color }]}>
                {hasValue ? detail ?? ' ' : 'Waiting for device'}
            </Text>
        </View>
    );
}

const styles = StyleSheet.create({
    card: {
        backgroundColor: '#fff',
        borderColor: '#e3e6ea',
        borderRadius: 12,
        borderWidth: 1,
        flexBasis: '47%',
        flexGrow: 1,
        padding: 14,
    },
    label: {
        color: '#666',
        fontSize: 13,
        fontWeight: '600',
        letterSpacing: 0.5,
        textTransform: 'uppercase',
    },
    value: {
        color: '#222',
        fontSize: 26,
        fontVariant: ['tabular-nums'],
        fontWeight: '700',
        marginTop: 6,
    },
    placeholder: {
        color: '#bbb',
    },
    track: {
        backgroundColor: '#eceff2',
        borderRadius: 4,
        height: 8,
        marginTop: 8,
        overflow: 'hidden',
    },
    fill: {
        borderRadius: 4,
        height: '100%',
    },
    bars: {
        alignItems: 'flex-end',
        flexDirection: 'row',
        gap: 3,
        height: 22,
        marginTop: 8,
    },
    bar: {
        backgroundColor: '#dde1e5',
        borderRadius: 2,
        width: 6,
    },
    detail: {
        color: '#888',
        fontSize: 13,
        marginTop: 8,
    },
});
