"""Hardware test for event-driven RIIC completion on VK_RA6M3.

The test never polls I2CAsync.done() and never calls I2CAsync.wait().  Python
keeps running a heartbeat loop until the persistent completion handler records
the message from the port.
"""

import asyncio
import errno
import gc
import micropython
import time

from machine import I2C, I2CAsync


I2C_ID = 1
I2C_FREQUENCY = 400_000
AMG_ADDRESS = 0x68
MLX_ADDRESS = 0x33
MISSING_ADDRESS = 0x7E
ERR_CANCELED = getattr(errno, "ECANCELED", 125)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


event_count = 0
event_result = 0
event_tick_us = 0


def reset_event():
    global event_count, event_result, event_tick_us
    event_count = 0
    event_result = 0
    event_tick_us = 0


def record_completion(transfer):
    global event_count, event_result, event_tick_us
    event_count += 1
    event_result = transfer.result_code()
    event_tick_us = time.ticks_us()


def wait_for_events(expected, timeout_ms=1000):
    heartbeat = 0
    deadline = time.ticks_add(time.ticks_ms(), timeout_ms)
    while event_count < expected and time.ticks_diff(deadline, time.ticks_ms()) > 0:
        heartbeat += 1
        time.sleep_ms(1)
    return heartbeat


i2c = I2C(I2C_ID, freq=I2C_FREQUENCY)
devices = i2c.scan()
require(AMG_ADDRESS in devices, "AMG8833 not found")
require(MLX_ADDRESS in devices, "MLX90640 not found")

transfer = I2CAsync(i2c)

# Legacy mode remains available when no handler is installed.  Starting the
# second read immediately after result() proves there is no hidden notification
# gate in the polling-only path.
poll_pointer = bytearray((0x80,))
poll_buffer = bytearray(2)
transfer.irq(None)
i2c.writeto(AMG_ADDRESS, poll_pointer, False)
transfer.readinto(AMG_ADDRESS, poll_buffer, timeout_ms=100)
poll_heartbeat = 0
while not transfer.done():
    poll_heartbeat += 1
require(transfer.result() == 2, "polling result is wrong")
i2c.writeto(AMG_ADDRESS, poll_pointer, False)
transfer.readinto(AMG_ADDRESS, poll_buffer, timeout_ms=100)
require(transfer.wait() == 2, "blocking compatibility result is wrong")
print("PASS polling_compat", "heartbeat", poll_heartbeat)

transfer.irq(record_completion)
require(transfer.irq() is record_completion, "completion handler was not retained")

# MLX status clear is a safe four-byte write and exercises the complete TX DTC
# path: CPU sends the address, DTC sends every payload byte, TEI ends the write.
mlx_clear_command = bytearray((0x80, 0x00, 0x00, 0x30))
reset_event()
transfer.writefrom(MLX_ADDRESS, mlx_clear_command, timeout_ms=100)
write_heartbeat = wait_for_events(1)
require(event_count == 1, "write callback missing")
require(event_result == len(mlx_clear_command), "wrong write result")
write_stats = transfer.stats()
require(write_stats[4] <= 3, "MLX write entered TXI too many times")
require(write_stats[5] == 1, "MLX write did not use one DTC transfer")
require(write_stats[6] == len(mlx_clear_command), "wrong MLX TX DTC byte count")
require(write_stats[7] == 0, "MLX TX DTC path fell back to byte interrupts")
print("PASS write_dtc", "bytes", event_result, "heartbeat", write_heartbeat,
      "stats", write_stats)

# A DTC write followed by an async read with repeated START proves that the
# completion message can advance a combined transaction without polling.
combined_state = [0, 0, 0]
status_pointer = bytearray((0x80, 0x00))
status_buffer = bytearray(2)


def combined_completion(done_transfer):
    result = done_transfer.result()
    if combined_state[0] == 0:
        combined_state[1] = result
        combined_state[0] = 1
        done_transfer.readinto(MLX_ADDRESS, status_buffer, True, 100)
    else:
        combined_state[2] = result
        combined_state[0] = 2


transfer.irq(combined_completion)
transfer.writefrom(MLX_ADDRESS, status_pointer, False, 100)
combined_deadline = time.ticks_add(time.ticks_ms(), 500)
combined_heartbeat = 0
while combined_state[0] < 2 and time.ticks_diff(combined_deadline, time.ticks_ms()) > 0:
    combined_heartbeat += 1
    time.sleep_ms(1)
require(combined_state[0] == 2, "combined write/read did not complete")
require(combined_state[1] == 2 and combined_state[2] == 2,
        "combined write/read returned a wrong result")
print("PASS combined", "write", combined_state[1], "read", combined_state[2],
      "heartbeat", combined_heartbeat)

transfer.irq(record_completion)

# A full MLX RAM read proves that Python continues to run while RIIC receives
# bytes under DTC control.
mlx_pointer = bytearray((0x04, 0x00))
mlx_buffer = bytearray(1664)
reset_event()
i2c.writeto(MLX_ADDRESS, mlx_pointer, False)
start_us = time.ticks_us()
transfer.readinto(MLX_ADDRESS, mlx_buffer, timeout_ms=1000)
require(event_count == 0, "completion ran synchronously inside readinto")
success_heartbeat = wait_for_events(1)
require(event_count == 1, "success callback missing")
require(event_result == len(mlx_buffer), "wrong success result")
success_us = time.ticks_diff(event_tick_us, start_us)
mlx_stats = transfer.stats()
require(mlx_stats[0] <= 6, "MLX read entered RXI too many times")
require(mlx_stats[1] == 1, "MLX read did not use one DTC transfer")
require(mlx_stats[2] == len(mlx_buffer) - 3, "wrong MLX DTC byte count")
require(mlx_stats[3] == 0, "MLX DTC path fell back to byte interrupts")
time.sleep_ms(20)
require(event_count == 1, "success callback was delivered more than once")
print("PASS success", "bytes", event_result, "heartbeat", success_heartbeat,
      "start_to_callback_us", success_us, "checksum", sum(mlx_buffer) & 0xFFFF,
      "stats", mlx_stats)

# The shorter AMG8833 frame uses the same DTC path on the shared RIIC bus.
amg_frame_pointer = bytearray((0x80,))
amg_frame_buffer = bytearray(128)
reset_event()
i2c.writeto(AMG_ADDRESS, amg_frame_pointer, False)
transfer.readinto(AMG_ADDRESS, amg_frame_buffer, timeout_ms=100)
amg_frame_heartbeat = wait_for_events(1)
require(event_result == len(amg_frame_buffer), "wrong AMG frame result")
amg_stats = transfer.stats()
require(amg_stats[0] <= 6, "AMG frame entered RXI too many times")
require(amg_stats[1] == 1, "AMG frame did not use one DTC transfer")
require(amg_stats[2] == len(amg_frame_buffer) - 3, "wrong AMG DTC byte count")
require(amg_stats[3] == 0, "AMG DTC path fell back to byte interrupts")
print("PASS amg_frame", "bytes", event_result, "heartbeat", amg_frame_heartbeat,
      "checksum", sum(amg_frame_buffer) & 0xFFFF, "stats", amg_stats)

# The handler is allowed to start the next transfer because C releases the bus
# and clears completion_pending before entering Python.
chain_count = 0
chain_results = [0, 0]
chain_pointer = bytearray((0x80,))
chain_buffer = bytearray(2)


def chain_completion(done_transfer):
    global chain_count
    chain_results[chain_count] = done_transfer.result()
    chain_count += 1
    if chain_count < 2:
        i2c.writeto(AMG_ADDRESS, chain_pointer, False)
        done_transfer.readinto(AMG_ADDRESS, chain_buffer, timeout_ms=100)


transfer.irq(chain_completion)
i2c.writeto(AMG_ADDRESS, chain_pointer, False)
transfer.readinto(AMG_ADDRESS, chain_buffer, timeout_ms=100)
chain_deadline = time.ticks_add(time.ticks_ms(), 500)
chain_heartbeat = 0
while chain_count < 2 and time.ticks_diff(chain_deadline, time.ticks_ms()) > 0:
    chain_heartbeat += 1
    time.sleep_ms(1)
require(chain_count == 2, "callback chaining did not complete")
require(chain_results[0] == 2 and chain_results[1] == 2, "wrong chained result")
time.sleep_ms(20)
require(chain_count == 2, "chained callback was delivered more than once")
print("PASS chain", "callbacks", chain_count, "heartbeat", chain_heartbeat)

# NACK is terminal and must notify once with ENODEV.
transfer.irq(record_completion)
nack_buffer = bytearray(1)
reset_event()
transfer.readinto(MISSING_ADDRESS, nack_buffer, timeout_ms=100)
nack_heartbeat = wait_for_events(1)
require(event_count == 1, "NACK callback missing")
require(event_result == -errno.ENODEV, "NACK did not report ENODEV")
time.sleep_ms(20)
require(event_count == 1, "NACK callback was delivered more than once")
print("PASS nack", "errno", -event_result, "heartbeat", nack_heartbeat)

# cancel() must return promptly, notify with ECANCELED, and leave the bus usable.
reset_event()
i2c.writeto(MLX_ADDRESS, mlx_pointer, False)
transfer.readinto(MLX_ADDRESS, mlx_buffer, timeout_ms=1000)
cancel_start_us = time.ticks_us()
transfer.cancel()
cancel_call_us = time.ticks_diff(time.ticks_us(), cancel_start_us)
cancel_heartbeat = wait_for_events(1)
require(event_count == 1, "cancel callback missing")
require(event_result == -ERR_CANCELED, "cancel did not report ECANCELED")
time.sleep_ms(20)
require(event_count == 1, "cancel callback was delivered more than once")
require(AMG_ADDRESS in i2c.scan() and MLX_ADDRESS in i2c.scan(),
        "bus did not recover after cancel")
print("PASS cancel", "errno", -event_result, "call_us", cancel_call_us,
      "heartbeat", cancel_heartbeat)

# The static one-shot watchdog makes timeout completion autonomous.
reset_event()
i2c.writeto(MLX_ADDRESS, mlx_pointer, False)
timeout_start_us = time.ticks_us()
transfer.readinto(MLX_ADDRESS, mlx_buffer, timeout_ms=1)
timeout_heartbeat = wait_for_events(1)
timeout_total_us = time.ticks_diff(event_tick_us, timeout_start_us)
require(event_count == 1, "timeout callback missing")
require(event_result == -errno.ETIMEDOUT, "timeout did not report ETIMEDOUT")
time.sleep_ms(20)
require(event_count == 1, "timeout callback was delivered more than once")
require(AMG_ADDRESS in i2c.scan() and MLX_ADDRESS in i2c.scan(),
        "bus did not recover after timeout")
print("PASS timeout", "errno", -event_result, "total_us", timeout_total_us,
      "heartbeat", timeout_heartbeat)

# Normal callback delivery must not allocate after all objects are prepared.
heap_state = [0, 0]


def heap_locked_completion(done_transfer):
    result = done_transfer.result_code()
    if heap_state[0] == 0:
        heap_state[1] = result
        heap_state[0] = 1
        done_transfer.readinto(AMG_ADDRESS, chain_buffer, True, 100)
    else:
        heap_state[1] += result
        heap_state[0] = 2


transfer.irq(heap_locked_completion)
gc.collect()
heap_heartbeat = 0
heap_timed_out = False
micropython.heap_lock()
try:
    transfer.writefrom(AMG_ADDRESS, chain_pointer, False, 100)
    heap_deadline = time.ticks_add(time.ticks_ms(), 500)
    while heap_state[0] < 2:
        if time.ticks_diff(heap_deadline, time.ticks_ms()) <= 0:
            heap_timed_out = True
            break
        heap_heartbeat += 1
        time.sleep_ms(1)
finally:
    heap_depth = micropython.heap_unlock()

require(not heap_timed_out, "heap-lock callback timed out")
require(heap_state[0] == 2 and heap_state[1] == 3,
        "heap-lock callback returned a wrong result")
time.sleep_ms(20)
require(heap_state[0] == 2, "heap-lock callback was delivered more than once")
require(heap_depth == 0, "heap lock depth was not restored")
print("PASS heap_lock_chain", "callbacks", heap_state[0],
      "result_sum", heap_state[1], "heartbeat", heap_heartbeat)

# Error reporting must also remain allocation-free. result_code() returns the
# negative errno directly instead of constructing OSError inside the handler.
heap_error_state = [0]


def heap_locked_error_completion(done_transfer):
    heap_error_state[0] = done_transfer.result_code()


transfer.irq(heap_locked_error_completion)
heap_error_timed_out = False
gc.collect()
micropython.heap_lock()
try:
    transfer.readinto(MISSING_ADDRESS, nack_buffer, True, 100)
    heap_error_deadline = time.ticks_add(time.ticks_ms(), 500)
    while heap_error_state[0] == 0:
        if time.ticks_diff(heap_error_deadline, time.ticks_ms()) <= 0:
            heap_error_timed_out = True
            break
        time.sleep_ms(1)
finally:
    heap_error_depth = micropython.heap_unlock()

require(not heap_error_timed_out, "heap-lock error callback timed out")
require(heap_error_state[0] == -errno.ENODEV,
        "heap-lock error callback returned a wrong result")
require(heap_error_depth == 0, "heap-lock error test left the heap locked")
print("PASS heap_lock_error", "result_code", heap_error_state[0])

# Stock asyncio proves the desired LEGO-like behavior: the transfer task waits
# for a message while an unrelated heartbeat task keeps making progress.
async_flag = asyncio.ThreadSafeFlag()
async_state = [0, 0]
async_heartbeat = [0]


def asyncio_completion(done_transfer):
    async_state[1] = done_transfer.result()
    async_state[0] = 1
    async_flag.set()


async def asyncio_heartbeat_task():
    while async_state[0] == 0:
        async_heartbeat[0] += 1
        await asyncio.sleep_ms(1)


async def asyncio_transfer_task():
    transfer.irq(asyncio_completion)
    i2c.writeto(MLX_ADDRESS, mlx_pointer, False)
    transfer.readinto(MLX_ADDRESS, mlx_buffer, timeout_ms=1000)
    await async_flag.wait()


async def asyncio_main():
    heartbeat_task = asyncio.create_task(asyncio_heartbeat_task())
    await asyncio_transfer_task()
    await heartbeat_task


asyncio.run(asyncio_main())
require(async_state[0] == 1 and async_state[1] == len(mlx_buffer),
        "asyncio completion returned a wrong result")
require(async_heartbeat[0] > 0, "asyncio heartbeat did not run during transfer")
print("PASS asyncio_message", "bytes", async_state[1],
      "heartbeat", async_heartbeat[0])

# This is an architectural gate, not a failure of the completion IRQ.  Stock
# ThreadSafeFlag creates a coroutine and an IOQueue entry for each wait, so it
# cannot satisfy the stricter no-allocation-after-init rule.
coroutine_allocation_blocked = False
gc.collect()
micropython.heap_lock()
try:
    unused_waiter = async_flag.wait()
except MemoryError:
    coroutine_allocation_blocked = True
finally:
    coroutine_probe_depth = micropython.heap_unlock()

queue_allocation_blocked = False
queue_probe_flag = asyncio.ThreadSafeFlag()
queue_probe_waiter = queue_probe_flag.wait()
gc.collect()
micropython.heap_lock()
try:
    queue_probe_waiter.send(None)
except MemoryError:
    queue_allocation_blocked = True
finally:
    queue_probe_depth = micropython.heap_unlock()

require(coroutine_probe_depth == 0 and queue_probe_depth == 0,
        "asyncio allocation probe left the heap locked")
require(coroutine_allocation_blocked and queue_allocation_blocked,
        "stock ThreadSafeFlag unexpectedly passed the no-allocation gate")
print("PASS asyncio_allocation_gate", "coroutine_allocates",
      coroutine_allocation_blocked, "ioqueue_allocates", queue_allocation_blocked)

transfer.irq(None)
print("PASS all", [hex(address) for address in i2c.scan()])
