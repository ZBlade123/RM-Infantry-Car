#ifndef __CAR_H
#define __CAR_H

#include "main.h"

/* ????????:0~1000 */

/* ???? */
void Car_Forward(int16_t speed);
void Car_Backward(int16_t speed);

/* ???? */
void Car_Left(int16_t speed);
void Car_Right(int16_t speed);

/* ?????? */
void Car_ForwardLeft(int16_t speed);
void Car_ForwardRight(int16_t speed);
void Car_BackwardLeft(int16_t speed);
void Car_BackwardRight(int16_t speed);

/* ???? */
void Car_RotateLeft(int16_t speed);
void Car_RotateRight(int16_t speed);

/* ?? */
void Car_Stop(void);

#endif