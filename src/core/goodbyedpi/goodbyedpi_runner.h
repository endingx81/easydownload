#ifndef _GOODBYEDPI_RUNNER_H
#define _GOODBYEDPI_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

int RunGoodbyeDPI(int argc, char *argv[], volatile int *pStopFlag);
void StopGoodbyeDPI(void);

#ifdef __cplusplus
}
#endif

#endif /* _GOODBYEDPI_RUNNER_H */

