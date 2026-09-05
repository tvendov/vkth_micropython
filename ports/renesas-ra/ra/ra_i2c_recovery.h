// Static, cooperative recovery state. All times are unsigned microseconds.
// One call performs at most one state transition; no peripheral wait loops.
#ifndef RA_I2C_RECOVERY_H
#define RA_I2C_RECOVERY_H
typedef enum {
    REC_IDLE, REC_QUIESCE, REC_BUS_CHECK, REC_LOW, REC_WAIT_HIGH,
    REC_HIGH, REC_STOP_LOW, REC_STOP_WAIT_HIGH, REC_STOP_HIGH,
    REC_STOP_SETTLE, REC_RESTORE, REC_DONE,
} ra_i2c_recovery_state_t;
typedef struct {
    R_IIC0_Type *inst;
    uint32_t scl, sda, freq, started, edge;
    ra_i2c_recovery_state_t state;
    unsigned pulses;
    int result; // 0 unused/pending; 1 restored; -1 SCL; -2 SDA; -3 deadline;
                // -4 RIIC readback; -5 DTC not quiescent (buffer quarantined).
    bool buffer_safe;
} ra_i2c_recovery_t;
#define RA_I2C_RECOVERY_HOLD_US (1000U)
#define RA_I2C_RECOVERY_STRETCH_US (5000U)
#define RA_I2C_RECOVERY_DEADLINE_US (100000U)
#endif
