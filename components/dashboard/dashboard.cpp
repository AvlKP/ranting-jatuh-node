#include "dashboard.hpp"
#include "logger_internal.hpp"
#include "mqtt_log.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cctype>

namespace dashboard {

namespace {

static const char* kTag = "DASHBOARD";
Dashboard* g_self = nullptr;

// Beautiful premium dark mode dashboard raw HTML/CSS/JS string literal
const char* kIndexHtml = R"raw(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Ranting Jatuh - Debugging Dashboard</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&display=swap" rel="stylesheet">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        :root {
            --bg-color: #08080a;
            --panel-bg: rgba(18, 18, 24, 0.7);
            --border-color: rgba(255, 255, 255, 0.08);
            --text-primary: #f3f4f6;
            --text-secondary: #9ca3af;
            --accent-primary: #8b5cf6;
            --accent-secondary: #3b82f6;
            --accent-glow: rgba(139, 92, 246, 0.15);
            --success: #10b981;
            --warning: #f59e0b;
            --danger: #ef4444;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            font-family: 'Outfit', sans-serif;
            background-color: var(--bg-color);
            color: var(--text-primary);
            overflow-x: hidden;
            background-image: radial-gradient(circle at 10% 20%, rgba(139, 92, 246, 0.08) 0%, transparent 40%),
                              radial-gradient(circle at 90% 80%, rgba(59, 130, 246, 0.08) 0%, transparent 40%);
            background-attachment: fixed;
            min-height: 100vh;
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 20px 40px;
            border-bottom: 1px solid var(--border-color);
            background: rgba(8, 8, 10, 0.8);
            backdrop-filter: blur(12px);
            position: sticky;
            top: 0;
            z-index: 100;
        }

        .logo {
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 1.4rem;
            font-weight: 700;
            letter-spacing: 0.5px;
            background: linear-gradient(135deg, var(--accent-primary), var(--accent-secondary));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .system-status {
            display: flex;
            gap: 20px;
        }

        .status-badge {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            padding: 8px 16px;
            border-radius: 30px;
            font-size: 0.85rem;
            display: flex;
            align-items: center;
            gap: 8px;
            transition: all 0.3s ease;
        }

        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            display: inline-block;
        }

        .status-dot.active { background: var(--success); box-shadow: 0 0 8px var(--success); }
        .status-dot.inactive { background: var(--danger); box-shadow: 0 0 8px var(--danger); }

        .dashboard-grid {
            display: grid;
            grid-template-columns: repeat(12, 1fr);
            gap: 24px;
            padding: 30px 40px;
            max-width: 1600px;
            margin: 0 auto;
        }

        .card {
            background: var(--panel-bg);
            border: 1px solid var(--border-color);
            border-radius: 16px;
            padding: 24px;
            backdrop-filter: blur(16px);
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
            transition: transform 0.3s ease, border-color 0.3s ease;
        }

        .card:hover {
            border-color: rgba(139, 92, 246, 0.3);
            transform: translateY(-2px);
        }

        .card-title {
            font-size: 1.1rem;
            font-weight: 600;
            margin-bottom: 20px;
            color: var(--text-primary);
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-left: 3px solid var(--accent-primary);
            padding-left: 10px;
        }

        .col-12 { grid-column: span 12; }
        .col-8 { grid-column: span 8; }
        .col-6 { grid-column: span 6; }
        .col-4 { grid-column: span 4; }

        @media (max-width: 1024px) {
            .col-8, .col-6, .col-4 { grid-column: span 12; }
            .dashboard-grid { padding: 15px; }
            header { padding: 15px; flex-direction: column; gap: 15px; }
        }

        /* Stream Table Styling */
        .table-container {
            overflow-x: auto;
            max-height: 280px;
        }

        table {
            width: 100%;
            border-collapse: collapse;
            text-align: left;
            font-size: 0.9rem;
        }

        th, td {
            padding: 12px 16px;
            border-bottom: 1px solid var(--border-color);
        }

        th {
            color: var(--text-secondary);
            font-weight: 500;
            background: rgba(255, 255, 255, 0.02);
            position: sticky;
            top: 0;
            backdrop-filter: blur(4px);
        }

        tr:hover td {
            background: rgba(255, 255, 255, 0.01);
            color: #fff;
        }

        /* File Browser Styling */
        .file-list {
            display: flex;
            flex-direction: column;
            gap: 12px;
            max-height: 280px;
            overflow-y: auto;
        }

        .file-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 16px;
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid var(--border-color);
            border-radius: 10px;
            transition: all 0.2s ease;
        }

        .file-item:hover {
            background: rgba(255, 255, 255, 0.05);
            border-color: var(--accent-secondary);
        }

        .file-info {
            display: flex;
            flex-direction: column;
            gap: 4px;
        }

        .file-name {
            font-weight: 500;
            font-size: 0.95rem;
        }

        .file-size {
            font-size: 0.8rem;
            color: var(--text-secondary);
        }

        /* Console Terminal Styling */
        .terminal {
            background: #040406;
            border: 1px solid var(--border-color);
            border-radius: 12px;
            font-family: 'Courier New', Courier, monospace;
            padding: 16px;
            height: 250px;
            overflow-y: auto;
            color: var(--success);
            font-size: 0.85rem;
            line-height: 1.5;
            box-shadow: inset 0 0 10px rgba(0, 0, 0, 0.8);
        }

        .terminal-line {
            margin-bottom: 6px;
            border-bottom: 1px dashed rgba(16, 185, 129, 0.05);
            padding-bottom: 4px;
        }

        .terminal-time {
            color: var(--accent-secondary);
            margin-right: 8px;
        }

        /* Chart Canvas height override */
        .chart-container {
            position: relative;
            height: 280px;
            width: 100%;
        }

        /* Scrollbars */
        ::-webkit-scrollbar {
            width: 6px;
            height: 6px;
        }
        ::-webkit-scrollbar-track {
            background: transparent;
        }
        ::-webkit-scrollbar-thumb {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 10px;
        }
        ::-webkit-scrollbar-thumb:hover {
            background: var(--accent-primary);
        }
    </style>
</head>
<body>
    <header>
        <div class="logo">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/>
            </svg>
            Ranting Jatuh Node
        </div>
        <div class="system-status">
            <div class="status-badge">
                <span class="status-dot active" id="wifi-dot"></span>
                Wi-Fi
            </div>
            <div class="status-badge">
                <span class="status-dot" id="mqtt-dot"></span>
                MQTT
            </div>
            <div class="status-badge">
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <rect x="2" y="2" width="20" height="8" rx="2" ry="2"/>
                    <rect x="2" y="14" width="20" height="8" rx="2" ry="2"/>
                    <line x1="6" y1="6" x2="6.01" y2="6"/>
                    <line x1="6" y1="18" x2="6.01" y2="18"/>
                </svg>
                Heap: <span id="heap-val">0 KB</span>
            </div>
            <div class="status-badge" id="node-state-badge" style="border-color: var(--success); color: var(--success);">
                State: <span id="node-state-val">IDLE</span>
            </div>
            <div class="status-badge" id="freq-roll-badge">
                f_n Roll: <span id="freq-roll-val">-</span> Hz
            </div>
            <div class="status-badge" id="freq-pitch-badge">
                f_n Pitch: <span id="freq-pitch-val">-</span> Hz
            </div>
            <div class="status-badge" id="damping-roll-badge">
                &zeta; Roll: <span id="damping-roll-val">-</span>
            </div>
            <div class="status-badge" id="damping-pitch-badge">
                &zeta; Pitch: <span id="damping-pitch-val">-</span>
            </div>
        </div>
    </header>

    <div class="dashboard-grid">
        <!-- Sensor Stream Table -->
        <div class="card col-8">
            <div class="card-title">Real-Time Sensor Stream (1 Hz)</div>
            <div class="table-container">
                <table>
                    <thead>
                        <tr>
                            <th>Time</th>
                            <th>Accel (X, Y, Z) G</th>
                            <th>Gyro (X, Y, Z) &deg;/s</th>
                            <th>Roll (&deg;)</th>
                            <th>Pitch (&deg;)</th>
                        </tr>
                    </thead>
                    <tbody id="stream-body">
                        <tr><td colspan="5" style="text-align: center; color: var(--text-secondary);">Waiting for data...</td></tr>
                    </tbody>
                </table>
            </div>
        </div>

        <!-- File Browser -->
        <div class="card col-4">
            <div class="card-title">MicroSD Filesystem</div>
            <div class="file-list" id="file-browser">
                <div style="text-align: center; color: var(--text-secondary); padding-top: 50px;">SD Card empty or not mounted</div>
            </div>
        </div>

        <!-- Plots -->
        <div class="card col-6">
            <div class="card-title">Tilt Time Domain Plot</div>
            <div class="chart-container">
                <canvas id="tiltTimeChart"></canvas>
            </div>
        </div>

        <div class="card col-6">
            <div class="card-title">FFT Welch Output (PSD)</div>
            <div class="chart-container">
                <canvas id="fftChart"></canvas>
            </div>
        </div>

        <div class="card col-6">
            <div class="card-title">Tilt Distribution Histogram</div>
            <div class="chart-container">
                <canvas id="tiltDistChart"></canvas>
            </div>
        </div>

        <!-- MQTT Logs -->
        <div class="card col-6">
            <div class="card-title">MQTT & Network Activity Logs</div>
            <div class="terminal" id="terminal-box">
                <div class="terminal-line"><span class="terminal-time">[Startup]</span> Dashboard server launched. Ready.</div>
            </div>
        </div>
    </div>

    <script>
        // Setup Charts
        const ctxTime = document.getElementById('tiltTimeChart').getContext('2d');
        const tiltTimeChart = new Chart(ctxTime, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'Roll', data: [], borderColor: '#8b5cf6', backgroundColor: 'transparent', borderWidth: 2, pointRadius: 0 },
                    { label: 'Pitch', data: [], borderColor: '#3b82f6', backgroundColor: 'transparent', borderWidth: 2, pointRadius: 0 }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.03)' } },
                    y: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.05)' } }
                },
                plugins: { legend: { labels: { color: '#f3f4f6' } } }
            }
        });

        const ctxFft = document.getElementById('fftChart').getContext('2d');
        const fftChart = new Chart(ctxFft, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'PSD (dB)',
                    data: [],
                    borderColor: '#10b981',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    borderWidth: 1.5,
                    fill: true,
                    pointRadius: 0
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { title: { display: true, text: 'Frequency (Hz)', color: '#9ca3af' }, ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.03)' } },
                    y: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.05)' } }
                },
                plugins: { legend: { labels: { color: '#f3f4f6' } } }
            }
        });

        const ctxDist = document.getElementById('tiltDistChart').getContext('2d');
        const tiltDistChart = new Chart(ctxDist, {
            type: 'bar',
            data: {
                labels: [],
                datasets: [
                    { label: 'Roll Dist', data: [], backgroundColor: 'rgba(139, 92, 246, 0.6)', borderColor: '#8b5cf6', borderWidth: 1 },
                    { label: 'Pitch Dist', data: [], backgroundColor: 'rgba(59, 130, 246, 0.6)', borderColor: '#3b82f6', borderWidth: 1 }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    x: { title: { display: true, text: 'Angle (deg)', color: '#9ca3af' }, ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.03)' } },
                    y: { ticks: { color: '#9ca3af' }, grid: { color: 'rgba(255,255,255,0.05)' } }
                },
                plugins: { legend: { labels: { color: '#f3f4f6' } } }
            }
        });

        // Compute local distribution (histogram)
        function computeDistribution(data, binSize = 1.0, minLimit = -10, maxLimit = 10) {
            const numBins = Math.round((maxLimit - minLimit) / binSize) + 1;
            const bins = Array(numBins).fill(0);
            const labels = [];
            
            for (let i = 0; i < numBins; i++) {
                labels.push((minLimit + i * binSize).toFixed(1));
            }

            data.forEach(val => {
                const binIdx = Math.round((val - minLimit) / binSize);
                if (binIdx >= 0 && binIdx < numBins) {
                    bins[binIdx]++;
                }
            });
            return { bins, labels };
        }

        // Periodic API Call
        async function fetchSystemData() {
            try {
                const response = await fetch('/api/status');
                if (!response.ok) throw new Error('API fetch failed');
                const data = await response.json();

                // Update badging
                document.getElementById('wifi-dot').className = data.wifi_connected ? 'status-dot active' : 'status-dot inactive';
                document.getElementById('mqtt-dot').className = data.mqtt_connected ? 'status-dot active' : 'status-dot inactive';
                document.getElementById('heap-val').textContent = (data.heap_free / 1024).toFixed(1) + ' KB';

                const stateBadge = document.getElementById('node-state-badge');
                const stateVal = document.getElementById('node-state-val');
                stateVal.textContent = data.node_state;
                if (data.node_state === 'DISTURBED') {
                    stateBadge.style.borderColor = 'var(--danger)';
                    stateBadge.style.color = 'var(--danger)';
                } else {
                    stateBadge.style.borderColor = 'var(--success)';
                    stateBadge.style.color = 'var(--success)';
                }

                // Update natural frequency and damping ratio badges
                document.getElementById('freq-roll-val').textContent = data.natural_freq_roll_hz > 0 ? data.natural_freq_roll_hz.toFixed(2) : '-';
                document.getElementById('freq-pitch-val').textContent = data.natural_freq_pitch_hz > 0 ? data.natural_freq_pitch_hz.toFixed(2) : '-';
                document.getElementById('damping-roll-val').textContent = data.roll_damping_ratio > 0 ? data.roll_damping_ratio.toFixed(4) : '-';
                document.getElementById('damping-pitch-val').textContent = data.pitch_damping_ratio > 0 ? data.pitch_damping_ratio.toFixed(4) : '-';

                // Update Sensor Table
                const streamBody = document.getElementById('stream-body');
                if (data.stream_samples && data.stream_samples.length > 0) {
                    streamBody.innerHTML = '';
                    // Show reverse chronological
                    data.stream_samples.slice().reverse().forEach(sample => {
                        const tr = document.createElement('tr');
                        const timeStr = new Date(sample.ts / 1000).toLocaleTimeString();
                        tr.innerHTML = `
                            <td>${timeStr}</td>
                            <td>${sample.ax.toFixed(3)}, ${sample.ay.toFixed(3)}, ${sample.az.toFixed(3)}</td>
                            <td>${sample.gx.toFixed(1)}, ${sample.gy.toFixed(1)}, ${sample.gz.toFixed(1)}</td>
                            <td>${sample.r.toFixed(2)}</td>
                            <td>${sample.p.toFixed(2)}</td>
                        `;
                        streamBody.appendChild(tr);
                    });
                }

                // Update File Browser
                const fileBrowser = document.getElementById('file-browser');
                if (data.files && data.files.length > 0) {
                    fileBrowser.innerHTML = '';
                    data.files.forEach(file => {
                        const div = document.createElement('div');
                        div.className = 'file-item';
                        div.innerHTML = `
                            <div class="file-info">
                                <div class="file-name">${file.name}</div>
                                <div class="file-size">${(file.size / 1024).toFixed(2)} KB</div>
                            </div>
                            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#3b82f6" stroke-width="2" style="cursor: pointer;" onclick="window.location.href='/download?file=' + encodeURIComponent('${file.name}')">
                                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/>
                            </svg>
                        `;
                        fileBrowser.appendChild(div);
                    });
                } else {
                    fileBrowser.innerHTML = '<div style="text-align: center; color: var(--text-secondary); padding-top: 50px;">SD Card empty or not mounted</div>';
                }

                // Update Terminal logs
                const terminalBox = document.getElementById('terminal-box');
                if (data.mqtt_logs && data.mqtt_logs.length > 0) {
                    terminalBox.innerHTML = '';
                    data.mqtt_logs.forEach(logLine => {
                        const div = document.createElement('div');
                        div.className = 'terminal-line';
                        div.innerHTML = `<span class="terminal-time">[log]</span>${logLine}`;
                        terminalBox.appendChild(div);
                    });
                }

                // Update Time Domain Plot
                if (data.tilt_history && data.tilt_history.roll && data.tilt_history.roll.length > 0) {
                    const len = data.tilt_history.roll.length;
                    const labels = Array.from({length: len}, (_, i) => i);
                    tiltTimeChart.data.labels = labels;
                    tiltTimeChart.data.datasets[0].data = data.tilt_history.roll;
                    tiltTimeChart.data.datasets[1].data = data.tilt_history.pitch;
                    tiltTimeChart.update('none'); // no animation for speed

                    // Calculate distributions locally from history
                    const rollDist = computeDistribution(data.tilt_history.roll);
                    const pitchDist = computeDistribution(data.tilt_history.pitch);
                    tiltDistChart.data.labels = rollDist.labels;
                    tiltDistChart.data.datasets[0].data = rollDist.bins;
                    tiltDistChart.data.datasets[1].data = pitchDist.bins;
                    tiltDistChart.update('none');
                }

                // Update FFT Plot
                if (data.fft && data.fft.length > 0) {
                    const sampleRate = data.sample_rate || 26;
                    const fftSize = data.fft.length * 2;
                    const labels = Array.from({length: data.fft.length}, (_, i) => ((i * sampleRate) / fftSize).toFixed(3));
                    
                    // Convert power values to dB (with safety offset to prevent log(0))
                    const dbData = data.fft.map(val => 10 * Math.log10(val + 1e-12));
                    
                    fftChart.data.labels = labels;
                    fftChart.data.datasets[0].data = dbData;
                    fftChart.update('none');
                }

            } catch (err) {
                console.error("Dashboard error:", err);
            }
        }

        // Fetch every 1.5 seconds
        setInterval(fetchSystemData, 1500);
        fetchSystemData();
    </script>
</body>
</html>)raw";

} // namespace

Dashboard::Dashboard(monitor::Monitor& monitor, logger::Logger& logger) noexcept
    : monitor_{monitor}, logger_{logger} {
    g_self = this;
}

Dashboard::~Dashboard() noexcept {
    Stop();
    if (g_self == this) {
        g_self = nullptr;
    }
}

esp_err_t Dashboard::Start() noexcept {
    return Start(Config());
}

esp_err_t Dashboard::Start(const Config& config) noexcept {
    if (!config.enabled) {
        return ESP_OK;
    }
    
    config_ = config;
    if (server_ != nullptr) {
        return ESP_OK;
    }

    ESP_LOGI(kTag, "Starting HTTP Server on port %u", config_.port);

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = config_.port;
    server_config.stack_size = 12288; // Ensure ample stack for directory browsing
    server_config.lru_purge_enable = true;

    const esp_err_t err = httpd_start(&server_, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = IndexHandler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(server_, &index_uri);

    httpd_uri_t status_uri = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = StatusHandler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(server_, &status_uri);

    httpd_uri_t download_uri = {
        .uri       = "/download",
        .method    = HTTP_GET,
        .handler   = DownloadHandler,
        .user_ctx  = nullptr
    };
    httpd_register_uri_handler(server_, &download_uri);

    esp_err_t event_err = esp_event_handler_register(
        monitor::MONITOR_EVENT_BASE,
        monitor::MONITOR_EVENT_RESULT,
        &Dashboard::EventHandler,
        this);
    if (event_err != ESP_OK) {
        ESP_LOGE(kTag, "Failed to register MONITOR_EVENT_RESULT handler: %s", esp_err_to_name(event_err));
    }

    return ESP_OK;
}

void Dashboard::Stop() noexcept {
    if (server_ != nullptr) {
        ESP_LOGI(kTag, "Stopping HTTP Server");
        httpd_stop(server_);
        server_ = nullptr;
        
        esp_event_handler_unregister(
            monitor::MONITOR_EVENT_BASE,
            monitor::MONITOR_EVENT_RESULT,
            &Dashboard::EventHandler);
    }
}

esp_err_t Dashboard::IndexHandler(httpd_req_t* req) noexcept {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t Dashboard::StatusHandler(httpd_req_t* req) noexcept {
    if (g_self == nullptr) {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");

    // Start JSON streaming using chunked response to conserve RAM
    httpd_resp_send_chunk(req, "{", 1);

    // WiFi & Heap Stats
    bool wifi_connected = false;
    wifi_ap_record_t ap_info{};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_connected = true;
    }

    const char* state_str = "IDLE";
    if (g_self->monitor_.GetState() == monitor::NodeState::DISTURBED) {
        state_str = "DISTURBED";
    }
    float freq_roll = 0.0f;
    float freq_pitch = 0.0f;
    float damp_roll = 0.0f;
    float damp_pitch = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_self->mutex_);
        freq_roll = g_self->latest_result_.natural_freq_roll_hz;
        freq_pitch = g_self->latest_result_.natural_freq_pitch_hz;
        damp_roll = g_self->latest_result_.roll_damping_ratio;
        damp_pitch = g_self->latest_result_.pitch_damping_ratio;
    }

    char chunk_buf[384];
    int len = std::snprintf(chunk_buf, sizeof(chunk_buf),
                            "\"wifi_connected\":%s,\"mqtt_connected\":%s,\"heap_free\":%lu,\"sample_rate\":%d,\"node_id\":\"%s\",\"node_state\":\"%s\","
                            "\"natural_freq_roll_hz\":%.3f,\"natural_freq_pitch_hz\":%.3f,"
                            "\"roll_damping_ratio\":%.4f,\"pitch_damping_ratio\":%.4f,",
                            wifi_connected ? "true" : "false",
                            g_self->logger_.HasMonitorResult() ? "true" : "false", // Use logger state as MQTT proxy
                            static_cast<unsigned long>(esp_get_free_heap_size()),
                            CONFIG_MONITOR_IMU_RATE_HZ,
                            logger::mqtt::GetNodeId(),
                            state_str,
                            freq_roll,
                            freq_pitch,
                            damp_roll,
                            damp_pitch);
    httpd_resp_send_chunk(req, chunk_buf, len);

    // Latest Sensor Stream Table (20 samples)
    httpd_resp_send_chunk(req, "\"stream_samples\":[", 18);
    static constexpr std::size_t kMaxStreamQuery = 20U;
    std::array<monitor::StreamSample, kMaxStreamQuery> stream_buf{};
    std::size_t stream_count = 0U;
    g_self->monitor_.GetLatestSamples(stream_buf.data(), stream_count, kMaxStreamQuery);

    const std::uint64_t current_uptime_us = static_cast<std::uint64_t>(esp_timer_get_time());
    std::time_t current_time = 0;
    std::time(&current_time);

    for (std::size_t i = 0U; i < stream_count; ++i) {
        std::uint64_t sample_ts_us = 0;
        if (current_time >= 1672531200) {
            const std::uint64_t current_time_us = static_cast<std::uint64_t>(current_time) * 1000000ULL;
            if (current_uptime_us >= stream_buf[i].timestamp_us) {
                const std::uint64_t age_us = current_uptime_us - stream_buf[i].timestamp_us;
                sample_ts_us = current_time_us - age_us;
            } else {
                sample_ts_us = current_time_us;
            }
        } else {
            sample_ts_us = stream_buf[i].timestamp_us;
        }

        len = std::snprintf(chunk_buf, sizeof(chunk_buf),
                            "%s{\"ts\":%llu,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"r\":%.2f,\"p\":%.2f}",
                            (i > 0U) ? "," : "",
                            static_cast<unsigned long long>(sample_ts_us),
                            stream_buf[i].accel_x, stream_buf[i].accel_y, stream_buf[i].accel_z,
                            stream_buf[i].gyro_x, stream_buf[i].gyro_y, stream_buf[i].gyro_z,
                            stream_buf[i].roll, stream_buf[i].pitch);
        httpd_resp_send_chunk(req, chunk_buf, len);
    }
    httpd_resp_send_chunk(req, "],", 2);

    // MicroSD Directory browser
    httpd_resp_send_chunk(req, "\"files\":[", 9);
    bool first_file = true;
    DIR* dir = opendir(CONFIG_APP_SD_MOUNT_POINT);
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_REG) {
                char file_path[512];
                std::snprintf(file_path, sizeof(file_path), "%s/%s", CONFIG_APP_SD_MOUNT_POINT, entry->d_name);
                struct stat st{};
                long long fsize = 0;
                if (stat(file_path, &st) == 0) {
                    fsize = st.st_size;
                }
                
                len = std::snprintf(chunk_buf, sizeof(chunk_buf),
                                    "%s{\"name\":\"%s\",\"size\":%lld}",
                                    first_file ? "" : ",",
                                    entry->d_name,
                                    fsize);
                httpd_resp_send_chunk(req, chunk_buf, len);
                first_file = false;
            }
        }
        closedir(dir);
    }
    httpd_resp_send_chunk(req, "],", 2);

    // Downsampled Tilt History (Max 150 points for line charts)
    httpd_resp_send_chunk(req, "\"tilt_history\":{\"roll\":[", 24);
    static constexpr std::size_t kMaxHistoryQuery = 150U;
    std::array<float, kMaxHistoryQuery> roll_buf{};
    std::array<float, kMaxHistoryQuery> pitch_buf{};
    std::size_t history_count = 0U;
    g_self->monitor_.GetTiltHistory(roll_buf.data(), pitch_buf.data(), history_count, kMaxHistoryQuery);

    for (std::size_t i = 0U; i < history_count; ++i) {
        len = std::snprintf(chunk_buf, sizeof(chunk_buf), "%s%.2f", (i > 0U) ? "," : "", roll_buf[i]);
        httpd_resp_send_chunk(req, chunk_buf, len);
    }
    httpd_resp_send_chunk(req, "],\"pitch\":[", 11);
    for (std::size_t i = 0U; i < history_count; ++i) {
        len = std::snprintf(chunk_buf, sizeof(chunk_buf), "%s%.2f", (i > 0U) ? "," : "", pitch_buf[i]);
        httpd_resp_send_chunk(req, chunk_buf, len);
    }
    httpd_resp_send_chunk(req, "]},", 3);

    // FFT PSD (downsampled from 512 to 128 for lightweight HTTP response)
    httpd_resp_send_chunk(req, "\"fft\":[", 7);
    static constexpr std::size_t kFftFullSize = 512U;
    std::array<float, kFftFullSize> fft_buf{};
    std::size_t fft_count = 0U;
    g_self->monitor_.GetFftData(fft_buf.data(), fft_count);

    if (fft_count > 0U) {
        // Downsample factor 4 (512 / 128)
        for (std::size_t i = 0U; i < 128U; ++i) {
            float val = 0.0f;
            for (std::size_t j = 0U; j < 4U; ++j) {
                const std::size_t idx = i * 4U + j;
                if (idx < fft_count) {
                    val += fft_buf[idx];
                }
            }
            val /= 4.0f; // average
            
            len = std::snprintf(chunk_buf, sizeof(chunk_buf), "%s%.4f", (i > 0U) ? "," : "", val);
            httpd_resp_send_chunk(req, chunk_buf, len);
        }
    }
    httpd_resp_send_chunk(req, "],", 2);

    // MQTT Circular Buffer Logs
    httpd_resp_send_chunk(req, "\"mqtt_logs\":[", 13);
    char logs_flat[logger::mqtt::kMaxMqttLogLines * logger::mqtt::kMaxMqttLogLineLen];
    std::size_t logs_count = 0;
    logger::mqtt::g_mqtt_log_buffer.GetLogs(logs_flat, logger::mqtt::kMaxMqttLogLines, logs_count);

    for (std::size_t i = 0U; i < logs_count; ++i) {
        const char* single_line = logs_flat + (i * logger::mqtt::kMaxMqttLogLineLen);
        
        // Escape quotes to maintain strict JSON compliance
        char escaped[128];
        std::size_t esc_idx = 0;
        for (std::size_t k = 0; single_line[k] != '\0' && esc_idx < sizeof(escaped) - 4; ++k) {
            if (single_line[k] == '"' || single_line[k] == '\\') {
                escaped[esc_idx++] = '\\';
            }
            escaped[esc_idx++] = single_line[k];
        }
        escaped[esc_idx] = '\0';

        len = std::snprintf(chunk_buf, sizeof(chunk_buf), "%s\"%s\"", (i > 0U) ? "," : "", escaped);
        httpd_resp_send_chunk(req, chunk_buf, len);
    }
    httpd_resp_send_chunk(req, "]", 1);

    // Terminate JSON stream
    httpd_resp_send_chunk(req, "}", 1);
    httpd_resp_send_chunk(req, nullptr, 0); // End chunk transmission
    return ESP_OK;
}

void UrlDecode(char* dst, const char* src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (std::isxdigit(static_cast<unsigned char>(a)) && std::isxdigit(static_cast<unsigned char>(b)))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

esp_err_t Dashboard::DownloadHandler(httpd_req_t* req) noexcept {
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
        return ESP_FAIL;
    }

    char raw_filename[128];
    if (httpd_query_key_value(query, "file", raw_filename, sizeof(raw_filename)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' parameter");
        return ESP_FAIL;
    }

    char filename[128];
    UrlDecode(filename, raw_filename);

    // Basic security: check for path traversal
    if (std::strstr(filename, "..") != nullptr || std::strchr(filename, '/') != nullptr || std::strchr(filename, '\\') != nullptr) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid file path");
        return ESP_FAIL;
    }

    char file_path[512];
    std::snprintf(file_path, sizeof(file_path), "%s/%s", CONFIG_APP_SD_MOUNT_POINT, filename);

    FILE* f = std::fopen(file_path, "rb");
    if (f == nullptr) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    // Set headers
    httpd_resp_set_type(req, "application/octet-stream");
    
    char disposition[256];
    std::snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    // Send file contents in chunks
    char chunk_buf[1024];
    size_t read_bytes;
    esp_err_t err = ESP_OK;
    while ((read_bytes = std::fread(chunk_buf, 1, sizeof(chunk_buf), f)) > 0) {
        err = httpd_resp_send_chunk(req, chunk_buf, read_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to send chunk: %s", esp_err_to_name(err));
            break;
        }
    }
    std::fclose(f);

    // End chunk transmission
    httpd_resp_send_chunk(req, nullptr, 0);

    return err;
}

void Dashboard::EventHandler(void* handler_args,
                             esp_event_base_t base,
                             std::int32_t id,
                             void* event_data) noexcept {
    auto* self = static_cast<Dashboard*>(handler_args);
    if (self == nullptr || event_data == nullptr) {
        return;
    }

    if (id == monitor::MONITOR_EVENT_RESULT) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->latest_result_ = *static_cast<const monitor::MonitorResult*>(event_data);
    }
}

} // namespace dashboard
