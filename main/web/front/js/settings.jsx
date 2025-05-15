const Modal = ({ isOpen, onClose, children, className = '' }) => {
    if (!isOpen) return null;
    return (
        <div className="modal-overlay" onClick={onClose}>
            <div className={`modal-content ${className}`} onClick={e => e.stopPropagation()}>
                <button className="modal-close" onClick={onClose}>×</button>
                {children}
            </div>
        </div>
    );
};

const useWebSocket = (onMessage) => {
    const [connected, setConnected] = React.useState(false);
    const [loading, setLoading] = React.useState(true);
    const [error, setError] = React.useState(null);
    const [lastMessageTime, setLastMessageTime] = React.useState(0);
    
    const socketRef = React.useRef(null);
    const wsCheckIntervalRef = React.useRef(null);
    const reconnectTimeoutRef = React.useRef(null);

    const connect = React.useCallback(() => {
        if (socketRef.current && socketRef.current.readyState !== WebSocket.CLOSED) {
            return;
        }

        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = `${protocol}//${window.location.host}/ws`;
        const socket = new WebSocket(wsUrl);
        
        socket.onopen = () => {
            console.log('WebSocket connected');
            setConnected(true);
            setLoading(false);
            setLastMessageTime(Date.now());
            setError(null);
        };

        socket.onclose = () => {
            console.log('WebSocket disconnected');
            setConnected(false);
            setLoading(true);

            if (reconnectTimeoutRef.current) {
                clearTimeout(reconnectTimeoutRef.current);
            }
            reconnectTimeoutRef.current = setTimeout(connect, 2500);
        };

        socket.onerror = (err) => {
            console.error('WebSocket error:', err);
            setConnected(false);
            setLoading(true);

            if (reconnectTimeoutRef.current) {
                clearTimeout(reconnectTimeoutRef.current);
            }
            reconnectTimeoutRef.current = setTimeout(connect, 2500);
        };

        socket.onmessage = (event) => {
            setLastMessageTime(Date.now());
            onMessage(event.data);
        };

        socketRef.current = socket;
    }, [onMessage]);

    const checkWebSocketActivity = React.useCallback(() => {
        const now = Date.now();
        if (lastMessageTime > 0 && now - lastMessageTime > 2500) {
            setConnected(false);
            setLoading(true);
            
            if (socketRef.current && socketRef.current.readyState !== WebSocket.CLOSED) {
                try {
                    socketRef.current.close();
                } catch (e) {
                    console.error('Error closing socket:', e);
                }
            }
            
            if (reconnectTimeoutRef.current) {
                clearTimeout(reconnectTimeoutRef.current);
            }
            reconnectTimeoutRef.current = setTimeout(connect, 500);
        }
    }, [lastMessageTime, connect]);

    const send = React.useCallback((data) => {
        if (!socketRef.current || socketRef.current.readyState !== WebSocket.OPEN) {
            setError('WebSocket not connected.');
            return false;
        }
        socketRef.current.send(typeof data === 'string' ? data : JSON.stringify(data));
        return true;
    }, [ socketRef, socketRef.current ]);

    React.useEffect(() => {
        connect();
        wsCheckIntervalRef.current = setInterval(checkWebSocketActivity, 1000);
        
        return () => {
            if (socketRef.current) {
                socketRef.current.close();
            }
            if (wsCheckIntervalRef.current) {
                clearInterval(wsCheckIntervalRef.current);
            }
            if (reconnectTimeoutRef.current) {
                clearTimeout(reconnectTimeoutRef.current);
            }
        };
    }, []);

    return { connected, loading, error, send };
};

const ConfirmModal = ({ isOpen, onClose, title, message, onYes, onNo, showCancel = false }) => {
    return (
        <Modal isOpen={isOpen} onClose={onClose} className="confirm-modal">
            <h3>{title}</h3>
            <p>{message}</p>
            <div className="confirm-buttons">
                <button onClick={() => { onYes(); onClose(); }}>Yes</button>
                <button onClick={() => { onNo(); onClose(); }}>No</button>
                {showCancel && <button onClick={onClose}>Cancel</button>}
            </div>
        </Modal>
    );
};

const App = () => {
    const [isModalOpen, setIsModalOpen] = React.useState(false);
    const [confirmModal, setConfirmModal] = React.useState({ isOpen: false, config: {} });
    const [systemInfo, setSystemInfo] = React.useState({
        heap: 0,
        temp: 0,
        bat: 0,
    });

    const [settings, setSettings] = React.useState({
        deviceInfo: {
            name: 'TBD',
            fwVersion: 'TBD',
            hwVersion: 'TBD',
            macAddress: '00:00:00:00:00:00',
            newFirmware: false,
        },
        power: {
            sleepTimeout: 60,
            deepSleepTimeout: 180,
            enableSleep: true,
            deepSleep: true,
            twoSleeps: true,
            fastCharge: true,
            disableSlowPhase: false,
            warpSpeed: 'slow',
            output: 5,
        },
        led: {
            brightness: 80,
        },
        connectivity: {
            bleTxPower: 'low',
            bleRecDelay: 3,
        },
        mouse: {
            sensitivity: 100,
        },
        buttons: {
            longPressMs: 750,
            keys: [
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
            ],
            longPress: [
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
                {
                    "acType": "keyboard_key",
                    "action": "KC_A",
                    "mods": []
                },
            ],
            encoder: {
                mode: "scroll",
                left: "KC_MS_WH_UP",
                right: "KC_MS_WH_DOWN",
                click: "KC_MS_WH_SWITCH",
            },
        },
    });

    const [statusMessage, setStatusMessage] = React.useState(null);
    const [statusType, setStatusType] = React.useState('info');
    const [otaProgress, setOtaProgress] = React.useState(0);
    const [otaInProgress, setOtaInProgress] = React.useState(false);
    const [deviceInfoExpanded, setDeviceInfoExpanded] = React.useState(false);
    const fileInputRef = React.useRef(null);
    const initialSettingsRef = React.useRef(null);

    const showStatus = React.useCallback((message, type) => {
        setStatusMessage(message);
        setStatusType(type);
        window.scrollTo(0, 0);

        setTimeout(() => {
            setStatusMessage(null);
        }, 15000);
    }, [ setStatusMessage, setStatusType ]);

    const handleWebSocketMessage = React.useCallback((data) => {
        try {
            const message = JSON.parse(data);

            switch (message.type) {
                case 'ota_progress':
                    if (message.content && message.content.progress !== undefined) {
                        const progress = typeof message.content === 'string'
                            ? JSON.parse(message.content).progress
                            : message.content.progress;

                        setOtaProgress(progress);

                        if (progress === 100) {
                            showStatus('Image updated. The device is rebooting, WiFi on. Please refresh this page, then verify OTA.', 'success');
                            setTimeout(() => {
                                setOtaInProgress(false);
                            }, 5000);
                        }
                    }
                    break;
                case 'settings':
                    if (message.content) {
                        const newSettings = {...settings, ...message.content};
                        setSettings(newSettings);
                        if (!initialSettingsRef.current) {
                            initialSettingsRef.current = JSON.stringify(newSettings);
                        }
                    }
                    break;
                case 'settings_update_status':
                    if (message.content.success) {
                        showStatus('Settings updated successfully. The device is restarting.', 'success');
                    } else {
                        showStatus(`Failed to update settings: ${message.content.error}`, 'error');
                    }
                    break;
                case 'log':
                    console.log('Server log:', message.content);
                    break;
                case 'ping':
                    if (message.content) {
                        try {
                            const pingData = typeof message.content === 'string'
                                ? JSON.parse(message.content)
                                : message.content;

                            setSystemInfo({
                                heap: pingData.heap || 0,
                                temp: pingData.temp || 0,
                                bat: pingData.bat || 0,
                            });
                        } catch (e) {
                            console.error('Error parsing ping data:', e);
                        }
                    }
                    break;
                default:
                    console.log('Unknown message type:', message.type);
            }
        } catch (error) {
            console.error('Error parsing WebSocket message:', error);
        }
    }, [ showStatus, setSettings, settings, setSystemInfo, systemInfo ]);

    const { connected, loading, error, send } = useWebSocket(handleWebSocketMessage);

    const requestSettings = React.useCallback(() => {
        if (send({
            type: 'command',
            command: 'get_settings'
        })) {
            // showStatus('Requesting settings...', 'info');
        }
    }, [send]);

    React.useEffect(() => {
        if (connected) {
            requestSettings();
        }
    }, [connected, requestSettings]);

    const saveSettings = () => {
        setConfirmModal({
            isOpen: true,
            config: {
                title: 'Save Settings',
                message: 'Do you want to keep WiFi and web stack on after reboot?',
                showCancel: true,
                onYes: () => {
                    if (send({
                        type: 'command',
                        command: 'update_settings',
                        content: { ...settings, keepWifi: true }
                    })) {
                        showStatus('Settings saved. WiFi kept on.', 'success');
                    }
                },
                onNo: () => {
                    if (send({
                        type: 'command',
                        command: 'update_settings',
                        content: { ...settings, keepWifi: false }
                    })) {
                        showStatus('Settings saved. You can close this page.', 'info');
                    }
                }
            }
        });
    };

    const updateSetting = (category, key, value) => {
        if (key === '') {
            return setSettings(prevSettings => {
                return { ...prevSettings, [category]: value };
            });
        }

        setSettings(prevSettings => {
            return {
                ...prevSettings,
                [category]: {
                    ...prevSettings[category],
                    [key]: value
                }
            };
        });
    };

    const min = 10;
    const minp = React.useMemo(() => Math.log(min), []);
    const maxp = React.useMemo(() => Math.log(100), []);
    const scale = React.useMemo(() => (maxp - minp) / (100 - min), [maxp, minp]);
    
    const toLinear = React.useCallback((logValue) => {
        return Math.round(((Math.log(logValue) - minp) / scale) + min);
    }, [minp, scale, min]);
    
    const toLog = React.useCallback((linearValue) => {
        return Math.round(Math.exp(minp + scale * (linearValue - min)));
    }, [minp, scale, min]);

    const onConfigChange = (config) => {
        updateSetting('buttons', 'keys', config.keys);
        updateSetting('buttons', 'longPress', config.longPress);
        updateSetting('buttons', 'encoder', config.encoder);
    }

    const handleOtaConfirm = () => {
        if (send({ type: 'ota_confirm' })) {
            showStatus('Done! The device is rebooting.', 'success');
        }
    }

    const reboot = () => {
        send({ type: 'reboot', content: { keepWifi: true } });
        showStatus('Done! The device is rebooting.', 'success');
    }

    const clearData = () => {
        setConfirmModal({
            isOpen: true,
            config: {
                title: 'Clear Data',
                message: 'Are you sure you want to restore default configuration?',
                onYes: () => {
                    send({ type: 'clear', content: { keepWifi: true } });
                    showStatus('Done! The device is rebooting.', 'success');
                },
                onNo: () => {}
            }
        });
    }

    const handleFirmwareUpload = () => {
        const fileInput = fileInputRef.current;
        if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
            showStatus('Please select a firmware file.', 'error');
            return;
        }

        const file = fileInput.files[0];
        const formData = new FormData();
        formData.append('firmware', file);

        setOtaInProgress(true);
        setOtaProgress(0);

        const req = new XMLHttpRequest();
        req.open('POST', '/upload', true);
        
        new Promise((resolve, reject) => {
            req.onload = () => {
                if (req.status >= 200 && req.status < 300) {
                    resolve(req.responseText);
                } else {
                    reject(new Error(`HTTP error ${req.status}`));
                }
            };
            req.onerror = () => reject(new Error('Network error'));
            req.send(file);
        })
        .catch(error => {
            showStatus(`Upload failed: ${error.message}`, 'error');
        });
    };

    if (loading || !connected) {
        return (
            <div id="loadingContainer">
                <div className="spinner"></div>
                <p>{connected ? 'Loading settings' : 'Waiting for connection…'}</p>
            </div>
        );
    }

    if (error) {
        return (
            <div className="container">
                <div className="status error">{error}</div>
                <button onClick={requestSettings}>Retry Connection</button>
            </div>
        );
    }

    return (
        <div>
            <Modal isOpen={isModalOpen} onClose={() => setIsModalOpen(false)} className="config-modal">
                {isModalOpen ? (
                    <HIDControlsConfigurator onClose={() => setIsModalOpen(false)} defaultValues={settings.buttons} onConfigChange={onConfigChange} />
                ) : null}
            </Modal>

            <h1>Device Settings</h1>

            <ConfirmModal 
                isOpen={confirmModal.isOpen}
                onClose={() => setConfirmModal({ isOpen: false, config: {} })}
                {...confirmModal.config}
            />

            <div className="container">
                {statusMessage && (
                    <div className={`status ${statusType}`}>
                        {statusMessage}
                    </div>
                )}

                <div className="header-controls">
                    <div>
                        <button className="save-button" onClick={saveSettings} disabled={!connected || otaInProgress || (initialSettingsRef.current && JSON.stringify(settings) === initialSettingsRef.current)}>
                            Save and restart
                        </button>
                        <button className="return-button" onClick={() => window.location.href = '/'} disabled={otaInProgress}>
                            Return
                        </button>
                    </div>

                    <div onClick={() => setDeviceInfoExpanded(!deviceInfoExpanded)} className="temp-display">
                        <h3>{deviceInfoExpanded ? '' : '▼'} {systemInfo.temp.toFixed(0)}°C</h3>
                    </div>
                </div>

                <div className="setting-group">
                    <div className={`device-info-panel ${deviceInfoExpanded ? 'expanded' : ''}`}>
                        <div className="setting-item">
                            <div className="setting-title">Firmware version</div>
                            <div>{settings.deviceInfo.fwVersion}</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">Hardware revision</div>
                            <div>{settings.deviceInfo.hwVersion}</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">MAC address</div>
                            <div>{settings.deviceInfo.macAddress}</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">Free heap</div>
                            <div>{(systemInfo.heap / 1000).toFixed(0)} kb</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">SoC temperature</div>
                            <div>{systemInfo.temp.toFixed(0)}°C</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">Battery level</div>
                            <div>{systemInfo.bat.toFixed(2)}V</div>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">Device name</div>
                            <input
                                type="text"
                                value={settings.deviceInfo.name}
                                onChange={(e) => updateSetting('deviceInfo', 'name', e.target.value)}
                                placeholder="Enter device name"
                            />
                        </div>
                    </div>
                </div>

                <div className="setting-group">
                    <h2>Connectivity</h2>

                    <div className="setting-item">
                        <div className="setting-title">BLE report rate</div>
                        <div className="setting-description">
                            For high speed (polling rate) devices, it is possible to choose higher BLE report rate.
                            Please note that it is not supported by the BLE standard and may or may not work in your case.
                            This option will not affect normal speed (125 rps) devices in any way.
                            The values are calibrated for 1000 Hz USB devices, other rates not supported.
                            Some modes might work better than others with your device(s).
                        </div>
                        <select
                            value={settings.power.warpSpeed}
                            onChange={(e) => updateSetting('power', 'warpSpeed', e.target.value)}
                        >
                            <option value="slow">±125 rps</option>
                            <option value="fast">±175 rps</option>
                            <option value="veryfast">±250 rps</option>
                        </select>
                    </div>

                    <div className="setting-item">
                        <div className="setting-title">BLE TX power</div>
                        <div className="setting-description">
                            Bluetooth transmission power level.
                        </div>
                        <select
                            value={settings.connectivity.bleTxPower}
                            onChange={(e) => updateSetting('connectivity', 'bleTxPower', e.target.value)}
                        >
                            <option value="n6">-6 dB</option>
                            <option value="n3">-3 dB</option>
                            <option value="n0">0 dB</option>
                            <option value="p3">+3 dB</option>
                            <option value="p6">+6 dB</option>
                            <option value="p9">+9 dB</option>
                        </select>
                    </div>
                </div>

                <div className="setting-group">
                    <h2>Device behavior</h2>

                    <div className="setting-item">
                        <div className="setting-title">On-device buttons</div>
                        <div className="setting-description">
                            Configure on-device buttons and the rotary encoder.
                        </div>
                        <button onClick={() => setIsModalOpen(true)}>Configure</button>
                    </div>

                    <div className="setting-item">
                        <div className="setting-title">Long press threshold</div>
                        <div className="setting-description">
                            Time in milliseconds to trigger long press action.
                        </div>
                        <input
                            type="number"
                            min="100"
                            max="5000"
                            value={settings.buttons?.longPressMs || 750}
                            onChange={(e) => updateSetting('buttons', 'longPressMs', parseInt(e.target.value))}
                        />
                    </div>

                    <div className="setting-item">
                        <div className="setting-title">Clear data</div>
                        <div className="setting-description">
                            Restore factory default configuration, remove saved devices.
                        </div>
                        <button className="error" onClick={() => clearData()}>Clear</button>
                    </div>
                </div>

                <div className="setting-group">
                    <h2>Power</h2>

                    <div className="setting-item">
                        <div className="setting-title">Fast charging</div>
                        <div className="setting-description">
                            Allows faster charging with current up to 1.5A.
                            Recommended for ±1200 mAh batteries or more.
                        </div>
                        <label className="toggle-switch">
                            <input
                                type="checkbox"
                                checked={settings.power.fastCharge}
                                onChange={(e) => updateSetting('power', 'fastCharge', e.target.checked)}
                            />
                            <span className="slider"></span>
                        </label>
                    </div>

                    <div className={`setting-item ${settings.power.fastCharge ? 'animate-visible' : 'animate-hidden'}`}>
                        <div className="setting-title">Limit charge to ±80%</div>
                        <div className="setting-description">
                            Will terminate charging early, it helps with long-term battery
                            health and makes charging process significantly faster.
                        </div>
                        <label className="toggle-switch">
                            <input
                                type="checkbox"
                                checked={settings.power.disableSlowPhase}
                                onChange={(e) => updateSetting('power', 'disableSlowPhase', e.target.checked)}
                            />
                            <span className="slider"></span>
                        </label>
                    </div>

                    <div className="setting-item">
                        <div className="setting-title">Enable sleep</div>
                        <div className="setting-description">
                            Enable device to enter sleep mode and disable Bluetooth when no events received in the
                            time window.
                        </div>
                        <label className="toggle-switch">
                            <input
                                type="checkbox"
                                checked={settings.power.enableSleep}
                                onChange={(e) => updateSetting('power', 'enableSleep', e.target.checked)}
                            />
                            <span className="slider"></span>
                        </label>
                    </div>

                    <div className={settings.power.enableSleep ? 'animate-visible' : 'animate-hidden'}>
                        <div className="setting-item">
                            <div className="setting-title">Deep sleep</div>
                            <div className="setting-description">
                                When enabled, USB device will not be able to wake up the device. You will have to press any button on the device itself.
                            </div>
                            <label className="toggle-switch">
                                <input
                                    type="checkbox"
                                    checked={settings.power.deepSleep}
                                    onChange={(e) => updateSetting('power', 'deepSleep', e.target.checked)}
                                />
                                <span className="slider"></span>
                            </label>
                        </div>

                        <div className={`setting-item ${settings.power.deepSleep ? 'animate-visible' : 'animate-hidden'}`}>
                            <div className="setting-title">Separate sleep timeouts</div>
                            <div className="setting-description">
                                If disabled, the device will go into the deep sleep immediately after entering light sleep.
                            </div>
                            <label className="toggle-switch">
                                <input
                                    type="checkbox"
                                    checked={settings.power.twoSleeps}
                                    onChange={(e) => updateSetting('power', 'twoSleeps', e.target.checked)}
                                />
                                <span className="slider"></span>
                            </label>
                        </div>

                        <div className="setting-item">
                            <div className="setting-title">
                                {settings.power.twoSleeps 
                                    ? 'Light sleep timeout (seconds)'
                                    : 'Sleep timeout (seconds)'}
                            </div>
                            <div className="setting-description">
                                Time without USB events in seconds before device enters light sleep mode. 
                                In light sleep, Bluetooth is turned off but the device still awaits for USB events to turn it on immediately.
                            </div>
                            <input
                                type="number"
                                min="20"
                                max="3600"
                                value={settings.power.sleepTimeout}
                                onChange={(e) => updateSetting('power', 'sleepTimeout', parseInt(e.target.value))}
                            />
                        </div>

                        <div className={`setting-item ${settings.power.twoSleeps && settings.power.deepSleep ? 'animate-visible' : 'animate-hidden'}`}>
                            <div className="setting-title">Deep sleep timeout (seconds)</div>
                            <div className="setting-description">
                                Time without USB events in seconds before device enters deep sleep mode.
                                In deep sleep, the device will do nothing and only wake up when you press any button on the bridge device itself.
                            </div>
                            <input
                                type="number"
                                min="20"
                                max="3600"
                                value={settings.power.deepSleepTimeout}
                                onChange={(e) => updateSetting('power', 'deepSleepTimeout', parseInt(e.target.value))}
                            />
                        </div>
                    </div>
                </div>

                <div className="setting-group">
                    <h2>Miscellaneous</h2>

                    <div className="setting-item">
                        <div className="setting-title">Mouse sensitivity</div>
                        <div className="setting-description">
                            For pointer devices, it's possible to apply the sensitity modifer on the device layer instead of your OS.
                            However, it is recommended to use OS settings and keep this preference untouched.
                        </div>
                        <div className="range-container">
                            <input
                                type="range"
                                min="50"
                                max="300"
                                value={settings.mouse.sensitivity}
                                onChange={(e) => updateSetting('mouse', 'sensitivity', parseInt(e.target.value))}
                            />
                            <span className="range-value">{settings.mouse.sensitivity}%</span>
                        </div>
                    </div>

                    <div className="setting-item">
                        <div className="setting-title">Brightness</div>
                        <div className="setting-description">
                            Global LED brightness percentage.
                        </div>
                        <input
                            type="range"
                            min="10"
                            max="100"
                            value={toLinear(settings.led.brightness)}
                            onChange={(e) => updateSetting('led', 'brightness', toLog(parseInt(e.target.value)))}
                        />
                    </div>
                </div>

                <div className="setting-group">
                    <h2>Firmware</h2>

                    <div className="setting-item">
                        <div className="setting-description">
                            Upload a new firmware file (.bin) to update the device.
                            It will reboot after the successful update.
                            Faulty firmware will be rolled back automatically.
                        </div>

                        {otaInProgress ? (
                            <div>
                                <div className="progress-container">
                                    <div
                                        className="progress-bar"
                                        style={{width: `${otaProgress}%`}}
                                    >
                                        <span>{otaProgress}%</span>
                                    </div>
                                </div>
                            </div>
                        ) : !settings.deviceInfo.newFirmware ? (
                            <div>
                                <div className="file-input-container">
                                    <input
                                        type="file"
                                        ref={fileInputRef}
                                        accept=".bin"
                                        style={{ display: 'none' }}
                                    />
                                    <button onClick={() => fileInputRef.current?.click()}>
                                        Choose file…
                                    </button>
                                    <button
                                        onClick={handleFirmwareUpload}
                                        disabled={!connected || !fileInputRef.current?.files?.length}
                                    >
                                        Flash
                                    </button>
                                </div>
                            </div>
                        ) : (
                            <div>
                                <div className="file-input-container">
                                    <button onClick={handleOtaConfirm}>
                                        Verify OTA image
                                    </button>

                                    <button onClick={reboot} className="error">
                                        Reject update
                                    </button>
                                </div>
                            </div>
                        )}
                    </div>
                </div>
            </div>
        </div>
    );
};

ReactDOM.render(<App/>, document.getElementById('root'));
