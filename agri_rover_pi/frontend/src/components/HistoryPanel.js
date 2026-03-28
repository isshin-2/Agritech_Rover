import React, { useState, useEffect } from 'react';
import { Line } from 'react-chartjs-2';
import {
    Chart as ChartJS,
    CategoryScale,
    LinearScale,
    PointElement,
    LineElement,
    Title,
    Tooltip,
    Legend
} from 'chart.js';
import axios from 'axios';

// FIX: Register Chart.js components (required for v3+)
ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Title, Tooltip, Legend);

export default function HistoryPanel({ apiUrl }) {
    const [history, setHistory] = useState([]);
    const [hours, setHours] = useState(24);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        fetchHistory();
    }, [hours]); // eslint-disable-line react-hooks/exhaustive-deps

    const fetchHistory = async () => {
        setLoading(true);
        try {
            const res = await axios.get(`${apiUrl}/api/sensors/history?hours=${hours}`);
            setHistory(res.data);
        } catch (err) {
            console.error('History fetch failed:', err);
        }
        setLoading(false);
    };

    const labels = history.map(r => {
        const d = new Date(r.timestamp);
        return `${d.getHours()}:${String(d.getMinutes()).padStart(2, '0')}`;
    });

    const chartData = {
        labels,
        datasets: [
            {
                label: 'Soil Moisture %',
                data: history.map(r => r.soil_moisture),
                borderColor: '#4caf50',
                backgroundColor: 'rgba(76,175,80,0.1)',
                tension: 0.3,
                fill: true
            },
            {
                label: 'Air Temp °C',
                data: history.map(r => r.air_temperature),
                borderColor: '#ff9800',
                backgroundColor: 'rgba(255,152,0,0.1)',
                tension: 0.3
            },
            {
                label: 'Humidity %',
                data: history.map(r => r.humidity),
                borderColor: '#00bcd4',
                backgroundColor: 'rgba(0,188,212,0.1)',
                tension: 0.3
            }
        ]
    };

    const options = {
        responsive: true,
        plugins: {
            legend: { labels: { color: '#ccc' } },
            title: { display: false }
        },
        scales: {
            x: { ticks: { color: '#888', maxTicksLimit: 8 }, grid: { color: '#333' } },
            y: { ticks: { color: '#888' }, grid: { color: '#333' } }
        }
    };

    return (
        <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
                <h3 style={{ margin: 0 }}>📊 Sensor History</h3>
                <select
                    value={hours}
                    onChange={e => setHours(parseInt(e.target.value))}
                    style={{ background: '#444', color: '#fff', border: 'none', padding: '5px 8px', borderRadius: '4px' }}
                >
                    <option value={1}>1 hour</option>
                    <option value={6}>6 hours</option>
                    <option value={24}>24 hours</option>
                    <option value={48}>48 hours</option>
                </select>
            </div>

            {loading ? (
                <div style={{ height: '200px', display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#555' }}>
                    Loading...
                </div>
            ) : history.length === 0 ? (
                <div style={{ height: '200px', display: 'flex', alignItems: 'center', justifyContent: 'center', color: '#555' }}>
                    No data yet
                </div>
            ) : (
                <Line data={chartData} options={options} />
            )}
        </div>
    );
}
