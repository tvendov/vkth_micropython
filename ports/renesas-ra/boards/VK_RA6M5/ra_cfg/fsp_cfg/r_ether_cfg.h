/* generated configuration header file - do not edit */
#ifndef R_ETHER_CFG_H_
#define R_ETHER_CFG_H_
#ifdef __cplusplus
extern "C" {
#endif

#define ETHER_CFG_PARAM_CHECKING_ENABLE   (BSP_CFG_PARAM_CHECKING_ENABLE)
/* RA6M5 EDMAC has hardware link-status detection (LMON), use it. */
#define ETHER_CFG_USE_LINKSTA             (1)
/* Polarity: PSR.LMON = 1 when link is up on this board. */
#define ETHER_CFG_LINK_PRESENT            (1)

#ifdef __cplusplus
}
#endif
#endif /* R_ETHER_CFG_H_ */
