"""IQTX HIL transport: existing common folder, RAM execution, no flashing/copy.

--snapshot records the live SDR state without reset. --run executes a payload
after a raw-REPL soft reset and restores the recorded receiver in finally.
--run-live does not reset: the payload must restore the existing app in finally.
The operator must confirm that the RF power stage is disconnected/muted first.
"""
import argparse
import json
from pathlib import Path
import sys
import time
from mpremote.transport_serial import SerialTransport

HERE = Path(__file__).resolve().parent
SNAPSHOT = HERE / "iqtx_hil_snapshot.json"
LOG = HERE / "iqtx_hil_transcript.log"


def execute(transport, code, log, timeout=15):
    log.write("\nCOMMAND " + code + "\n")
    log.flush()
    captured = []
    def consume(data):
        text = data.decode("utf-8", errors="replace")
        captured.append(text)
        sys.stdout.write(text)
        sys.stdout.flush()
        log.write(text)
        log.flush()
    output, errors = transport.exec_raw(code, timeout=timeout, data_consumer=consume)
    if errors:
        text = errors.decode("utf-8", errors="replace")
        log.write("\nSTDERR " + text)
        log.flush()
        raise RuntimeError(text)
    return "".join(captured) if captured else output.decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--snapshot", action="store_true")
    action.add_argument("--run", type=Path)
    action.add_argument("--run-live", type=Path)
    action.add_argument("--restore", action="store_true")
    args = parser.parse_args()
    with LOG.open("a", encoding="utf-8") as log:
        log.write("\nHOST_SESSION " + time.strftime("%Y-%m-%dT%H:%M:%S%z") + " " + repr(vars(args)) + "\n")
        transport = SerialTransport(args.port, timeout=3)
        try:
            transport.enter_raw_repl(soft_reset=False)
            if args.snapshot:
                code = """import sys,os,gc,machine,json
_iqtx_m=sys.modules.get('sdr_single')
_iqtx_a=_iqtx_m._KEEP.get('app') if _iqtx_m else None
_iqtx_s={'uid':machine.unique_id().hex(),'uname':list(os.uname()),'iqtx':hasattr(machine,'IQTX'),'heap':gc.mem_free(),'app':_iqtx_a is not None}
if _iqtx_a:
    _iqtx_s.update(params=_iqtx_a.p,running=_iqtx_a.be.running,scope_stage=_iqtx_a.be.scope_stage,scope_view=_iqtx_a._scope_view,spectrum_view=_iqtx_a._spectrum_view,squelch=_iqtx_a._squelch,injection=_iqtx_a._inj_on)
print('IQTX_SNAPSHOT '+json.dumps(_iqtx_s))
del _iqtx_s,_iqtx_a,_iqtx_m
"""
                output = execute(transport, code, log)
                line = next(line for line in output.splitlines() if line.startswith("IQTX_SNAPSHOT "))
                state = json.loads(line[len("IQTX_SNAPSHOT "):])
                if not state["iqtx"] or "RA6M3" not in state["uname"][-1]:
                    raise RuntimeError("Wrong firmware/board; no reset performed")
                if state.get("injection"):
                    raise RuntimeError("Active tester injection requires an explicit restore plan")
                state["port"] = args.port
                SNAPSHOT.write_text(json.dumps(state, indent=2), encoding="utf-8")
                return
            state = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
            if args.port != state["port"]:
                raise RuntimeError("Port differs from recorded target")
            execute(transport, "import machine; assert machine.unique_id().hex()==" + repr(state["uid"]), log)
            if args.run_live:
                payload = args.run_live.read_text(encoding="utf-8")
                execute(transport, "exec(" + repr(payload) + ",{'__name__':'__iqtx_live_hil__'})", log, timeout=20)
                return
            try:
                if args.run:
                    transport.exit_raw_repl()
                    transport.enter_raw_repl(soft_reset=True)
                    execute(transport, "import machine; assert machine.unique_id().hex()==" + repr(state["uid"]), log)
                    payload = args.run.read_text(encoding="utf-8")
                    # Isolated globals: do not leave test handles in the REPL.
                    execute(transport, "exec(" + repr(payload) + ",{'__name__':'__iqtx_hil__'})", log, timeout=20)
            finally:
                execute(transport, "from machine import IQTX; IQTX.release(); print('IQTX_RELEASED')", log)
                if state["app"]:
                    params = json.dumps(state["params"])
                    restore = """import json,sdr_single
_iqtx_params=json.loads(%r)
_iqtx_rxauto=_iqtx_params.get('rxauto',0)
_iqtx_params['rxauto']=%d
_iqtx_load=sdr_single.load_params
sdr_single.load_params=lambda:_iqtx_params
try:
    _iqtx_restored=sdr_single.start()
finally:
    sdr_single.load_params=_iqtx_load
_iqtx_restored._apply_gain('SQL',%d)
assert _iqtx_restored.be.set_scope(%d)
_iqtx_restored._scope_id=%d
_iqtx_restored._paint_listen_markers()
_iqtx_restored._paint_output_status()
for _ in range(%d): _iqtx_restored.toggle_spectrum_view()
for _ in range(%d): _iqtx_restored.toggle_scope_view()
_iqtx_restored.p['rxauto']=_iqtx_rxauto
_iqtx_params['rxauto']=_iqtx_rxauto
if _iqtx_restored.save_timer and sdr_single.load_params()==_iqtx_restored.p:
    _iqtx_restored.save_timer.delete()
    _iqtx_restored.save_timer=None
assert _iqtx_restored.be.running == %r
assert _iqtx_restored.p == _iqtx_params
print('IQTX_RESTORED '+json.dumps({'running':_iqtx_restored.be.running,'params':_iqtx_restored.p,'scope_stage':_iqtx_restored.be.scope_stage,'scope_view':_iqtx_restored._scope_view}))
del _iqtx_params,_iqtx_load,_iqtx_restored,_iqtx_rxauto
""" % (params, int(state["running"]), state["squelch"], state["scope_stage"], state["scope_stage"], state["spectrum_view"], state["scope_view"], state["running"])
                    execute(transport, restore, log, timeout=20)
        finally:
            if transport.in_raw_repl:
                transport.exit_raw_repl()
            transport.close()


if __name__ == "__main__":
    main()
