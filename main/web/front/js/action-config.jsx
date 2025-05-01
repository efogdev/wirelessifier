const HIDControlsConfigurator = ({ numKeys = 4, onConfigChange }) => {
  const actionTypes = [
    { key: 'keyboard_key', value: 'Keyboard Key' },
    { key: 'keyboard_combo', value: 'Keyboard Combo' },
    { key: 'mouse_button', value: 'Mouse Button' },
    { key: 'system_control', value: 'System Control' }
  ];

  const keyboardKeys = [
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
    "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Escape", "Tab", "CapsLock", "Space", "Enter", "Backspace", "Delete",
    "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
    "Home", "End", "PageUp", "PageDown", "Insert", "PrintScreen"
  ];

  const modifiers = [ "Ctrl", "Shift", "Alt", "Win" ];

  const actionOptions = {
    keyboard_key: keyboardKeys,
    mouse_button: [
      "Left Click", "Right Click", "Middle Click"
    ],
    system_control: [
      "Volume Up", "Volume Down", "Mute", "Media Play/Pause", "Media Next", "Media Previous",
      "Brightness Up", "Brightness Down", "System Forward", "System Back"
    ]
  };

  const encoderModes = [
    { key: 'standard_actions', value: 'Standard Actions' },
    { key: 'volume_control', value: 'Volume Control' },
    { key: 'media_control', value: 'Media Control' },
    { key: 'system_navigation', value: 'System Navigation' },
    { key: 'scroll', value: 'Scroll' }
  ];

  const [ keyConfigs, setKeyConfigs ] = React.useState(
    Array(numKeys).fill().map((_, index) => ({
      actionType: 'keyboard_key',
      action: actionOptions.keyboard_key[0],
      selectedModifiers: [],
      selectedKey: keyboardKeys[0],
    }))
  );

  const [ encoderConfig, setEncoderConfig ] = React.useState({
    mode: 'standard_actions',
    rotateLeft: "Volume Down",
    rotateRight: "Volume Up",
    click: "Mute"
  });

  React.useEffect(() => {
    if (keyConfigs.length !== numKeys) {
      setKeyConfigs(
        Array(numKeys).fill().map((_, index) => {
          return index < keyConfigs.length
            ? keyConfigs[index]
            : {
              actionType: 'keyboard_key',
              action: actionOptions.keyboard_key[0],
              selectedModifiers: [],
              selectedKey: keyboardKeys[0]
            };
        })
      );
    }
  }, [ numKeys ]);

  const handleKeyConfigChange = (index, field, value) => {
    const newConfigs = [ ...keyConfigs ];
    newConfigs[index] = {
      ...newConfigs[index],
      [field]: value
    };

    if (field === "actionType") {
      if (value !== 'keyboard_combo') {
        newConfigs[index].action = actionOptions[value][0];
      } else {
        newConfigs[index].selectedModifiers = [];
        newConfigs[index].selectedKey = keyboardKeys[0];
      }
    }

    setKeyConfigs(newConfigs);
    onConfigChange?.({ keys: newConfigs, encoder: encoderConfig });
  };

  const handleModifierToggle = (index, modifier) => {
    const newConfigs = [ ...keyConfigs ];
    const currentModifiers = [ ...newConfigs[index].selectedModifiers ];

    if (currentModifiers.includes(modifier)) {
      newConfigs[index].selectedModifiers = currentModifiers.filter(mod => mod !== modifier);
    } else {
      newConfigs[index].selectedModifiers = [ ...currentModifiers, modifier ];
    }

    setKeyConfigs(newConfigs);
    onConfigChange?.({ keys: newConfigs, encoder: encoderConfig });
  };

  const handleComboKeyChange = (index, key) => {
    const newConfigs = [ ...keyConfigs ];
    newConfigs[index].selectedKey = key;
    setKeyConfigs(newConfigs);
    onConfigChange?.({ keys: newConfigs, encoder: encoderConfig });
  };

  const handleEncoderModeChange = (mode) => {
    let rotateLeft, rotateRight, click;

    switch (mode) {
      case 'volume_control':
        rotateLeft = "Volume Down";
        rotateRight = "Volume Up";
        click = "Mute";
        break;
      case 'media_control':
        rotateLeft = "Media Previous";
        rotateRight = "Media Next";
        click = "Media Play/Pause";
        break;
      case 'system_navigation':
        rotateLeft = "System Back";
        rotateRight = "System Forward";
        click = "";
        break;
      case 'scroll':
        rotateLeft = "Scroll Down";
        rotateRight = "Scroll Up";
        click = "Switch Vertical/Horizontal";
        break;
      default:
        rotateLeft = "Volume Down";
        rotateRight = "Volume Up";
        click = "Mute";
    }

    const newConfig = {
      mode,
      rotateLeft,
      rotateRight,
      click
    };
    setEncoderConfig(newConfig);
    onConfigChange?.({ keys: keyConfigs, encoder: newConfig });
  };

  const handleEncoderActionChange = (event, action) => {
    const newConfig = {
      ...encoderConfig,
      [action]: event.target.value
    };
    setEncoderConfig(newConfig);
    onConfigChange?.({ keys: keyConfigs, encoder: newConfig });
  };

  const renderEncoderConfig = () => {
    if (encoderConfig.mode === 'standard_actions') {
      return (
        <div className="setting-group">
          <div className="setting-item">
            <div className="setting-title">Rotate Left</div>
            <select
              value={encoderConfig.rotateLeft}
              onChange={(e) => handleEncoderActionChange(e, "rotateLeft")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`left-${idx}`} value={option}>{option}</option>
              ))}
            </select>
          </div>
          <div className="setting-item">
            <div className="setting-title">Rotate Right</div>
            <select
              value={encoderConfig.rotateRight}
              onChange={(e) => handleEncoderActionChange(e, "rotateRight")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`right-${idx}`} value={option}>{option}</option>
              ))}
            </select>
          </div>
          <div className="setting-item">
            <div className="setting-title">Click</div>
            <select
              value={encoderConfig.click}
              onChange={(e) => handleEncoderActionChange(e, "click")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`click-${idx}`} value={option}>{option}</option>
              ))}
            </select>
          </div>
        </div>
      );
    } else {
      return (
        <div className="setting-item">
          <div className="setting-title">Rotate Left:</div>
          <div className="setting-description">{encoderConfig.rotateLeft}</div>

          <div className="setting-title">Rotate Right:</div>
          <div className="setting-description">{encoderConfig.rotateRight}</div>

          <div className="setting-title">Click:</div>
          <div className="setting-description">{encoderConfig.click}</div>
        </div>
      );
    }
  };

  const formatKeyboardCombo = (config) => {
    if (config.selectedModifiers.length === 0) {
      return config.selectedKey;
    }
    return `${config.selectedModifiers.join('+')}+${config.selectedKey}`;
  };

  return (
    <div className="configurator">
      <div className="setting-group">
        <h3>Buttons</h3>

        <div className="setting-group">
          {keyConfigs.map((config, idx) => (
            <div key={`key-${idx}`} className="container">
              <div className="setting-title secondary">Button #{idx + 1}</div>

              <div className="setting-item">
                <div className="setting-title">Action Type</div>
                <select
                  value={config.actionType}
                  onChange={(e) => handleKeyConfigChange(idx, "actionType", e.target.value)}
                >
                  {actionTypes.map((type) => (
                    <option key={type.key} value={type.key}>{type.value}</option>
                  ))}
                </select>

                {config.actionType === 'keyboard_combo' ? (
                  <div className="setting-group">
                    <div className="setting-item">
                      <div className="setting-title">Modifiers</div>
                      <div>
                        {modifiers.map((modifier, modIdx) => (
                          <button
                            key={`mod-${modIdx}`}
                            className={config.selectedModifiers.includes(modifier) ? "" : "success"}
                            onClick={() => handleModifierToggle(idx, modifier)}
                            type="button"
                          >
                            {modifier}
                          </button>
                        ))}
                      </div>
                    </div>
                    <div className="setting-item">
                      <div className="setting-title">Key</div>
                      <select
                        value={config.selectedKey}
                        onChange={(e) => handleComboKeyChange(idx, e.target.value)}
                      >
                        {keyboardKeys.map((key, keyIdx) => (
                          <option key={`combo-key-${keyIdx}`} value={key}>{key}</option>
                        ))}
                      </select>
                    </div>
                  </div>
                ) : (
                  <div className="setting-item">
                    <div className="setting-title">Action</div>
                    <select
                      value={config.action}
                      onChange={(e) => handleKeyConfigChange(idx, "action", e.target.value)}
                    >
                      {actionOptions[config.actionType].map((action, actionIdx) => (
                        <option key={`action-${actionIdx}`} value={action}>{action}</option>
                      ))}
                    </select>
                  </div>
                )}
              </div>
            </div>
          ))}
        </div>
      </div>

      <div className="setting-group">
        <h3>Encoder</h3>

        <div className="container">
          <div className="setting-item">
            <div className="setting-title">Mode</div>
            <select
              value={encoderConfig.mode}
              onChange={(e) => handleEncoderModeChange(e.target.value)}
            >
              {encoderModes.map((mode) => (
                <option key={mode.key} value={mode.key}>{mode.value}</option>
              ))}
            </select>
          </div>

          {renderEncoderConfig()}
        </div>
      </div>
    </div>
  );
};

window._HIDControlsConfigurator = HIDControlsConfigurator;