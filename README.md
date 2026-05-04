# MouseRemap
Mouse remapping program I created for a vaxee outset ax since nothing worked.

This uses a Windows low-level mouse hook (`WH_MOUSE_LL`) and `SendInput` to turn the side buttons into left/right arrow repeats. The app self-elevates via UAC so it can also inject input into elevated/high-integrity apps such as Task Manager. It still will not work on secure desktops such as UAC prompts, the lock screen, or Ctrl+Alt+Del; covering those requires a signed kernel/HID filter driver instead of a user-mode hook.

