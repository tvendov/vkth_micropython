# boot.py - bring up LAN and start a hardened WebREPL listener.
import network
import time

# 1. Bring up Ethernet (DHCP).
lan = network.LAN()
lan.active(True)
for _ in range(150):
    if lan.ifconfig()[0] != "0.0.0.0":
        break
    time.sleep_ms(100)
print("LAN:", lan.ifconfig())


def _start_webrepl(password="vk6m5", port=8266, lan=lan):
    import socket, _webrepl, os, websocket, machine
    try:
        import webrepl
    except Exception as e:
        print("WebREPL: import failed", e)
        return

    _webrepl.password(password)

    listen_s = socket.socket()
    listen_s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_s.bind(("0.0.0.0", port))
    listen_s.listen(1)
    listen_s.setblocking(False)
    webrepl.listen_s = listen_s

    # Mutable state held by the closure: state[0] = wrapped websocket,
    # state[1] = underlying client socket.
    state = [None, None]

    def _is_alive():
        cl = state[1]
        if cl is None:
            return False
        try:
            cl.send(b"")
            return True
        except Exception:
            return False

    def _evict():
        try: os.dupterm(None)
        except Exception: pass
        try:
            if state[0] is not None and hasattr(state[0], "close"):
                state[0].close()
        except Exception: pass
        try:
            if state[1] is not None:
                state[1].close()
        except Exception: pass
        state[0] = None
        state[1] = None

    def _poll(_t):
        # Outermost guard — never let an exception kill the Timer.
        try:
            try:
                cl, addr = listen_s.accept()
            except OSError:
                return
            try:
                cl.setblocking(True)
                cl.settimeout(2)
                ok = False
                try:
                    ok = webrepl.server_handshake(cl)
                except Exception as e:
                    print("WebREPL: handshake error", repr(e))
                if not ok:
                    try: webrepl.send_html(cl)
                    except Exception: pass
                    try: cl.close()
                    except: pass
                    return
                if state[0] is not None:
                    if _is_alive():
                        print("WebREPL: busy, rejecting", addr)
                        try: cl.close()
                        except: pass
                        return
                    print("WebREPL: evicting stale client, accepting", addr)
                    _evict()
                ws = websocket.websocket(cl, True)
                ws = _webrepl._webrepl(ws)
                cl.setblocking(False)
                state[0] = ws
                state[1] = cl
                os.dupterm(ws)
                print("WebREPL: client", addr)
            except Exception as e:
                print("WebREPL: accept-handler error", repr(e))
                try: cl.close()
                except: pass
        except Exception as e:
            print("WebREPL: _poll error", repr(e))

    t = machine.Timer(-1)
    t.init(period=200, mode=machine.Timer.PERIODIC, callback=_poll)
    ip = lan.ifconfig()[0]
    print("WebREPL: ws://%s:%d/   password=%s" % (ip, port, password))
    print("Upload via webrepl_cli.py with /flash/ prefix:")
    print("  python webrepl_cli.py -p %s local.bin %s:/flash/local.bin" % (password, ip))


if lan.ifconfig()[0] != "0.0.0.0":
    _start_webrepl()
else:
    print("WebREPL: skipping - no LAN IP")
