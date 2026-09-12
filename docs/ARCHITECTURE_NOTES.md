# Architecture Notes

Guide button behavior: Pressing PS/Xbox/Guide intercepts input, suspends active game via SIGSTOP, brings Console UI to foreground, and resumes via SIGCONT.

- Game Card Dynamic Loading: The game card shelf will soon stream metadata and cover art remotely from a backend server. The `question-square.png` asset must remain the permanent client fallback when no cover is delivered.
- Console Branding: The top-left "Console UI" label is a temporary placeholder that will be replaced by a configurable custom console brand name.
- Account System: The top-right Profile button will integrate with a local/cloud authentication and profile management system.

## Execution Hook & Process Management
- **Zero-Copy Binary Execution:** On confirming a game card, the system invokes `/bin/sh -c "<launch_path>" &` to immediately execute the NFS pre-mounted game binary (`launch.sh`) without local disk copying.
- **Process Management:** The architecture is designed for the UI to intercept the Guide (PS/Xbox) button via a background event loop (planned). When pressed, it will send a `SIGSTOP` signal to suspend the active child game process, bring the Console UI to the foreground for dashboard interactions, and upon returning to the game, send a `SIGCONT` signal to seamlessly resume execution.
