"""On-device filesystem browser, callable from REPL or WebREPL.

Usage:
    import fs
    fs.ls()          # list /flash with sizes
    fs.tree()        # recursive ASCII tree
    fs.cat(path)     # print file contents
    fs.rm(path)      # delete file
    fs.mkdir(path)   # create directory
    fs.free()        # FS + heap stats
"""
import os, gc

def _stat(p):
    try:
        return os.stat(p)
    except OSError:
        return None

def _isdir(st):
    return st is not None and (st[0] & 0x4000) != 0

def ls(path="/flash"):
    print("size       name")
    print("---------- " + "-" * 24)
    try:
        items = sorted(os.listdir(path))
    except OSError as e:
        print("error:", e)
        return
    for n in items:
        full = path.rstrip("/") + "/" + n
        st = _stat(full)
        if _isdir(st):
            print("%-10s %s/" % ("<dir>", n))
        elif st is None:
            print("%-10s %s" % ("?", n))
        else:
            print("%10d %s" % (st[6], n))

def tree(path="/flash", prefix=""):
    if prefix == "":
        print(path)
    try:
        items = sorted(os.listdir(path))
    except OSError as e:
        print("error:", e)
        return
    for i, n in enumerate(items):
        full = path.rstrip("/") + "/" + n
        last = i == len(items) - 1
        marker = "`-- " if last else "|-- "
        st = _stat(full)
        if _isdir(st):
            print(prefix + marker + n + "/")
            ext = "    " if last else "|   "
            tree(full, prefix + ext)
        elif st is None:
            print(prefix + marker + n + "  (?)")
        else:
            print(prefix + marker + "%s  (%d B)" % (n, st[6]))

def cat(path, max_bytes=4096):
    try:
        with open(path, "rb") as f:
            d = f.read(max_bytes)
    except OSError as e:
        print("error:", e)
        return 0
    try:
        print(d.decode("utf-8"), end="")
    except UnicodeError:
        print(repr(d))
    if len(d) == max_bytes:
        print("\n... (truncated, %d more bytes)" % (_stat(path)[6] - max_bytes))
    return len(d)

def rm(path):
    os.remove(path)
    print("removed", path)

def mkdir(path):
    os.mkdir(path)
    print("mkdir", path)

def free():
    s = os.statvfs("/flash")
    block = s[0]
    total = s[2] * block
    avail = s[3] * block
    print("FS /flash: %d KB free of %d KB" % (avail // 1024, total // 1024))
    gc.collect()
    free = gc.mem_free()
    used = gc.mem_alloc()
    print("Heap:      %d KB free, %d KB used (%d KB total)" %
          (free // 1024, used // 1024, (free + used) // 1024))


def _json(path="/flash"):
    """Marker-bracketed JSON listing for the webrepl.html "List" button.

    Always outputs SOMETHING bracketed by <<FS:...:FS>> so the JS parser
    can recover, even when the path is bogus.  Format:
        <<FS:[["name","f",size],["dir","d",0],...]:FS>>
    On error the list is empty and a leading entry encodes the message.
    """
    items = []
    err_msg = None
    try:
        # Defensive normalisation: clamp to a string with at least "/".
        if not isinstance(path, str) or not path:
            path = "/flash"
        for n in sorted(os.listdir(path)):
            try:
                full = path.rstrip("/") + "/" + n
                st = _stat(full)
                if _isdir(st):
                    items.append((n, "d", 0))
                elif st is None:
                    items.append((n, "?", 0))
                else:
                    items.append((n, "f", st[6]))
            except Exception as e:
                # Per-entry failure — keep going with the rest.
                items.append((n, "?", 0))
    except Exception as e:
        err_msg = repr(e)

    parts = []
    if err_msg:
        # Encode the error as a synthetic entry the JS will display in red.
        ne = ("err: " + err_msg)[:120]
        ne = ne.replace("\\", "\\\\").replace('"', '\\"')
        parts.append('["%s","E",0]' % ne)
    for n, t, sz in items:
        ne = n.replace("\\", "\\\\").replace('"', '\\"')
        parts.append('["%s","%s",%d]' % (ne, t, sz))
    print("<<FS:[%s]:FS>>" % ",".join(parts))
