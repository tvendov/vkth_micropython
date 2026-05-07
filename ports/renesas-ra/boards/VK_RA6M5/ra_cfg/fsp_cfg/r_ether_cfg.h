/* generated configuration header file - do not edit */
#ifndef R_ETHER_CFG_H_
#define R_ETHER_CFG_H_
#ifdef __cplusplus
extern "C" {
#endif

#define ETHER_CFG_PARAM_CHECKING_ENABLE   (BSP_CFG_PARAM_CHECKING_ENABLE)
/* Detect link status by polling the PHY's BMSR register from
 * R_ETHER_LinkProcess.  This avoids the LMON pin entirely (which can have
 * wrong polarity wiring on some boards). */
#define ETHER_CFG_USE_LINKSTA             (0)
#define ETHER_CFG_LINK_PRESENT            (0)

#ifdef __cplusplus
}
#endif
#endif /* R_ETHER_CFG_H_ */
