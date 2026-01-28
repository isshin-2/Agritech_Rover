import React from 'react';
// import { Line } from 'react-chartjs-2';
// Import chart defaults...

export default function HistoryPanel() {
    return (
        <div style={{ background: '#2d2d2d', padding: '15px', borderRadius: '8px' }}>
            <h3>📊 24h History</h3>
            <div style={{ height: '200px', display: 'flex', alignItems: 'center', justifyContent: 'center', background: '#1e1e1e', borderRadius: '4px' }}>
                <span style={{ color: '#555' }}>[Chart.js Graph Placeholder - Needs npm install to render]</span>
            </div>
        </div>
    );
}
