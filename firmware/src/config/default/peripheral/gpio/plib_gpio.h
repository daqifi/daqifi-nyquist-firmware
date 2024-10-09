/*******************************************************************************
  GPIO PLIB

  Company:
    Microchip Technology Inc.

  File Name:
    plib_gpio.h UUUUUUUUU

  Summary:
    GPIO PLIB Header File

  Description:
    This library provides an interface to control and interact with Parallel
    Input/Output controller (GPIO) module.

*******************************************************************************/

/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/

#ifndef PLIB_GPIO_H
#define PLIB_GPIO_H

#include <device.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

    extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Data types and constants
// *****************************************************************************
// *****************************************************************************


/*** Macros for DIO_EN_10 pin ***/
#define DIO_EN_10_Set()               (LATGSET = (1U<<15))
#define DIO_EN_10_Clear()             (LATGCLR = (1U<<15))
#define DIO_EN_10_Toggle()            (LATGINV= (1U<<15))
#define DIO_EN_10_OutputEnable()      (TRISGCLR = (1U<<15))
#define DIO_EN_10_InputEnable()       (TRISGSET = (1U<<15))
#define DIO_EN_10_Get()               ((PORTG >> 15) & 0x1U)
#define DIO_EN_10_PIN                  GPIO_PIN_RG15

/*** Macros for DIO_EN_14 pin ***/
#define DIO_EN_14_Set()               (LATASET = (1U<<5))
#define DIO_EN_14_Clear()             (LATACLR = (1U<<5))
#define DIO_EN_14_Toggle()            (LATAINV= (1U<<5))
#define DIO_EN_14_OutputEnable()      (TRISACLR = (1U<<5))
#define DIO_EN_14_InputEnable()       (TRISASET = (1U<<5))
#define DIO_EN_14_Get()               ((PORTA >> 5) & 0x1U)
#define DIO_EN_14_PIN                  GPIO_PIN_RA5

/*** Macros for DIO_14 pin ***/
#define DIO_14_Set()               (LATESET = (1U<<5))
#define DIO_14_Clear()             (LATECLR = (1U<<5))
#define DIO_14_Toggle()            (LATEINV= (1U<<5))
#define DIO_14_OutputEnable()      (TRISECLR = (1U<<5))
#define DIO_14_InputEnable()       (TRISESET = (1U<<5))
#define DIO_14_Get()               ((PORTE >> 5) & 0x1U)
#define DIO_14_PIN                  GPIO_PIN_RE5

/*** Macros for DIO_13 pin ***/
#define DIO_13_Set()               (LATESET = (1U<<6))
#define DIO_13_Clear()             (LATECLR = (1U<<6))
#define DIO_13_Toggle()            (LATEINV= (1U<<6))
#define DIO_13_OutputEnable()      (TRISECLR = (1U<<6))
#define DIO_13_InputEnable()       (TRISESET = (1U<<6))
#define DIO_13_Get()               ((PORTE >> 6) & 0x1U)
#define DIO_13_PIN                  GPIO_PIN_RE6

/*** Macros for DIO_EN_13 pin ***/
#define DIO_EN_13_Set()               (LATESET = (1U<<7))
#define DIO_EN_13_Clear()             (LATECLR = (1U<<7))
#define DIO_EN_13_Toggle()            (LATEINV= (1U<<7))
#define DIO_EN_13_OutputEnable()      (TRISECLR = (1U<<7))
#define DIO_EN_13_InputEnable()       (TRISESET = (1U<<7))
#define DIO_EN_13_Get()               ((PORTE >> 7) & 0x1U)
#define DIO_EN_13_PIN                  GPIO_PIN_RE7

/*** Macros for DIO_15 pin ***/
#define DIO_15_Set()               (LATCSET = (1U<<1))
#define DIO_15_Clear()             (LATCCLR = (1U<<1))
#define DIO_15_Toggle()            (LATCINV= (1U<<1))
#define DIO_15_OutputEnable()      (TRISCCLR = (1U<<1))
#define DIO_15_InputEnable()       (TRISCSET = (1U<<1))
#define DIO_15_Get()               ((PORTC >> 1) & 0x1U)
#define DIO_15_PIN                  GPIO_PIN_RC1

/*** Macros for DIO_EN_15 pin ***/
#define DIO_EN_15_Set()               (LATJSET = (1U<<12))
#define DIO_EN_15_Clear()             (LATJCLR = (1U<<12))
#define DIO_EN_15_Toggle()            (LATJINV= (1U<<12))
#define DIO_EN_15_OutputEnable()      (TRISJCLR = (1U<<12))
#define DIO_EN_15_InputEnable()       (TRISJSET = (1U<<12))
#define DIO_EN_15_Get()               ((PORTJ >> 12) & 0x1U)
#define DIO_EN_15_PIN                  GPIO_PIN_RJ12

/*** Macros for DIO_EN_11 pin ***/
#define DIO_EN_11_Set()               (LATJSET = (1U<<10))
#define DIO_EN_11_Clear()             (LATJCLR = (1U<<10))
#define DIO_EN_11_Toggle()            (LATJINV= (1U<<10))
#define DIO_EN_11_OutputEnable()      (TRISJCLR = (1U<<10))
#define DIO_EN_11_InputEnable()       (TRISJSET = (1U<<10))
#define DIO_EN_11_Get()               ((PORTJ >> 10) & 0x1U)
#define DIO_EN_11_PIN                  GPIO_PIN_RJ10

/*** Macros for DIO_11 pin ***/
#define DIO_11_Set()               (LATCSET = (1U<<2))
#define DIO_11_Clear()             (LATCCLR = (1U<<2))
#define DIO_11_Toggle()            (LATCINV= (1U<<2))
#define DIO_11_OutputEnable()      (TRISCCLR = (1U<<2))
#define DIO_11_InputEnable()       (TRISCSET = (1U<<2))
#define DIO_11_Get()               ((PORTC >> 2) & 0x1U)
#define DIO_11_PIN                  GPIO_PIN_RC2

/*** Macros for LED_WHITE pin ***/
#define LED_WHITE_Set()               (LATCSET = (1U<<3))
#define LED_WHITE_Clear()             (LATCCLR = (1U<<3))
#define LED_WHITE_Toggle()            (LATCINV= (1U<<3))
#define LED_WHITE_OutputEnable()      (TRISCCLR = (1U<<3))
#define LED_WHITE_InputEnable()       (TRISCSET = (1U<<3))
#define LED_WHITE_Get()               ((PORTC >> 3) & 0x1U)
#define LED_WHITE_PIN                  GPIO_PIN_RC3

/*** Macros for AIN_0 pin ***/
#define AIN_0_Get()               ((PORTG >> 9) & 0x1U)
#define AIN_0_PIN                  GPIO_PIN_RG9

/*** Macros for AIN_1 pin ***/
#define AIN_1_Get()               ((PORTA >> 0) & 0x1U)
#define AIN_1_PIN                  GPIO_PIN_RA0

/*** Macros for AIN_2 pin ***/
#define AIN_2_Get()               ((PORTE >> 8) & 0x1U)
#define AIN_2_PIN                  GPIO_PIN_RE8

/*** Macros for AIN_3 pin ***/
#define AIN_3_Get()               ((PORTE >> 9) & 0x1U)
#define AIN_3_PIN                  GPIO_PIN_RE9

/*** Macros for AIN_8 pin ***/
#define AIN_8_Get()               ((PORTB >> 5) & 0x1U)
#define AIN_8_PIN                  GPIO_PIN_RB5

/*** Macros for AIN_4 pin ***/
#define AIN_4_Get()               ((PORTB >> 4) & 0x1U)
#define AIN_4_PIN                  GPIO_PIN_RB4

/*** Macros for BUTTON pin ***/
#define BUTTON_Set()               (LATJSET = (1U<<14))
#define BUTTON_Clear()             (LATJCLR = (1U<<14))
#define BUTTON_Toggle()            (LATJINV= (1U<<14))
#define BUTTON_OutputEnable()      (TRISJCLR = (1U<<14))
#define BUTTON_InputEnable()       (TRISJSET = (1U<<14))
#define BUTTON_Get()               ((PORTJ >> 14) & 0x1U)
#define BUTTON_PIN                  GPIO_PIN_RJ14

/*** Macros for PWR_VREF_EN pin ***/
#define PWR_VREF_EN_Set()               (LATJSET = (1U<<15))
#define PWR_VREF_EN_Clear()             (LATJCLR = (1U<<15))
#define PWR_VREF_EN_Toggle()            (LATJINV= (1U<<15))
#define PWR_VREF_EN_OutputEnable()      (TRISJCLR = (1U<<15))
#define PWR_VREF_EN_InputEnable()       (TRISJSET = (1U<<15))
#define PWR_VREF_EN_Get()               ((PORTJ >> 15) & 0x1U)
#define PWR_VREF_EN_PIN                  GPIO_PIN_RJ15

/*** Macros for AIN_10 pin ***/
#define AIN_10_Get()               ((PORTB >> 6) & 0x1U)
#define AIN_10_PIN                  GPIO_PIN_RB6

/*** Macros for AIN_12 pin ***/
#define AIN_12_Get()               ((PORTB >> 7) & 0x1U)
#define AIN_12_PIN                  GPIO_PIN_RB7

/*** Macros for AIN_13 pin ***/
#define AIN_13_Get()               ((PORTA >> 9) & 0x1U)
#define AIN_13_PIN                  GPIO_PIN_RA9

/*** Macros for AN_6 pin ***/
#define AN_6_Get()               ((PORTH >> 0) & 0x1U)
#define AN_6_PIN                  GPIO_PIN_RH0

/*** Macros for AN_5 pin ***/
#define AN_5_Get()               ((PORTH >> 1) & 0x1U)
#define AN_5_PIN                  GPIO_PIN_RH1

/*** Macros for AIN_14 pin ***/
#define AIN_14_Get()               ((PORTB >> 8) & 0x1U)
#define AIN_14_PIN                  GPIO_PIN_RB8

/*** Macros for AIN_9 pin ***/
#define AIN_9_Get()               ((PORTB >> 10) & 0x1U)
#define AIN_9_PIN                  GPIO_PIN_RB10

/*** Macros for AIN_11 pin ***/
#define AIN_11_Get()               ((PORTB >> 11) & 0x1U)
#define AIN_11_PIN                  GPIO_PIN_RB11

/*** Macros for PWR_5VREF_MON pin ***/
#define PWR_5VREF_MON_Get()               ((PORTA >> 1) & 0x1U)
#define PWR_5VREF_MON_PIN                  GPIO_PIN_RA1

/*** Macros for PWR_BATT_MON pin ***/
#define PWR_BATT_MON_Get()               ((PORTF >> 13) & 0x1U)
#define PWR_BATT_MON_PIN                  GPIO_PIN_RF13

/*** Macros for PWR_2_5VREF_MON pin ***/
#define PWR_2_5VREF_MON_Get()               ((PORTF >> 12) & 0x1U)
#define PWR_2_5VREF_MON_PIN                  GPIO_PIN_RF12

/*** Macros for AIN_7 pin ***/
#define AIN_7_Get()               ((PORTB >> 12) & 0x1U)
#define AIN_7_PIN                  GPIO_PIN_RB12

/*** Macros for LED_BLUE pin ***/
#define LED_BLUE_Set()               (LATBSET = (1U<<14))
#define LED_BLUE_Clear()             (LATBCLR = (1U<<14))
#define LED_BLUE_Toggle()            (LATBINV= (1U<<14))
#define LED_BLUE_OutputEnable()      (TRISBCLR = (1U<<14))
#define LED_BLUE_InputEnable()       (TRISBSET = (1U<<14))
#define LED_BLUE_Get()               ((PORTB >> 14) & 0x1U)
#define LED_BLUE_PIN                  GPIO_PIN_RB14

/*** Macros for GPIO_RH4 pin ***/
#define GPIO_RH4_Set()               (LATHSET = (1U<<4))
#define GPIO_RH4_Clear()             (LATHCLR = (1U<<4))
#define GPIO_RH4_Toggle()            (LATHINV= (1U<<4))
#define GPIO_RH4_OutputEnable()      (TRISHCLR = (1U<<4))
#define GPIO_RH4_InputEnable()       (TRISHSET = (1U<<4))
#define GPIO_RH4_Get()               ((PORTH >> 4) & 0x1U)
#define GPIO_RH4_PIN                  GPIO_PIN_RH4

/*** Macros for PWR_VSYS_MON pin ***/
#define PWR_VSYS_MON_Get()               ((PORTH >> 5) & 0x1U)
#define PWR_VSYS_MON_PIN                  GPIO_PIN_RH5

/*** Macros for PWER_5_5V_PROT_MON pin ***/
#define PWER_5_5V_PROT_MON_Get()               ((PORTH >> 6) & 0x1U)
#define PWER_5_5V_PROT_MON_PIN                  GPIO_PIN_RH6

/*** Macros for PWR_10V_MON pin ***/
#define PWR_10V_MON_Get()               ((PORTD >> 14) & 0x1U)
#define PWR_10V_MON_PIN                  GPIO_PIN_RD14

/*** Macros for WDRV_WINC_RESETN pin ***/
#define WDRV_WINC_RESETN_Set()               (LATHSET = (1U<<8))
#define WDRV_WINC_RESETN_Clear()             (LATHCLR = (1U<<8))
#define WDRV_WINC_RESETN_Toggle()            (LATHINV= (1U<<8))
#define WDRV_WINC_RESETN_OutputEnable()      (TRISHCLR = (1U<<8))
#define WDRV_WINC_RESETN_InputEnable()       (TRISHSET = (1U<<8))
#define WDRV_WINC_RESETN_Get()               ((PORTH >> 8) & 0x1U)
#define WDRV_WINC_RESETN_PIN                  GPIO_PIN_RH8

/*** Macros for USB_DP_MON pin ***/
#define USB_DP_MON_Set()               (LATHSET = (1U<<9))
#define USB_DP_MON_Clear()             (LATHCLR = (1U<<9))
#define USB_DP_MON_Toggle()            (LATHINV= (1U<<9))
#define USB_DP_MON_OutputEnable()      (TRISHCLR = (1U<<9))
#define USB_DP_MON_InputEnable()       (TRISHSET = (1U<<9))
#define USB_DP_MON_Get()               ((PORTH >> 9) & 0x1U)
#define USB_DP_MON_PIN                  GPIO_PIN_RH9

/*** Macros for USB_DN_MON pin ***/
#define USB_DN_MON_Set()               (LATHSET = (1U<<10))
#define USB_DN_MON_Clear()             (LATHCLR = (1U<<10))
#define USB_DN_MON_Toggle()            (LATHINV= (1U<<10))
#define USB_DN_MON_OutputEnable()      (TRISHCLR = (1U<<10))
#define USB_DN_MON_InputEnable()       (TRISHSET = (1U<<10))
#define USB_DN_MON_Get()               ((PORTH >> 10) & 0x1U)
#define USB_DN_MON_PIN                  GPIO_PIN_RH10

/*** Macros for BATT_MAN_STAT pin ***/
#define BATT_MAN_STAT_Set()               (LATHSET = (1U<<11))
#define BATT_MAN_STAT_Clear()             (LATHCLR = (1U<<11))
#define BATT_MAN_STAT_Toggle()            (LATHINV= (1U<<11))
#define BATT_MAN_STAT_OutputEnable()      (TRISHCLR = (1U<<11))
#define BATT_MAN_STAT_InputEnable()       (TRISHSET = (1U<<11))
#define BATT_MAN_STAT_Get()               ((PORTH >> 11) & 0x1U)
#define BATT_MAN_STAT_PIN                  GPIO_PIN_RH11

/*** Macros for SCL2 pin ***/
#define SCL2_Get()               ((PORTA >> 2) & 0x1U)
#define SCL2_PIN                  GPIO_PIN_RA2

/*** Macros for SDA2 pin ***/
#define SDA2_Get()               ((PORTA >> 3) & 0x1U)
#define SDA2_PIN                  GPIO_PIN_RA3

/*** Macros for BATT_MAN_INT pin ***/
#define BATT_MAN_INT_Set()               (LATASET = (1U<<4))
#define BATT_MAN_INT_Clear()             (LATACLR = (1U<<4))
#define BATT_MAN_INT_Toggle()            (LATAINV= (1U<<4))
#define BATT_MAN_INT_OutputEnable()      (TRISACLR = (1U<<4))
#define BATT_MAN_INT_InputEnable()       (TRISASET = (1U<<4))
#define BATT_MAN_INT_Get()               ((PORTA >> 4) & 0x1U)
#define BATT_MAN_INT_PIN                  GPIO_PIN_RA4
#define BATT_MAN_INT_InterruptEnable()   (CNENASET = (1U<<4))
#define BATT_MAN_INT_InterruptDisable()  (CNENACLR = (1U<<4))

/*** Macros for BATT_MAN_OTG pin ***/
#define BATT_MAN_OTG_Set()               (LATFSET = (1U<<5))
#define BATT_MAN_OTG_Clear()             (LATFCLR = (1U<<5))
#define BATT_MAN_OTG_Toggle()            (LATFINV= (1U<<5))
#define BATT_MAN_OTG_OutputEnable()      (TRISFCLR = (1U<<5))
#define BATT_MAN_OTG_InputEnable()       (TRISFSET = (1U<<5))
#define BATT_MAN_OTG_Get()               ((PORTF >> 5) & 0x1U)
#define BATT_MAN_OTG_PIN                  GPIO_PIN_RF5

/*** Macros for WDRV_WINC_SS pin ***/
#define WDRV_WINC_SS_Set()               (LATKSET = (1U<<4))
#define WDRV_WINC_SS_Clear()             (LATKCLR = (1U<<4))
#define WDRV_WINC_SS_Toggle()            (LATKINV= (1U<<4))
#define WDRV_WINC_SS_OutputEnable()      (TRISKCLR = (1U<<4))
#define WDRV_WINC_SS_InputEnable()       (TRISKSET = (1U<<4))
#define WDRV_WINC_SS_Get()               ((PORTK >> 4) & 0x1U)
#define WDRV_WINC_SS_PIN                  GPIO_PIN_RK4

/*** Macros for GPIO_RK5 pin ***/
#define GPIO_RK5_Set()               (LATKSET = (1U<<5))
#define GPIO_RK5_Clear()             (LATKCLR = (1U<<5))
#define GPIO_RK5_Toggle()            (LATKINV= (1U<<5))
#define GPIO_RK5_OutputEnable()      (TRISKCLR = (1U<<5))
#define GPIO_RK5_InputEnable()       (TRISKSET = (1U<<5))
#define GPIO_RK5_Get()               ((PORTK >> 5) & 0x1U)
#define GPIO_RK5_PIN                  GPIO_PIN_RK5

/*** Macros for SDI4 pin ***/
#define SDI4_Get()               ((PORTA >> 15) & 0x1U)
#define SDI4_PIN                  GPIO_PIN_RA15

/*** Macros for SD_SS pin ***/
#define SD_SS_Set()               (LATDSET = (1U<<9))
#define SD_SS_Clear()             (LATDCLR = (1U<<9))
#define SD_SS_Toggle()            (LATDINV= (1U<<9))
#define SD_SS_OutputEnable()      (TRISDCLR = (1U<<9))
#define SD_SS_InputEnable()       (TRISDSET = (1U<<9))
#define SD_SS_Get()               ((PORTD >> 9) & 0x1U)
#define SD_SS_PIN                  GPIO_PIN_RD9

/*** Macros for SCK4 pin ***/
#define SCK4_Get()               ((PORTD >> 10) & 0x1U)
#define SCK4_PIN                  GPIO_PIN_RD10

/*** Macros for WDRV_WINC_IRQ pin ***/
#define WDRV_WINC_IRQ_Set()               (LATDSET = (1U<<11))
#define WDRV_WINC_IRQ_Clear()             (LATDCLR = (1U<<11))
#define WDRV_WINC_IRQ_Toggle()            (LATDINV= (1U<<11))
#define WDRV_WINC_IRQ_OutputEnable()      (TRISDCLR = (1U<<11))
#define WDRV_WINC_IRQ_InputEnable()       (TRISDSET = (1U<<11))
#define WDRV_WINC_IRQ_Get()               ((PORTD >> 11) & 0x1U)
#define WDRV_WINC_IRQ_PIN                  GPIO_PIN_RD11
#define WDRV_WINC_IRQ_InterruptEnable()   (CNENDSET = (1U<<11))
#define WDRV_WINC_IRQ_InterruptDisable()  (CNENDCLR = (1U<<11))

/*** Macros for PWR_3_3V_EN pin ***/
#define PWR_3_3V_EN_Set()               (LATHSET = (1U<<12))
#define PWR_3_3V_EN_Clear()             (LATHCLR = (1U<<12))
#define PWR_3_3V_EN_Toggle()            (LATHINV= (1U<<12))
#define PWR_3_3V_EN_OutputEnable()      (TRISHCLR = (1U<<12))
#define PWR_3_3V_EN_InputEnable()       (TRISHSET = (1U<<12))
#define PWR_3_3V_EN_Get()               ((PORTH >> 12) & 0x1U)
#define PWR_3_3V_EN_PIN                  GPIO_PIN_RH12

/*** Macros for WDRV_WINC_CHIP_EN pin ***/
#define WDRV_WINC_CHIP_EN_Set()               (LATHSET = (1U<<13))
#define WDRV_WINC_CHIP_EN_Clear()             (LATHCLR = (1U<<13))
#define WDRV_WINC_CHIP_EN_Toggle()            (LATHINV= (1U<<13))
#define WDRV_WINC_CHIP_EN_OutputEnable()      (TRISHCLR = (1U<<13))
#define WDRV_WINC_CHIP_EN_InputEnable()       (TRISHSET = (1U<<13))
#define WDRV_WINC_CHIP_EN_Get()               ((PORTH >> 13) & 0x1U)
#define WDRV_WINC_CHIP_EN_PIN                  GPIO_PIN_RH13

/*** Macros for PWR_12V_EN pin ***/
#define PWR_12V_EN_Set()               (LATHSET = (1U<<15))
#define PWR_12V_EN_Clear()             (LATHCLR = (1U<<15))
#define PWR_12V_EN_Toggle()            (LATHINV= (1U<<15))
#define PWR_12V_EN_OutputEnable()      (TRISHCLR = (1U<<15))
#define PWR_12V_EN_InputEnable()       (TRISHSET = (1U<<15))
#define PWR_12V_EN_Get()               ((PORTH >> 15) & 0x1U)
#define PWR_12V_EN_PIN                  GPIO_PIN_RH15

/*** Macros for PWR_5V_EN pin ***/
#define PWR_5V_EN_Set()               (LATDSET = (1U<<0))
#define PWR_5V_EN_Clear()             (LATDCLR = (1U<<0))
#define PWR_5V_EN_Toggle()            (LATDINV= (1U<<0))
#define PWR_5V_EN_OutputEnable()      (TRISDCLR = (1U<<0))
#define PWR_5V_EN_InputEnable()       (TRISDSET = (1U<<0))
#define PWR_5V_EN_Get()               ((PORTD >> 0) & 0x1U)
#define PWR_5V_EN_PIN                  GPIO_PIN_RD0

/*** Macros for SDO4 pin ***/
#define SDO4_Get()               ((PORTC >> 13) & 0x1U)
#define SDO4_PIN                  GPIO_PIN_RC13

/*** Macros for DIO_0 pin ***/
#define DIO_0_Get()               ((PORTD >> 1) & 0x1U)
#define DIO_0_PIN                  GPIO_PIN_RD1

/*** Macros for DIO_EN_0 pin ***/
#define DIO_EN_0_Set()               (LATDSET = (1U<<2))
#define DIO_EN_0_Clear()             (LATDCLR = (1U<<2))
#define DIO_EN_0_Toggle()            (LATDINV= (1U<<2))
#define DIO_EN_0_OutputEnable()      (TRISDCLR = (1U<<2))
#define DIO_EN_0_InputEnable()       (TRISDSET = (1U<<2))
#define DIO_EN_0_Get()               ((PORTD >> 2) & 0x1U)
#define DIO_EN_0_PIN                  GPIO_PIN_RD2

/*** Macros for DIO_2 pin ***/
#define DIO_2_Set()               (LATDSET = (1U<<3))
#define DIO_2_Clear()             (LATDCLR = (1U<<3))
#define DIO_2_Toggle()            (LATDINV= (1U<<3))
#define DIO_2_OutputEnable()      (TRISDCLR = (1U<<3))
#define DIO_2_InputEnable()       (TRISDSET = (1U<<3))
#define DIO_2_Get()               ((PORTD >> 3) & 0x1U)
#define DIO_2_PIN                  GPIO_PIN_RD3

/*** Macros for DIO_3 pin ***/
#define DIO_3_Get()               ((PORTD >> 12) & 0x1U)
#define DIO_3_PIN                  GPIO_PIN_RD12

/*** Macros for DIO_EN_2 pin ***/
#define DIO_EN_2_Set()               (LATDSET = (1U<<13))
#define DIO_EN_2_Clear()             (LATDCLR = (1U<<13))
#define DIO_EN_2_Toggle()            (LATDINV= (1U<<13))
#define DIO_EN_2_OutputEnable()      (TRISDCLR = (1U<<13))
#define DIO_EN_2_InputEnable()       (TRISDSET = (1U<<13))
#define DIO_EN_2_Get()               ((PORTD >> 13) & 0x1U)
#define DIO_EN_2_PIN                  GPIO_PIN_RD13

/*** Macros for DIO_EN_3 pin ***/
#define DIO_EN_3_Set()               (LATJSET = (1U<<0))
#define DIO_EN_3_Clear()             (LATJCLR = (1U<<0))
#define DIO_EN_3_Toggle()            (LATJINV= (1U<<0))
#define DIO_EN_3_OutputEnable()      (TRISJCLR = (1U<<0))
#define DIO_EN_3_InputEnable()       (TRISJSET = (1U<<0))
#define DIO_EN_3_Get()               ((PORTJ >> 0) & 0x1U)
#define DIO_EN_3_PIN                  GPIO_PIN_RJ0

/*** Macros for DIO_EN_1 pin ***/
#define DIO_EN_1_Set()               (LATJSET = (1U<<2))
#define DIO_EN_1_Clear()             (LATJCLR = (1U<<2))
#define DIO_EN_1_Toggle()            (LATJINV= (1U<<2))
#define DIO_EN_1_OutputEnable()      (TRISJCLR = (1U<<2))
#define DIO_EN_1_InputEnable()       (TRISJSET = (1U<<2))
#define DIO_EN_1_Get()               ((PORTJ >> 2) & 0x1U)
#define DIO_EN_1_PIN                  GPIO_PIN_RJ2

/*** Macros for DIO_1 pin ***/
#define DIO_1_Set()               (LATJSET = (1U<<3))
#define DIO_1_Clear()             (LATJCLR = (1U<<3))
#define DIO_1_Toggle()            (LATJINV= (1U<<3))
#define DIO_1_OutputEnable()      (TRISJCLR = (1U<<3))
#define DIO_1_InputEnable()       (TRISJSET = (1U<<3))
#define DIO_1_Get()               ((PORTJ >> 3) & 0x1U)
#define DIO_1_PIN                  GPIO_PIN_RJ3

/*** Macros for DIO_EN_4 pin ***/
#define DIO_EN_4_Set()               (LATDSET = (1U<<7))
#define DIO_EN_4_Clear()             (LATDCLR = (1U<<7))
#define DIO_EN_4_Toggle()            (LATDINV= (1U<<7))
#define DIO_EN_4_OutputEnable()      (TRISDCLR = (1U<<7))
#define DIO_EN_4_InputEnable()       (TRISDSET = (1U<<7))
#define DIO_EN_4_Get()               ((PORTD >> 7) & 0x1U)
#define DIO_EN_4_PIN                  GPIO_PIN_RD7

/*** Macros for DIO_4 pin ***/
#define DIO_4_Get()               ((PORTF >> 0) & 0x1U)
#define DIO_4_PIN                  GPIO_PIN_RF0

/*** Macros for DIO_5 pin ***/
#define DIO_5_Get()               ((PORTF >> 1) & 0x1U)
#define DIO_5_PIN                  GPIO_PIN_RF1

/*** Macros for DIO_EN_5 pin ***/
#define DIO_EN_5_Set()               (LATKSET = (1U<<7))
#define DIO_EN_5_Clear()             (LATKCLR = (1U<<7))
#define DIO_EN_5_Toggle()            (LATKINV= (1U<<7))
#define DIO_EN_5_OutputEnable()      (TRISKCLR = (1U<<7))
#define DIO_EN_5_InputEnable()       (TRISKSET = (1U<<7))
#define DIO_EN_5_Get()               ((PORTK >> 7) & 0x1U)
#define DIO_EN_5_PIN                  GPIO_PIN_RK7

/*** Macros for DIO_7 pin ***/
#define DIO_7_Get()               ((PORTG >> 1) & 0x1U)
#define DIO_7_PIN                  GPIO_PIN_RG1

/*** Macros for DIO_6 pin ***/
#define DIO_6_Get()               ((PORTG >> 0) & 0x1U)
#define DIO_6_PIN                  GPIO_PIN_RG0

/*** Macros for DIO_EN_6 pin ***/
#define DIO_EN_6_Set()               (LATJSET = (1U<<4))
#define DIO_EN_6_Clear()             (LATJCLR = (1U<<4))
#define DIO_EN_6_Toggle()            (LATJINV= (1U<<4))
#define DIO_EN_6_OutputEnable()      (TRISJCLR = (1U<<4))
#define DIO_EN_6_InputEnable()       (TRISJSET = (1U<<4))
#define DIO_EN_6_Get()               ((PORTJ >> 4) & 0x1U)
#define DIO_EN_6_PIN                  GPIO_PIN_RJ4

/*** Macros for DIO_EN_7 pin ***/
#define DIO_EN_7_Set()               (LATJSET = (1U<<5))
#define DIO_EN_7_Clear()             (LATJCLR = (1U<<5))
#define DIO_EN_7_Toggle()            (LATJINV= (1U<<5))
#define DIO_EN_7_OutputEnable()      (TRISJCLR = (1U<<5))
#define DIO_EN_7_InputEnable()       (TRISJSET = (1U<<5))
#define DIO_EN_7_Get()               ((PORTJ >> 5) & 0x1U)
#define DIO_EN_7_PIN                  GPIO_PIN_RJ5

/*** Macros for DIO_8 pin ***/
#define DIO_8_Set()               (LATJSET = (1U<<6))
#define DIO_8_Clear()             (LATJCLR = (1U<<6))
#define DIO_8_Toggle()            (LATJINV= (1U<<6))
#define DIO_8_OutputEnable()      (TRISJCLR = (1U<<6))
#define DIO_8_InputEnable()       (TRISJSET = (1U<<6))
#define DIO_8_Get()               ((PORTJ >> 6) & 0x1U)
#define DIO_8_PIN                  GPIO_PIN_RJ6

/*** Macros for DIO_EN_8 pin ***/
#define DIO_EN_8_Set()               (LATJSET = (1U<<7))
#define DIO_EN_8_Clear()             (LATJCLR = (1U<<7))
#define DIO_EN_8_Toggle()            (LATJINV= (1U<<7))
#define DIO_EN_8_OutputEnable()      (TRISJCLR = (1U<<7))
#define DIO_EN_8_InputEnable()       (TRISJSET = (1U<<7))
#define DIO_EN_8_Get()               ((PORTJ >> 7) & 0x1U)
#define DIO_EN_8_PIN                  GPIO_PIN_RJ7

/*** Macros for DIO_EN_9 pin ***/
#define DIO_EN_9_Set()               (LATESET = (1U<<0))
#define DIO_EN_9_Clear()             (LATECLR = (1U<<0))
#define DIO_EN_9_Toggle()            (LATEINV= (1U<<0))
#define DIO_EN_9_OutputEnable()      (TRISECLR = (1U<<0))
#define DIO_EN_9_InputEnable()       (TRISESET = (1U<<0))
#define DIO_EN_9_Get()               ((PORTE >> 0) & 0x1U)
#define DIO_EN_9_PIN                  GPIO_PIN_RE0

/*** Macros for DIO_9 pin ***/
#define DIO_9_Set()               (LATESET = (1U<<1))
#define DIO_9_Clear()             (LATECLR = (1U<<1))
#define DIO_9_Toggle()            (LATEINV= (1U<<1))
#define DIO_9_OutputEnable()      (TRISECLR = (1U<<1))
#define DIO_9_InputEnable()       (TRISESET = (1U<<1))
#define DIO_9_Get()               ((PORTE >> 1) & 0x1U)
#define DIO_9_PIN                  GPIO_PIN_RE1

/*** Macros for DIO_EN_12 pin ***/
#define DIO_EN_12_Set()               (LATESET = (1U<<2))
#define DIO_EN_12_Clear()             (LATECLR = (1U<<2))
#define DIO_EN_12_Toggle()            (LATEINV= (1U<<2))
#define DIO_EN_12_OutputEnable()      (TRISECLR = (1U<<2))
#define DIO_EN_12_InputEnable()       (TRISESET = (1U<<2))
#define DIO_EN_12_Get()               ((PORTE >> 2) & 0x1U)
#define DIO_EN_12_PIN                  GPIO_PIN_RE2

/*** Macros for DIO_12 pin ***/
#define DIO_12_Set()               (LATESET = (1U<<3))
#define DIO_12_Clear()             (LATECLR = (1U<<3))
#define DIO_12_Toggle()            (LATEINV= (1U<<3))
#define DIO_12_OutputEnable()      (TRISECLR = (1U<<3))
#define DIO_12_InputEnable()       (TRISESET = (1U<<3))
#define DIO_12_Get()               ((PORTE >> 3) & 0x1U)
#define DIO_12_PIN                  GPIO_PIN_RE3

/*** Macros for DIO_10 pin ***/
#define DIO_10_Set()               (LATESET = (1U<<4))
#define DIO_10_Clear()             (LATECLR = (1U<<4))
#define DIO_10_Toggle()            (LATEINV= (1U<<4))
#define DIO_10_OutputEnable()      (TRISECLR = (1U<<4))
#define DIO_10_InputEnable()       (TRISESET = (1U<<4))
#define DIO_10_Get()               ((PORTE >> 4) & 0x1U)
#define DIO_10_PIN                  GPIO_PIN_RE4


// *****************************************************************************
/* GPIO Port

  Summary:
    Identifies the available GPIO Ports.

  Description:
    This enumeration identifies the available GPIO Ports.

  Remarks:
    The caller should not rely on the specific numbers assigned to any of
    these values as they may change from one processor to the next.

    Not all ports are available on all devices.  Refer to the specific
    device data sheet to determine which ports are supported.
*/


#define    GPIO_PORT_A  (0)
#define    GPIO_PORT_B  (1)
#define    GPIO_PORT_C  (2)
#define    GPIO_PORT_D  (3)
#define    GPIO_PORT_E  (4)
#define    GPIO_PORT_F  (5)
#define    GPIO_PORT_G  (6)
#define    GPIO_PORT_H  (7)
#define    GPIO_PORT_J  (8)
#define    GPIO_PORT_K  (9)
typedef uint32_t GPIO_PORT;

typedef enum
{
    GPIO_INTERRUPT_ON_MISMATCH,
    GPIO_INTERRUPT_ON_RISING_EDGE,
    GPIO_INTERRUPT_ON_FALLING_EDGE,
    GPIO_INTERRUPT_ON_BOTH_EDGES,
}GPIO_INTERRUPT_STYLE;

// *****************************************************************************
/* GPIO Port Pins

  Summary:
    Identifies the available GPIO port pins.

  Description:
    This enumeration identifies the available GPIO port pins.

  Remarks:
    The caller should not rely on the specific numbers assigned to any of
    these values as they may change from one processor to the next.

    Not all pins are available on all devices.  Refer to the specific
    device data sheet to determine which pins are supported.
*/


#define     GPIO_PIN_RA0  (0U)
#define     GPIO_PIN_RA1  (1U)
#define     GPIO_PIN_RA2  (2U)
#define     GPIO_PIN_RA3  (3U)
#define     GPIO_PIN_RA4  (4U)
#define     GPIO_PIN_RA5  (5U)
#define     GPIO_PIN_RA6  (6U)
#define     GPIO_PIN_RA7  (7U)
#define     GPIO_PIN_RA9  (9U)
#define     GPIO_PIN_RA10  (10U)
#define     GPIO_PIN_RA14  (14U)
#define     GPIO_PIN_RA15  (15U)
#define     GPIO_PIN_RB0  (16U)
#define     GPIO_PIN_RB1  (17U)
#define     GPIO_PIN_RB2  (18U)
#define     GPIO_PIN_RB3  (19U)
#define     GPIO_PIN_RB4  (20U)
#define     GPIO_PIN_RB5  (21U)
#define     GPIO_PIN_RB6  (22U)
#define     GPIO_PIN_RB7  (23U)
#define     GPIO_PIN_RB8  (24U)
#define     GPIO_PIN_RB9  (25U)
#define     GPIO_PIN_RB10  (26U)
#define     GPIO_PIN_RB11  (27U)
#define     GPIO_PIN_RB12  (28U)
#define     GPIO_PIN_RB13  (29U)
#define     GPIO_PIN_RB14  (30U)
#define     GPIO_PIN_RB15  (31U)
#define     GPIO_PIN_RC1  (33U)
#define     GPIO_PIN_RC2  (34U)
#define     GPIO_PIN_RC3  (35U)
#define     GPIO_PIN_RC4  (36U)
#define     GPIO_PIN_RC12  (44U)
#define     GPIO_PIN_RC13  (45U)
#define     GPIO_PIN_RC14  (46U)
#define     GPIO_PIN_RC15  (47U)
#define     GPIO_PIN_RD0  (48U)
#define     GPIO_PIN_RD1  (49U)
#define     GPIO_PIN_RD2  (50U)
#define     GPIO_PIN_RD3  (51U)
#define     GPIO_PIN_RD4  (52U)
#define     GPIO_PIN_RD5  (53U)
#define     GPIO_PIN_RD6  (54U)
#define     GPIO_PIN_RD7  (55U)
#define     GPIO_PIN_RD9  (57U)
#define     GPIO_PIN_RD10  (58U)
#define     GPIO_PIN_RD11  (59U)
#define     GPIO_PIN_RD12  (60U)
#define     GPIO_PIN_RD13  (61U)
#define     GPIO_PIN_RD14  (62U)
#define     GPIO_PIN_RD15  (63U)
#define     GPIO_PIN_RE0  (64U)
#define     GPIO_PIN_RE1  (65U)
#define     GPIO_PIN_RE2  (66U)
#define     GPIO_PIN_RE3  (67U)
#define     GPIO_PIN_RE4  (68U)
#define     GPIO_PIN_RE5  (69U)
#define     GPIO_PIN_RE6  (70U)
#define     GPIO_PIN_RE7  (71U)
#define     GPIO_PIN_RE8  (72U)
#define     GPIO_PIN_RE9  (73U)
#define     GPIO_PIN_RF0  (80U)
#define     GPIO_PIN_RF1  (81U)
#define     GPIO_PIN_RF2  (82U)
#define     GPIO_PIN_RF3  (83U)
#define     GPIO_PIN_RF4  (84U)
#define     GPIO_PIN_RF5  (85U)
#define     GPIO_PIN_RF8  (88U)
#define     GPIO_PIN_RF12  (92U)
#define     GPIO_PIN_RF13  (93U)
#define     GPIO_PIN_RG0  (96U)
#define     GPIO_PIN_RG1  (97U)
#define     GPIO_PIN_RG6  (102U)
#define     GPIO_PIN_RG7  (103U)
#define     GPIO_PIN_RG8  (104U)
#define     GPIO_PIN_RG9  (105U)
#define     GPIO_PIN_RG12  (108U)
#define     GPIO_PIN_RG13  (109U)
#define     GPIO_PIN_RG14  (110U)
#define     GPIO_PIN_RG15  (111U)
#define     GPIO_PIN_RH0  (112U)
#define     GPIO_PIN_RH1  (113U)
#define     GPIO_PIN_RH2  (114U)
#define     GPIO_PIN_RH3  (115U)
#define     GPIO_PIN_RH4  (116U)
#define     GPIO_PIN_RH5  (117U)
#define     GPIO_PIN_RH6  (118U)
#define     GPIO_PIN_RH7  (119U)
#define     GPIO_PIN_RH8  (120U)
#define     GPIO_PIN_RH9  (121U)
#define     GPIO_PIN_RH10  (122U)
#define     GPIO_PIN_RH11  (123U)
#define     GPIO_PIN_RH12  (124U)
#define     GPIO_PIN_RH13  (125U)
#define     GPIO_PIN_RH14  (126U)
#define     GPIO_PIN_RH15  (127U)
#define     GPIO_PIN_RJ0  (128U)
#define     GPIO_PIN_RJ1  (129U)
#define     GPIO_PIN_RJ2  (130U)
#define     GPIO_PIN_RJ3  (131U)
#define     GPIO_PIN_RJ4  (132U)
#define     GPIO_PIN_RJ5  (133U)
#define     GPIO_PIN_RJ6  (134U)
#define     GPIO_PIN_RJ7  (135U)
#define     GPIO_PIN_RJ8  (136U)
#define     GPIO_PIN_RJ9  (137U)
#define     GPIO_PIN_RJ10  (138U)
#define     GPIO_PIN_RJ11  (139U)
#define     GPIO_PIN_RJ12  (140U)
#define     GPIO_PIN_RJ13  (141U)
#define     GPIO_PIN_RJ14  (142U)
#define     GPIO_PIN_RJ15  (143U)
#define     GPIO_PIN_RK0  (144U)
#define     GPIO_PIN_RK1  (145U)
#define     GPIO_PIN_RK2  (146U)
#define     GPIO_PIN_RK3  (147U)
#define     GPIO_PIN_RK4  (148U)
#define     GPIO_PIN_RK5  (149U)
#define     GPIO_PIN_RK6  (150U)
#define     GPIO_PIN_RK7  (151U)

    /* This element should not be used in any of the GPIO APIs.
       It will be used by other modules or application to denote that none of the GPIO Pin is used */
#define    GPIO_PIN_NONE   (-1)

typedef uint32_t GPIO_PIN;

typedef  void (*GPIO_PIN_CALLBACK) ( GPIO_PIN pin, uintptr_t context);

void GPIO_Initialize(void);

// *****************************************************************************
// *****************************************************************************
// Section: GPIO Functions which operates on multiple pins of a port
// *****************************************************************************
// *****************************************************************************

uint32_t GPIO_PortRead(GPIO_PORT port);

void GPIO_PortWrite(GPIO_PORT port, uint32_t mask, uint32_t value);

uint32_t GPIO_PortLatchRead ( GPIO_PORT port );

void GPIO_PortSet(GPIO_PORT port, uint32_t mask);

void GPIO_PortClear(GPIO_PORT port, uint32_t mask);

void GPIO_PortToggle(GPIO_PORT port, uint32_t mask);

void GPIO_PortInputEnable(GPIO_PORT port, uint32_t mask);

void GPIO_PortOutputEnable(GPIO_PORT port, uint32_t mask);

void GPIO_PortInterruptEnable(GPIO_PORT port, uint32_t mask);

void GPIO_PortInterruptDisable(GPIO_PORT port, uint32_t mask);

// *****************************************************************************
// *****************************************************************************
// Section: Local Data types and Prototypes
// *****************************************************************************
// *****************************************************************************

typedef struct {

    /* target pin */
    GPIO_PIN                 pin;

    /* Callback for event on target pin*/
    GPIO_PIN_CALLBACK        callback;

    /* Callback Context */
    uintptr_t               context;

} GPIO_PIN_CALLBACK_OBJ;

// *****************************************************************************
// *****************************************************************************
// Section: GPIO Functions which operates on one pin at a time
// *****************************************************************************
// *****************************************************************************

static inline void GPIO_PinWrite(GPIO_PIN pin, bool value)
{
	 uint32_t xvalue = (uint32_t)value;
    GPIO_PortWrite((pin>>4U), (uint32_t)(0x1U) << (pin & 0xFU), (xvalue) << (pin & 0xFU));
}

static inline bool GPIO_PinRead(GPIO_PIN pin)
{
    return ((((GPIO_PortRead((GPIO_PORT)(pin>>4U))) >> (pin & 0xFU)) & 0x1U) != 0U);
}

static inline bool GPIO_PinLatchRead(GPIO_PIN pin)
{
    return (((GPIO_PortLatchRead((GPIO_PORT)(pin>>4U)) >> (pin & 0xFU)) & 0x1U) != 0U);
}

static inline void GPIO_PinToggle(GPIO_PIN pin)
{
    GPIO_PortToggle((pin>>4U), (uint32_t)0x1U << (pin & 0xFU));
}

static inline void GPIO_PinSet(GPIO_PIN pin)
{
    GPIO_PortSet((pin>>4U), (uint32_t)0x1U << (pin & 0xFU));
}

static inline void GPIO_PinClear(GPIO_PIN pin)
{
    GPIO_PortClear((pin>>4U), (uint32_t)0x1U << (pin & 0xFU));
}

static inline void GPIO_PinInputEnable(GPIO_PIN pin)
{
    GPIO_PortInputEnable((pin>>4U), (uint32_t)0x1U << (pin & 0xFU));
}

static inline void GPIO_PinOutputEnable(GPIO_PIN pin)
{
    GPIO_PortOutputEnable((pin>>4U), (uint32_t)0x1U << (pin & 0xFU));
}

#define GPIO_PinInterruptEnable(pin)       GPIO_PinIntEnable(pin, GPIO_INTERRUPT_ON_MISMATCH)
#define GPIO_PinInterruptDisable(pin)      GPIO_PinIntDisable(pin)

void GPIO_PinIntEnable(GPIO_PIN pin, GPIO_INTERRUPT_STYLE style);
void GPIO_PinIntDisable(GPIO_PIN pin);

bool GPIO_PinInterruptCallbackRegister(
    GPIO_PIN pin,
    const   GPIO_PIN_CALLBACK callback,
    uintptr_t context
);

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

    }

#endif
// DOM-IGNORE-END
#endif // PLIB_GPIO_H
