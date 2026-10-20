import { View, Text, StyleSheet } from 'react-native';

interface StatusProps {
    title: 'BATT' | 'CELL' | 'GNSS' | 'BLUE' | 'TIME';
    data: string;
}
// Define the component and export it
export default function Status({ title, data }: StatusProps) {
    const RenderStatus = () => {
        switch (title) {
            case 'BATT':
                return <Text style={styles.text}>Battery: %{data}</Text>;

            case 'CELL':
                return <Text style={styles.text}>Cellular: {data}</Text>;

            case 'GNSS':
                return <Text style={styles.text}>GNSS: {data}</Text>;

            case 'BLUE':
                return <Text style={styles.text}>Bluetooth: {data}</Text>;

            case 'TIME':
                return <Text style={styles.text}>Time: {data}</Text>;
                
            default:
                return null;
        }

    }   
    return (
        <View style={styles.container}>
            <RenderStatus />
        </View>
    );
}

const styles = StyleSheet.create({
    container: {
        flexDirection: 'row',
        alignItems: 'center',
        justifyContent: 'center',
        padding: 10,
    },
    text: {
        fontSize: 16,
        fontWeight: 'bold',
        color: '#333',
    },
});