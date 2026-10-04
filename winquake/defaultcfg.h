/*
 * The built-in default.cfg, used when the pak has none (Cmd_Exec_f).
 *
 * The shareware pak0.pak carries a default.cfg; the 2021 re-release's does not, because that engine
 * has it built in. Its quake.rc still says "exec default.cfg", so without this the bindings (arrow
 * keys, fire, jump, the weapon impulses) and the default cvars would be missing, and the D-pad would
 * do nothing. Same content as the shareware file, so the game behaves the same with either pak.
 * "Reset defaults" in the Options menu runs it too.
 */
#ifndef DEFAULTCFG_H
#define DEFAULTCFG_H

static const char default_cfg[] =
	"unbindall\n"

	// character controls
	"bind , +moveleft\n"
	"bind . +moveright\n"
	"bind DEL +lookdown\n"
	"bind PGDN +lookup\n"
	"bind END centerview\n"
	"bind z +lookdown\n"
	"bind a +lookup\n"
	"bind d +moveup\n"
	"bind c +movedown\n"
	"bind ALT +strafe\n"
	"bind SHIFT +speed\n"
	"bind CTRL +attack\n"
	"bind UPARROW +forward\n"
	"bind DOWNARROW +back\n"
	"bind LEFTARROW +left\n"
	"bind RIGHTARROW +right\n"
	"bind SPACE +jump\n"
	"bind ENTER +jump\n"
	"bind TAB +showscores\n"
	"bind INS +klook\n"

	// weapons
	"bind 1 \"impulse 1\"\n"
	"bind 2 \"impulse 2\"\n"
	"bind 3 \"impulse 3\"\n"
	"bind 4 \"impulse 4\"\n"
	"bind 5 \"impulse 5\"\n"
	"bind 6 \"impulse 6\"\n"
	"bind 7 \"impulse 7\"\n"
	"bind 8 \"impulse 8\"\n"
	"bind 0 \"impulse 0\"\n"
	"bind / \"impulse 10\"\n"

	// menus, console, screen
	"bind F1 help\n"
	"bind F2 menu_save\n"
	"bind F3 menu_load\n"
	"bind F4 menu_options\n"
	"bind F5 menu_multiplayer\n"
	"bind F6 \"echo Quicksaving...; wait; save quick\"\n"
	"bind F9 \"echo Quickloading...; wait; load quick\"\n"
	"bind F10 quit\n"
	"bind F12 screenshot\n"
	"bind PAUSE pause\n"
	"bind ESCAPE togglemenu\n"
	"bind ~ toggleconsole\n"
	"bind ` toggleconsole\n"
	"bind t messagemode\n"
	"bind + sizeup\n"
	"bind = sizeup\n"
	"bind - sizedown\n"

	// mouse
	"bind \\ +mlook\n"
	"bind MOUSE1 +attack\n"
	"bind MOUSE2 +forward\n"
	"bind MOUSE3 +mlook\n"

	// default cvars
	"viewsize 100\n"
	"gamma 1.0\n"
	"volume 0.7\n"
	"sensitivity 3\n";

#endif
