# boot.py - bring up LAN and start a polling-based WebREPL listener.
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

    def _poll(_t):
        try:
            cl, addr = listen_s.accept()
        except OSError:
            return  # nothing pending
        # Make the client socket blocking for the handshake — server_handshake
        # uses makefile().readline() and requires blocking I/O.
        cl.setblocking(True)
        cl.settimeout(2)
        try:
            ok = webrepl.server_handshake(cl)
        except Exception as e:
            print("WebREPL: handshake error", repr(e))
            try: cl.close()
            except: pass
            return
        if not ok:
            try:
                webrepl.send_html(cl)
            except Exception:
                pass
            return
        prev = os.dupterm(None)
        os.dupterm(prev)
        if prev:
            print("WebREPL: already busy, rejecting", addr)
            cl.close()
            return
        ws = websocket.websocket(cl, True)
        ws = _webrepl._webrepl(ws)
        cl.setblocking(False)
        os.dupterm(ws)
        print("WebREPL: client", addr)

    t = machine.Timer(-1)
    t.init(period=200, mode=machine.Timer.PERIODIC, callback=_poll)
    print("WebREPL: ws://%s:%d/   password=%s" % (lan.ifconfig()[0], port, password))


if lan.ifconfig()[0] != "0.0.0.0":
    _start_webrepl()
else:
    print("WebREPL: skipping - no LAN IP")
