const HIDControlsConfigurator = ({ numKeys = 4, defaultValues, onConfigChange }) => {
  const acTypes = [
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
    { key: 'KC_MS_BTN3', value: 'Middle Click' },
    { key: 'KC_MS_BTN5', value: 'Forward' },
    { key: 'KC_MS_BTN4', value: 'Back' },
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
    { key: 'KC_MS_BTN5', value: 'System Forward' },
    { key: 'KC_MS_BTN4', value: 'System Back' },
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
    { key: 'KC_LAUNCHPAD', value: 'Launchpad' },
  ];

  const specialKeys = [
    { key: 'KC_CURSOR_BACK', value: 'Cursor Back' },
    { key: 'KC_CURSOR_FORWARD', value: 'Cursor Forward' },
    { key: 'KC_CURSOR_SWITCH', value: 'Cursor: Switch Axis' },
    { key: 'KC_MS_WH_DOWN', value: 'Wheel Down/Left' },
    { key: 'KC_MS_WH_UP', value: 'Wheel Up/Right' },
    { key: 'KC_MS_WH_SWITCH', value: 'Wheel: Switch Axis' },
    
  ];

  const modifiers = [ "Ctrl", "Shift", "Alt", "Win" ];

  const actionOptions = {
    keyboard_key: keyboardKeys,
    keyboard_combo: keyboardKeys,
    mouse_button: mouseButtons,
    system_control: systemControls,
    special: specialKeys,
  };

  const encoderModes = [
    { key: 'standard_actions', value: 'Standard Actions' },
    { key: 'volume_control', value: 'Volume Control' },
    { key: 'media_control', value: 'Media Control' },
    { key: 'system_navigation', value: 'System Navigation' },
    { key: 'cursor_fine', value: 'Curson Fine Control' },
    { key: 'scroll', value: 'Scroll' }
  ];

  const [ keyConfigs, setKeyConfigs ] = React.useState(defaultValues.keys);
  const [ longPressConfigs, setLongPressConfigs ] = React.useState(defaultValues.longPress);
  const [ encoderConfig, setEncoderConfig ] = React.useState(defaultValues.encoder);

  const handleKeyConfigChange = (index, field, value, isLongPress = false) => {
    const newConfigs = isLongPress ? [ ...longPressConfigs ] : [ ...keyConfigs ];
    newConfigs[index][field] = value;

    if (field === "acType") {
      if (value !== 'keyboard_combo') {
        newConfigs[index].action = actionOptions[value][0].key;
      } else {
        newConfigs[index].mods = [];
        newConfigs[index].action = keyboardKeys[0].key;
      }
    }

    if (isLongPress) {
      setLongPressConfigs(newConfigs);
      onConfigChange?.({ keys: keyConfigs, longPress: newConfigs, encoder: encoderConfig });
    } else {
      setKeyConfigs(newConfigs);
      onConfigChange?.({ keys: newConfigs, longPress: longPressConfigs, encoder: encoderConfig });
    }
  };

  const handleModifierToggle = (index, modifier, isLongPress = false) => {
    const newConfigs = isLongPress ? [ ...longPressConfigs ] : [ ...keyConfigs ];
    const currentModifiers = [ ...newConfigs[index].mods ];

    if (currentModifiers.includes(modifier)) {
      newConfigs[index].mods = currentModifiers.filter(mod => mod !== modifier);
    } else {
      newConfigs[index].mods = [ ...currentModifiers, modifier ];
    }

    if (isLongPress) {
      setLongPressConfigs(newConfigs);
      onConfigChange?.({ keys: keyConfigs, longPress: newConfigs, encoder: encoderConfig });
    } else {
      setKeyConfigs(newConfigs);
      onConfigChange?.({ keys: newConfigs, longPress: longPressConfigs, encoder: encoderConfig });
    }
  };

  const handleComboKeyChange = (index, key, isLongPress = false) => {
    const newConfigs = isLongPress ? [ ...longPressConfigs ] : [ ...keyConfigs ];
    newConfigs[index].action = key;
    
    if (isLongPress) {
      setLongPressConfigs(newConfigs);
      onConfigChange?.({ keys: keyConfigs, longPress: newConfigs, encoder: encoderConfig });
    } else {
      setKeyConfigs(newConfigs);
      onConfigChange?.({ keys: newConfigs, longPress: longPressConfigs, encoder: encoderConfig });
    }
  };

  const handleEncoderModeChange = (mode) => {
    let left, right, click;

    switch (mode) {
      case 'volume_control':
        left = "KC_AUDIO_VOL_DOWN";
        right = "KC_AUDIO_VOL_UP";
        click = "KC_AUDIO_MUTE";
        break;
      case 'media_control':
        left = "KC_MEDIA_PREV_TRACK";
        right = "KC_MEDIA_NEXT_TRACK";
        click = "KC_MEDIA_PLAY_PAUSE";
        break;
      case 'system_navigation':
        left = "KC_MS_BTN4";
        right = "KC_MS_BTN5";
        click = "KC_WWW_HOME";
        break;
      case 'cursor_fine':
        left = "KC_CURSOR_BACK";
        right = "KC_CURSOR_FORWARD";
        click = "KC_CURSOR_SWITCH";
        break;
      case 'scroll':
        left = "KC_MS_WH_DOWN";
        right = "KC_MS_WH_UP";
        click = "KC_MS_WH_SWITCH";
        break;
      default:
        left = "KC_AUDIO_VOL_DOWN";
        right = "KC_AUDIO_VOL_UP";
        click = "KC_AUDIO_MUTE";
    }

    const newConfig = {
      mode,
      left,
      right,
      click
    };
    setEncoderConfig(newConfig);
    onConfigChange?.({ keys: keyConfigs, encoder: newConfig });
  };

  const handleEncoderActionChange = (event, action) => {
    const selectedOption = Object.values(actionOptions)
      .flat()
      .find(option => option.key === event.target.value);

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
              value={encoderConfig.left}
              onChange={(e) => handleEncoderActionChange(e, "left")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`left-${idx}`} value={option.key}>{option.value}</option>
              ))}
            </select>
          </div>
          <div className="setting-item">
            <div className="setting-title">Rotate Right</div>
            <select
              value={encoderConfig.right}
              onChange={(e) => handleEncoderActionChange(e, "right")}
            >
              {Object.values(actionOptions).flat().map((option, idx) => (
                <option key={`right-${idx}`} value={option.key}>{option.value}</option>
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
                <option key={`click-${idx}`} value={option.key}>{option.value}</option>
              ))}
            </select>
          </div>
        </div>
      );
    } else if (encoderConfig.mode === 'system_navigation') {
      return (
        <div className="setting-group">
          <div className="setting-item">
            <div className="setting-title">Rotate Left</div>
            <div className="setting-description">{getDisplayValue(encoderConfig.left)}</div>
          </div>
          <div className="setting-item">
            <div className="setting-title">Rotate Right</div>
            <div className="setting-description">{getDisplayValue(encoderConfig.right)}</div>
          </div>
          <div className="setting-item">
            <div className="setting-title">Click</div>
            <select
              value={encoderConfig.click}
              onChange={(e) => handleEncoderActionChange(e, "click")}
            >
              {systemControls.map((option, idx) => (
                <option key={`click-${idx}`} value={option.key}>{option.value}</option>
              ))}
            </select>
          </div>
        </div>
      );
    } else {
      return (
        <div className="setting-item">
          <div className="setting-title">Rotate Left</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.left)}</div>

          <div className="setting-title">Rotate Right</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.right)}</div>

          <div className="setting-title">Click</div>
          <div className="setting-description">{getDisplayValue(encoderConfig.click)}</div>
        </div>
      );
    }
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
                <div className="setting-title">Click</div>
                <select
                  value={config.acType}
                  onChange={(e) => handleKeyConfigChange(idx, "acType", e.target.value)}
                >
                  {acTypes.map((type) => (
                    <option key={type.key} value={type.key}>{type.value}</option>
                  ))}
                </select>

                {config.acType === 'keyboard_combo' ? (
                  <div className="setting-group">
                    <div className="setting-item">
                      <div className="setting-title">Modifiers</div>
                      <div>
                        {modifiers.map((modifier, modIdx) => (
                          <button
                            key={`mod-${modIdx}`}
                            className={config.mods.includes(modifier) ? "" : "success"}
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
                        value={config.action}
                        onChange={(e) => handleKeyConfigChange(idx, "action", e.target.value)}
                      >
                        {keyboardKeys.map((key, keyIdx) => (
                          <option key={`combo-key-${keyIdx}`} value={key.key}>{key.value}</option>
                        ))}
                      </select>
                    </div>
                  </div>
                ) : (
                  <div className="setting-item">
                    <div className="setting-title">Key</div>
                    <select
                      value={config.action}
                      onChange={(e) => handleKeyConfigChange(idx, "action", e.target.value)}
                    >
                      {actionOptions[config.acType].map((action, actionIdx) => (
                        <option key={`action-${actionIdx}`} value={action.key}>{action.value}</option>
                      ))}
                    </select>
                  </div>
                )}
              </div>

              <div className="separator" />

              <div className="setting-item">
                <div className="setting-title">Long Press</div>
                <select
                  value={longPressConfigs[idx].acType}
                  onChange={(e) => handleKeyConfigChange(idx, "acType", e.target.value, true)}
                >
                  {acTypes.map((type) => (
                    <option key={type.key} value={type.key}>{type.value}</option>
                  ))}
                </select>

                {longPressConfigs[idx].acType === 'keyboard_combo' ? (
                  <div className="setting-group">
                    <div className="setting-item">
                      <div className="setting-title">Modifiers</div>
                      <div>
                        {modifiers.map((modifier, modIdx) => (
                          <button
                            key={`mod-${modIdx}`}
                            className={longPressConfigs[idx].mods.includes(modifier) ? "" : "success"}
                            onClick={() => handleModifierToggle(idx, modifier, true)}
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
                        value={longPressConfigs[idx].action}
                        onChange={(e) => handleKeyConfigChange(idx, "action", e.target.value, true)}
                      >
                        {keyboardKeys.map((key, keyIdx) => (
                          <option key={`combo-key-${keyIdx}`} value={key.key}>{key.value}</option>
                        ))}
                      </select>
                    </div>
                  </div>
                ) : (
                  <div className="setting-item">
                    <div className="setting-title">Key</div>
                    <select
                      value={longPressConfigs[idx].action}
                      onChange={(e) => handleKeyConfigChange(idx, "action", e.target.value, true)}
                    >
                      {actionOptions[longPressConfigs[idx].acType].map((action, actionIdx) => (
                        <option key={`action-${actionIdx}`} value={action.key}>{action.value}</option>
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
