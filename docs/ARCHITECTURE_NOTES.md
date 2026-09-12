# Architecture Notes

Guide button behavior: Pressing PS/Xbox/Guide intercepts input, suspends active game via SIGSTOP, brings Console UI to foreground, and resumes via SIGCONT.

- Game Card Dynamic Loading: The game card shelf will soon stream metadata and cover art remotely from a backend server. The `question-square.png` asset must remain the permanent client fallback when no cover is delivered.
- Console Branding: The top-left "Console UI" label is a temporary placeholder that will be replaced by a configurable custom console brand name.
- Account System: The top-right Profile button will integrate with a local/cloud authentication and profile management system.
