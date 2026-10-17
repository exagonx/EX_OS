# =============================================================================
# /exwin/shell/autoexec.sh - the shell's customisations inside ExWin
#
# Run by every shell that opens in an ExWin terminal window, and only there
# (sh 0.007): not on the consoles, not over telnet. The system's own
# /boot/autoexec.sh and /boot/avvio.sh are never run in a window - they start
# drivers that are already running.
#
# Same syntax as the autoexec: one line = one command, '#' comment, '@' runs a
# line without printing it. Put here what you want in every window, e.g.:
#
#   echo Benvenuto nella shell di ExWin
#   @cd /root
#
# Do not start drivers or services here: they belong to /boot/avvio.sh.
# =============================================================================
