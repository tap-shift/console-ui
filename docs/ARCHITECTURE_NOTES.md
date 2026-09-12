# Architecture Notes

Guide button behavior: Pressing PS/Xbox/Guide intercepts input, suspends active game via SIGSTOP, brings Console UI to foreground, and resumes via SIGCONT.
