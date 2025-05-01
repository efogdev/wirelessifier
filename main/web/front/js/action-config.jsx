const HIDControlsConfigurator = ({ numKeys = 4, onConfigChange }) => {
  const actionTypes = [
    { key: 'keyboard_key', value: 'Keyboard Key' },
    { key: 'keyboard_combo', value: 'Keyboard Combo' },
    { key: 'mouse_button', value: 'Mouse Button' },
    { key: 'system_control', value: 'System Control' }
  ];

  const keyboardKeys = [
    { key: 'KC_A', value: 'a' },
    { key: 'KC_B', value: 'b' },
    { key: 'KC_C', value: 'c' },
    { key: 'KC_D', value: 'd' },
    { key: 'KC_E', value: 'e' },
    { key: 'KC_F', value: 'f' },
    { key: 'KC_G', value: 'g' },
    { key: 'KC_H', value: 'h' },
    { key: 'KC_I', value: 'i' },
    { key: 'KC_J', value: 'j' },
    { key: 'KC_K', value: 'k' },
    { key: 'KC_L', value: 'l' },
    { key: 'KC_M', value: 'm' },
    { key: 'KC_N', value: 'n' },
    { key: 'KC_O', value: 'o' },
    { key: 'KC_P', value: 'p' },
    { key: 'KC_Q', value: 'q' },
    { key: 'KC_R', value: 'r' },
    { key: 'KC_S', value: 's' },
    { key: 'KC_T', value: 't' },
    { key: 'KC_U', value: 'u' },
    { key: 'KC_V', value: 'v' },
    { key: 'KC_W', value: 'w' },
    { key: 'KC_X', value: 'x' },
    { key: 'KC_Y', value: 'y' },
    { key: 'KC_Z', value: 'z' },
    { key: 'KC_0', value: '0' },
    { key: 'KC_1', value: '1' },
    { key: 'KC_2', value: '2' },
    { key: 'KC_3', value: '3' },
    { key: 'KC_4', value: '4' },
    { key: 'KC_5', value: '5' },
    { key: 'KC_6', value: '6' },
    { key: 'KC_7', value: '7' },
    { key: 'KC_8', value: '8' },
    { key: 'KC_9', value: '9' },
    { key: 'KC_F1', value: 'F1' },
    { key: 'KC_F2', value: 'F2' },
    { key: 'KC_F3', value: 'F3' },
    { key: 'KC_F4', value: 'F4' },
    { key: 'KC_F5', value: 'F5' },
    { key: 'KC_F6', value: 'F6' },
    { key: 'KC_F7', value: 'F7' },
    { key: 'KC_F8', value: 'F8' },
    { key: 'KC_F9', value: 'F9' },
    { key: 'KC_F10', value: 'F10' },
    { key: 'KC_F11', value: 'F11' },
    { key: 'KC_F12', value: 'F12' },
    { key: 'KC_F13', value: 'F13' },
    { key: 'KC_F14', value: 'F14' },
    { key: 'KC_F15', value: 'F15' },
    { key: 'KC_F16', value: 'F16' },
    { key: 'KC_F17', value: 'F17' },
    { key: 'KC_F18', value: 'F18' },
    { key: 'KC_F19', value: 'F19' },
    { key: 'KC_F20', value: 'F20' },
    { key: 'KC_F21', value: 'F21' },
    { key: 'KC_F22', value: 'F22' },
    { key: 'KC_F23', value: 'F23' },
    { key: 'KC_F24', value: 'F24' },
    { key: 'KC_ESCAPE', value: 'Escape' },
    { key: 'KC_TAB', value: 'Tab' },
    { key: 'KC_CAPS_LOCK', value: 'CapsLock' },
    { key: 'KC_SPACE', value: 'Space' },
    { key: 'KC_ENTER', value: 'Enter' },
    { key: 'KC_BACKSPACE', value: 'Backspace' },
    { key: 'KC_DELETE', value: 'Delete' },
    { key: 'KC_UP', value: 'Arrow Up' },
    { key: 'KC_DOWN', value: 'Arrow Down' },
    { key: 'KC_LEFT', value: 'Arrow Left' },
    { key: 'KC_RIGHT', value: 'Arrow Right' },
    { key: 'KC_HOME', value: 'Home' },
    { key: 'KC_END', value: 'End' },
    { key: 'KC_PAGE_UP', value: 'Page Up' },
    { key: 'KC_PAGE_DOWN', value: 'Page Down' },
    { key: 'KC_INSERT', value: 'Insert' },
    { key: 'KC_PRINT_SCREEN', value: 'Print Screen' },
    { key: 'KC_SCROLL_LOCK', value: 'Scroll Lock' },
    { key: 'KC_PAUSE', value: 'Pause' },
    { key: 'KC_NUM_LOCK', value: 'Num Lock' },
    { key: 'KC_MENU', value: 'Menu' },
    { key: 'KC_EQUAL', value: 'Equal' },
    { key: 'KC_MINUS', value: 'Minus' },
    { key: 'KC_LEFT_BRACKET', value: 'Left Bracket' },
    { key: 'KC_RIGHT_BRACKET', value: 'Right Bracket' },
    { key: 'KC_BACKSLASH', value: 'Backslash' },
    { key: 'KC_SEMICOLON', value: 'Semicolon' },
    { key: 'KC_QUOTE', value: 'Quote' },
    { key: 'KC_GRAVE', value: 'Grave' },
    { key: 'KC_COMMA', value: 'Comma' },
    { key: 'KC_DOT', value: 'Period' },
    { key: 'KC_SLASH', value: 'Slash' }
  ];

  const mouseButtons = [
    { key: 'KC_MS_BTN1', value: 'Left Click' },
    { key: 'KC_MS_BTN2', value: 'Right Click' },
    { key: 'KC_MS_BTN3', value: 'Middle Click' }
  ];

  const systemControls = [
    { key: 'KC_AUDIO_VOL_UP', value: 'Volume Up' },
    { key: 'KC_AUDIO_VOL_DOWN', value: 'Volume Down' },
    { key: 'KC_AUDIO_MUTE', value: 'Mute' },
    { key: 'KC_MEDIA_PLAY_PAUSE', value: 'Media Play/Pause' },
    { key: 'KC_MEDIA_NEXT_TRACK', value: 'Media Next' },
    { key: 'KC_MEDIA_PREV_TRACK', value: 'Media Previous' },
    { key: 'KC_BRIGHTNESS_UP', value: 'Brightness Up' },
    { key: 'KC_BRIGHTNESS_DOWN', value: 'Brightness Down' },
    { key: 'KC_WWW_FORWARD', value: 'System Forward' },
    { key: 'KC_WWW_BACK', value: 'System Back' },
    { key: 'KC_EXECUTE', value: 'Execute' },
    { key: 'KC_HELP', value: 'Help' },
    { key: 'KC_MENU', value: 'Menu' },
    { key: 'KC_SELECT', value: 'Select' },
    { key: 'KC_STOP', value: 'Stop' },
    { key: 'KC_AGAIN', value: 'Again' },
    { key: 'KC_UNDO', value: 'Undo' },
    { key: 'KC_CUT', value: 'Cut' },
    { key: 'KC_COPY', value: 'Copy' },
    { key: 'KC_PASTE', value: 'Paste' },
    { key: 'KC_FIND', value: 'Find' },
    { key: 'KC_MEDIA_STOP', value: 'Media Stop' },
    { key: 'KC_MEDIA_EJECT', value: 'Media Eject' },
    { key: 'KC_MEDIA_SELECT', value: 'Media Select' },
    { key: 'KC_MAIL', value: 'Mail' },
    { key: 'KC_CALCULATOR', value: 'Calculator' },
    { key: 'KC_MY_COMPUTER', value: 'My Computer' },
    { key: 'KC_WWW_SEARCH', value: 'WWW Search' },
    { key: 'KC_WWW_HOME', value: 'WWW Home' },
    { key: 'KC_WWW_STOP', value: 'WWW Stop' },
    { key: 'KC_WWW_REFRESH', value: 'WWW Refresh' },
    { key: 'KC_WWW_FAVORITES', value: 'WWW Favorites' },
    { key: 'KC_MEDIA_FAST_FORWARD', value: 'Media Fast Forward' },
    { key: 'KC_MEDIA_REWIND', value: 'Media Rewind' },
    { key: 'KC_CONTROL_PANEL', value: 'Control Panel' },
    { key: 'KC_ASSISTANT', value: 'Assistant' },
    { key: 'KC_MISSION_CONTROL', value: 'Mission Control' },
    { key: 'KC_LAUNCHPAD', value: 'Launchpad' }
  ];

  const modifiers = [ "Ctrl", "Shift", "Alt", "Win" ];

  const actionOptions = {
    keyboard_key: keyboardKeys,
    mouse_button: mouseButtons,
    system_control: systemControls
  };

  const encoderModes = [
    { key: 'standard_actions', value: 'Standard Actions' },
    { key: 'volume_control', value: 'Volume Control' },
    { key: 'media_control', value: 'Media Control' },
    { key: 'system_navigation', value: 'System Navigation' },
    { key: 'cursor_fine', value: 'Curson Fine Control' },
    { key: 'scroll', value: 'Scroll' }
  ];

  const [ keyConfigs, setKeyConfigs ] = React.useState(
    Array(numKeys).fill().map((_, index) => ({
      actionType: 'keyboard_key',
      action: actionOptions.keyboard_key[0].value,
      selectedModifiers: [],
      selectedKey: keyboardKeys[0].value,
    }))
  );

  const [ encoderConfig, setEncoderConfig ] = React.useState({
    mode: 'standard_actions',
    rotateLeft: "KC_AUDIO_VOL_DOWN",
    rotateRight: "KC_AUDIO_VOL_UP",
    click: "KC_AUDIO_MUTE"
  });

  React.useEffect(() => {
    if (keyConfigs.length !== numKeys) {
      setKeyConfigs(
        Array(numKeys).fill().map((_, index) => {
          return index < keyConfigs.length
            ? keyConfigs[index]
            : {
              actionType: 'keyboard_key',
              action: actionOptions.keyboard_key[0].value,
              selectedModifiers: [],
              selectedKey: keyboardKeys[0].value
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
        newConfigs[index].action = actionOptions[value][0].value;
      } else {
        newConfigs[index].selectedModifiers = [];
        newConfigs[index].selectedKey = keyboardKeys[0].value;
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
        rotateLeft = "KC_AUDIO_VOL_DOWN";
        rotateRight = "KC_AUDIO_VOL_UP";
        click = "KC_AUDIO_MUTE";
        break;
      case 'media_control':
        rotateLeft = "KC_MEDIA_PREV_TRACK";
        rotateRight = "KC_MEDIA_NEXT_TRACK";
        click = "KC_MEDIA_PLAY_PAUSE";
        break;
      case 'system_navigation':
        rotateLeft = "KC_WWW_BACK";
        rotateRight = "KC_WWW_FORWARD";
        click = "";
        break;
      case 'scroll':
        rotateLeft = "KC_MS_WH_DOWN";
        rotateRight = "KC_MS_WH_UP";
        click = "KC_MS_WH_SWITCH";
        break;
      default:
        rotateLeft = "KC_AUDIO_VOL_DOWN";
        rotateRight = "KC_AUDIO_VOL_UP";
        click = "KC_AUDIO_MUTE";
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
    const selectedOption = Object.values(actionOptions)
      .flat()
      .find(option => option.value === event.target.value);

    const newConfig = {
      ...encoderConfig,
      [action]: selectedOption.key
    };
    setEncoderConfig(newConfig);
    onConfigChange?.({ keys: keyConfigs, encoder: newConfig });
  };

  const getDisplayValue = (keyCode) => {
    const option = Object.values(actionOptions)
      .flat()
      .find(option => option.key === keyCode);
    return option ? option.value : keyCode;
  };

  const renderEncoderConfig = () => {
    if (encoderConfig.mode === 'standard_actions') {
      return (
        <div className="setting-group">
          <div className="setting-item">
            <div className="setting-title">Rotate Left</div>
            <select
              value={getDisplayValue(encoderConfig.rotateLeft)}
              onChange={(e) => handleEncoderActionChange(e, "rotateLeft")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`left-${idx}`} value={option.value}>{option.value}</option>
              ))}
            </select>
          </div>
          <div className="setting-item">
            <div className="setting-title">Rotate Right</div>
            <select
              value={getDisplayValue(encoderConfig.rotateRight)}
              onChange={(e) => handleEncoderActionChange(e, "rotateRight")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`right-${idx}`} value={option.value}>{option.value}</option>
              ))}
            </select>
          </div>
          <div className="setting-item">
            <div className="setting-title">Click</div>
            <select
              value={getDisplayValue(encoderConfig.click)}
              onChange={(e) => handleEncoderActionChange(e, "click")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`click-${idx}`} value={option.value}>{option.value}</option>
              ))}
            </select>
          </div>
        </div>
      );
    } else {
      return (
        <div className="setting-item">
          <div className="setting-title">Rotate Left:</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.rotateLeft)}</div>

          <div className="setting-title">Rotate Right:</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.rotateRight)}</div>

          <div className="setting-title">Click:</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.click)}</div>
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
                          <option key={`combo-key-${keyIdx}`} value={key.value}>{key.value}</option>
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
                        <option key={`action-${actionIdx}`} value={action.value}>{action.value}</option>
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
