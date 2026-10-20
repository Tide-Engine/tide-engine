using UnityEngine;
#if TIDE_INPUT_SYSTEM && ENABLE_INPUT_SYSTEM
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Controls;
#endif

namespace Tide
{
    // What Unity's input says this frame, as a window of Tide's own would say
    // it (TideEmbedInput): Tide latches presses and adds up movement itself.
    // Reads the Input System where the project uses it, else the Input Manager.
    public static class TideInputReader
    {
        // Tide's keys, in its keyboard's order (tide/devices.h, TIDE_KEYBOARD_KEYS): Unity Input System names in camelCase.
        public static readonly string[] Keys =
        {
            "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
            "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
            "digit0", "digit1", "digit2", "digit3", "digit4", "digit5", "digit6", "digit7", "digit8", "digit9",
            "space", "enter", "escape", "tab", "backspace",
            "insert", "delete", "home", "end", "pageUp", "pageDown",
            "upArrow", "downArrow", "leftArrow", "rightArrow",
            "leftShift", "rightShift", "leftCtrl", "rightCtrl", "leftAlt", "rightAlt", "capsLock",
            "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
            "minus", "equals", "leftBracket", "rightBracket", "backslash",
            "semicolon", "quote", "comma", "period", "slash", "backquote",
            "numpad0", "numpad1", "numpad2", "numpad3", "numpad4", "numpad5", "numpad6", "numpad7", "numpad8", "numpad9",
            "numpadEnter", "numpadPlus", "numpadMinus", "numpadMultiply", "numpadDivide", "numpadPeriod",
        };

#if ENABLE_LEGACY_INPUT_MANAGER
        // The Input Manager's KeyCode for each of Keys.
        static readonly KeyCode[] keyCodes =
        {
            KeyCode.A, KeyCode.B, KeyCode.C, KeyCode.D, KeyCode.E, KeyCode.F, KeyCode.G, KeyCode.H, KeyCode.I, KeyCode.J,
            KeyCode.K, KeyCode.L, KeyCode.M, KeyCode.N, KeyCode.O, KeyCode.P, KeyCode.Q, KeyCode.R, KeyCode.S, KeyCode.T,
            KeyCode.U, KeyCode.V, KeyCode.W, KeyCode.X, KeyCode.Y, KeyCode.Z,
            KeyCode.Alpha0, KeyCode.Alpha1, KeyCode.Alpha2, KeyCode.Alpha3, KeyCode.Alpha4,
            KeyCode.Alpha5, KeyCode.Alpha6, KeyCode.Alpha7, KeyCode.Alpha8, KeyCode.Alpha9,
            KeyCode.Space, KeyCode.Return, KeyCode.Escape, KeyCode.Tab, KeyCode.Backspace,
            KeyCode.Insert, KeyCode.Delete, KeyCode.Home, KeyCode.End, KeyCode.PageUp, KeyCode.PageDown,
            KeyCode.UpArrow, KeyCode.DownArrow, KeyCode.LeftArrow, KeyCode.RightArrow,
            KeyCode.LeftShift, KeyCode.RightShift, KeyCode.LeftControl, KeyCode.RightControl,
            KeyCode.LeftAlt, KeyCode.RightAlt, KeyCode.CapsLock,
            KeyCode.F1, KeyCode.F2, KeyCode.F3, KeyCode.F4, KeyCode.F5, KeyCode.F6,
            KeyCode.F7, KeyCode.F8, KeyCode.F9, KeyCode.F10, KeyCode.F11, KeyCode.F12,
            KeyCode.Minus, KeyCode.Equals, KeyCode.LeftBracket, KeyCode.RightBracket, KeyCode.Backslash,
            KeyCode.Semicolon, KeyCode.Quote, KeyCode.Comma, KeyCode.Period, KeyCode.Slash, KeyCode.BackQuote,
            KeyCode.Keypad0, KeyCode.Keypad1, KeyCode.Keypad2, KeyCode.Keypad3, KeyCode.Keypad4,
            KeyCode.Keypad5, KeyCode.Keypad6, KeyCode.Keypad7, KeyCode.Keypad8, KeyCode.Keypad9,
            KeyCode.KeypadEnter, KeyCode.KeypadPlus, KeyCode.KeypadMinus, KeyCode.KeypadMultiply,
            KeyCode.KeypadDivide, KeyCode.KeypadPeriod,
        };
#endif

#if TIDE_INPUT_SYSTEM && ENABLE_INPUT_SYSTEM
        static Key[] inputSystemKeys;

        static Key[] InputSystemKeys()
        {
            if (inputSystemKeys != null) return inputSystemKeys;
            inputSystemKeys = new Key[Keys.Length];
            for (var i = 0; i < Keys.Length; i++)
            {
                var name = char.ToUpperInvariant(Keys[i][0]) + Keys[i].Substring(1);
                inputSystemKeys[i] = System.Enum.TryParse(name, out Key key) ? key : Key.None;
            }
            return inputSystemKeys;
        }
#endif

        static Vector2 lastMouse;
        static bool mouseSeen;

        // This frame's input, for a screen of `logical` pixels at `scale` device pixels to one.
        public static unsafe void Read(ref TideEmbedInput input, float scale)
        {
            input = default;
            if (!(scale > 0f)) scale = 1f;
            var height = Screen.height;
#if TIDE_INPUT_SYSTEM && ENABLE_INPUT_SYSTEM
            if (ReadInputSystem(ref input, scale, height)) return;
#endif
#if ENABLE_LEGACY_INPUT_MANAGER
            ReadInputManager(ref input, scale, height);
#endif
        }

        static unsafe void Hold(ref TideEmbedInput input, int key)
        {
            fixed (byte* keys = input.keys) keys[key >> 3] |= (byte)(1 << (key & 7));
        }

        static unsafe void Type(ref TideEmbedInput input, string typed)
        {
            fixed (uint* text = input.text)
            {
                for (var i = 0; i < typed.Length && input.textCount < TideEmbedInput.TextMax; i++)
                {
                    int c = typed[i];
                    if (char.IsHighSurrogate(typed[i]) && i + 1 < typed.Length && char.IsLowSurrogate(typed[i + 1]))
                    {
                        c = char.ConvertToUtf32(typed[i], typed[i + 1]);
                        i++;
                    }
                    if (c < 32 || c == 127) continue; // Enter, backspace and the like are keys
                    text[input.textCount++] = (uint)c;
                }
            }
        }

        static unsafe void Touch(ref TideEmbedInput input, uint phase, int finger, Vector2 position, float scale, int height)
        {
            if (input.touchCount >= TideEmbedInput.TouchMax) return;
            fixed (byte* bytes = input.touches)
            {
                var t = (TideEmbedTouch*)bytes + input.touchCount++;
                t->phase = phase;
                t->source = (uint)finger;
                t->x = position.x / scale;
                t->y = (height - position.y) / scale;
            }
        }

        static void Mouse(ref TideEmbedInput input, Vector2 position, Vector2 scroll, Vector2 locked, float scale, int height)
        {
            input.mouseX = position.x / scale;
            input.mouseY = (height - position.y) / scale;
            input.wheelX = scroll.x;
            input.wheelY = scroll.y;
            input.mouseLocked = (byte)(Cursor.lockState == CursorLockMode.Locked ? 1 : 0);
            input.movedX = locked.x / scale;
            input.movedY = -locked.y / scale;
        }

#if ENABLE_LEGACY_INPUT_MANAGER
        static void ReadInputManager(ref TideEmbedInput input, float scale, int height)
        {
            for (var i = 0; i < keyCodes.Length; i++)
            {
                if (Input.GetKey(keyCodes[i])) Hold(ref input, i);
            }
            Vector2 position = Input.mousePosition;
            var moved = mouseSeen ? position - lastMouse : Vector2.zero;
            lastMouse = position;
            mouseSeen = true;
            // Locked, the cursor stays: its movement is the mouse's own, in the Input Manager's units
            var locked = Cursor.lockState == CursorLockMode.Locked
                ? new Vector2(Input.GetAxisRaw("Mouse X"), Input.GetAxisRaw("Mouse Y")) * 10f
                : moved;
            Mouse(ref input, position, Input.mouseScrollDelta, locked, scale, height);
            for (var b = 0; b < 5; b++)
            {
                if (Input.GetMouseButton(b)) input.mouseButtons |= 1u << b;
            }
            Type(ref input, Input.inputString);
            input.touchscreen = (byte)(Input.touchSupported ? 1 : 0);
            for (var i = 0; i < Input.touchCount; i++)
            {
                var t = Input.GetTouch(i);
                if (t.phase == TouchPhase.Stationary) continue;
                var phase = t.phase == TouchPhase.Began ? 0u : t.phase == TouchPhase.Moved ? 1u
                          : t.phase == TouchPhase.Ended ? 2u : 3u;
                Touch(ref input, phase, t.fingerId, t.position, scale, height);
            }
            // The first gamepad, by the Input Manager's default joystick axes and buttons, where it has them
            var names = Input.GetJoystickNames();
            input.gamepad = (byte)(names.Length > 0 && !string.IsNullOrEmpty(names[0]) ? 1 : 0);
            if (input.gamepad == 0) return;
            for (var b = 0; b < 10; b++)
            {
                if (Input.GetKey(KeyCode.JoystickButton0 + b)) input.gamepadButtons |= 1u << b;
            }
        }
#endif

#if TIDE_INPUT_SYSTEM && ENABLE_INPUT_SYSTEM
        static readonly string[] padButtons =
        {
            "buttonSouth", "buttonEast", "buttonWest", "buttonNorth", "leftShoulder", "rightShoulder",
            "leftStickButton", "rightStickButton", "start", "select",
        };

        static bool ReadInputSystem(ref TideEmbedInput input, float scale, int height)
        {
            var keyboard = Keyboard.current;
            var mouse = UnityEngine.InputSystem.Mouse.current;
            var pad = Gamepad.current;
            var screen = Touchscreen.current;
            if (keyboard == null && mouse == null && pad == null && screen == null) return false;
            if (keyboard != null)
            {
                var keys = InputSystemKeys();
                for (var i = 0; i < keys.Length; i++)
                {
                    if (keys[i] != Key.None && keyboard[keys[i]].isPressed) Hold(ref input, i);
                }
            }
            Type(ref input, typed.ToString());
            typed.Clear();
            ListenForText(keyboard);
            if (mouse != null)
            {
                var delta = mouse.delta.ReadValue();
                // A wheel's notch is 120 on some platforms: Tide's scroll is notches, as the Input Manager's
                var scroll = mouse.scroll.ReadValue();
                if (Mathf.Abs(scroll.y) >= 60f || Mathf.Abs(scroll.x) >= 60f) scroll /= 120f;
                Mouse(ref input, mouse.position.ReadValue(), scroll, delta, scale, height);
                ButtonControl[] buttons = { mouse.leftButton, mouse.rightButton, mouse.middleButton, mouse.backButton, mouse.forwardButton };
                for (var b = 0; b < buttons.Length; b++)
                {
                    if (buttons[b].isPressed) input.mouseButtons |= 1u << b;
                }
            }
            if (pad != null)
            {
                input.gamepad = 1;
                var left = pad.leftStick.ReadValue();
                var right = pad.rightStick.ReadValue();
                input.leftX = left.x;
                input.leftY = -left.y;
                input.rightX = right.x;
                input.rightY = -right.y;
                input.leftTrigger = pad.leftTrigger.ReadValue();
                input.rightTrigger = pad.rightTrigger.ReadValue();
                for (var b = 0; b < padButtons.Length; b++)
                {
                    if (pad[padButtons[b]] is ButtonControl button && button.isPressed) input.gamepadButtons |= 1u << b;
                }
                if (pad.dpad.up.isPressed) input.gamepadButtons |= 1u << 10;
                if (pad.dpad.down.isPressed) input.gamepadButtons |= 1u << 11;
                if (pad.dpad.left.isPressed) input.gamepadButtons |= 1u << 12;
                if (pad.dpad.right.isPressed) input.gamepadButtons |= 1u << 13;
            }
            if (screen != null)
            {
                input.touchscreen = 1;
                foreach (var touch in screen.touches)
                {
                    var phase = touch.phase.ReadValue();
                    uint tide;
                    switch (phase)
                    {
                        case UnityEngine.InputSystem.TouchPhase.Began: tide = 0; break;
                        case UnityEngine.InputSystem.TouchPhase.Moved: tide = 1; break;
                        case UnityEngine.InputSystem.TouchPhase.Ended: tide = 2; break;
                        case UnityEngine.InputSystem.TouchPhase.Canceled: tide = 3; break;
                        default: continue;
                    }
                    Touch(ref input, tide, touch.touchId.ReadValue(), touch.position.ReadValue(), scale, height);
                }
            }
            return true;
        }

        // Characters the keyboard typed since the last frame: the Input System hands them over as events.
        static readonly System.Text.StringBuilder typed = new System.Text.StringBuilder();
        static Keyboard listening;

        static void ListenForText(Keyboard keyboard)
        {
            if (keyboard == listening) return;
            if (listening != null) listening.onTextInput -= OnText;
            listening = keyboard;
            if (keyboard != null) keyboard.onTextInput += OnText;
        }

        static void OnText(char c)
        {
            typed.Append(c);
        }
#endif
    }
}
