/*
 * Shell.h
 *
 *  Created on: 23.05.2011
 *      Author: Erich Styger
 */

#ifndef SHELL_H_
#define SHELL_H_

void SHELL_SendCommand(unsigned char *cmd);

/*! \brief Serial driver initialization */
void SHELL_Init(void);

#endif /* SHELL_H_ */
