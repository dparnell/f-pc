#!/usr/bin/env python3
"""Drive fpc in a pseudo-terminal and show what the terminal displays.

usage: ptytest.py [--size COLSxROWS] [--cwd DIR] SCRIPT -- fpc args...

SCRIPT is a sequence of steps separated by ';;':
    keys:TEXT     send TEXT (\\r, \\e, \\t and \\xNN escapes allowed)
    wait:SECONDS  let the program run
    resize:CxR    resize the terminal (sends SIGWINCH)
    screen        print the emulated screen
    attrs:ROW     print the attributes (fg/bg) of a screen row

Needs the pyte terminal emulator (pip install pyte).
"""
import fcntl, os, pty, select, signal, struct, sys, termios, time

import pyte


def set_size(fd, cols, rows):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def main():
    args = sys.argv[1:]
    cols, rows, cwd = 80, 25, None
    while args and args[0].startswith("--"):
        if args[0] == "--size":
            cols, rows = (int(v) for v in args[1].split("x")); args = args[2:]
        elif args[0] == "--cwd":
            cwd = args[1]; args = args[2:]
        else:
            break
    script, cmd = args[0], args[args.index("--") + 1:]

    pid, fd = pty.fork()
    if pid == 0:
        if cwd:
            os.chdir(cwd)
        os.execvp(cmd[0], cmd)
    set_size(fd, cols, rows)
    os.kill(pid, signal.SIGWINCH)
    screen = pyte.Screen(cols, rows)
    stream = pyte.ByteStream(screen)

    def pump(seconds):
        end = time.time() + seconds
        while True:
            left = end - time.time()
            if left <= 0:
                return
            r, _, _ = select.select([fd], [], [], left)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    return
                if not data:
                    return
                stream.feed(data)

    pump(0.5)
    for step in script.split(";;"):
        step = step.strip()
        if step.startswith("keys:"):
            text = step[5:].encode().decode("unicode_escape").encode("latin-1")
            for b in text:
                os.write(fd, bytes([b]))
                pump(0.02)
            pump(0.3)
        elif step.startswith("wait:"):
            pump(float(step[5:]))
        elif step.startswith("resize:"):
            cols, rows = (int(v) for v in step[7:].split("x"))
            set_size(fd, cols, rows)
            screen.resize(rows, cols)
            os.kill(pid, signal.SIGWINCH)
            pump(0.5)
        elif step == "screen":
            print(f"+{'-' * cols}+ {cols}x{rows} cursor={screen.cursor.x},{screen.cursor.y}")
            for line in screen.display:
                print(f"|{line}|")
            print(f"+{'-' * cols}+")
        elif step.startswith("attrs:"):
            row = int(step[6:])
            print(" ".join(f"{c.fg}/{c.bg}" for c in screen.buffer[row].values()))
    os.kill(pid, signal.SIGTERM)
    pump(0.2)


if __name__ == "__main__":
    main()
