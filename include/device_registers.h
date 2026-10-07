/*
** ###################################################################
**     Abstract:
**         Common include file for CMSIS register access layer headers.
**
**     Copyright (c) 2015 Freescale Semiconductor, Inc.
**     Copyright 2016-2021 NXP
**     All rights reserved.
**
**     THIS SOFTWARE IS PROVIDED BY NXP "AS IS" AND ANY EXPRESSED OR
**     IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
**     OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
**     IN NO EVENT SHALL NXP OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
**     INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
**     (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
**     SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
**     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
**     STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
**     IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
**     THE POSSIBILITY OF SUCH DAMAGE.
**
**     http:                 www.nxp.com
**     mail:                 support@nxp.com
** ###################################################################
*/

#ifndef DEVICE_REGISTERS_H
#define DEVICE_REGISTERS_H

/**
* @page misra_violations MISRA-C:2012 violations
*
* @section [global]
* Violates MISRA 2012 Advisory Rule 2.5, global macro not referenced.
* The macro defines the device currently in use and may be used by components for specific checks.
*
*/


/*
 * Include the cpu specific register header files.
 *
 * The CPU macro should be declared in the project or makefile.
 */

#if (defined(CPU_S32K144HFT0VLLT) || defined(CPU_S32K144LFT0MLLT))

    #define S32K14x_SERIES

    /* Specific core definitions */
    #include "s32_core_cm4.h"

    #define S32K144_SERIES

    /* Register definitions */
    #include "S32K144.h"
/* CPU specific feature definitions */
    #include "S32K144_features.h"
 
#else
    /* Host simulation register mocks for unit testing */
    #include <stdint.h>
    typedef struct {
        volatile uint32_t MCR;
        volatile uint32_t CTRL1;
        volatile uint32_t TIMER;
        volatile uint32_t reserved1[3];
        volatile uint32_t ESR1;
        volatile uint32_t IFLAG1;
        volatile uint32_t reserved2[1];
        volatile uint32_t RAMn[128];
    } CAN_Type;
    extern CAN_Type g_mock_can0;
    #define CAN0 (&g_mock_can0)
    #define CAN_CTRL1_CLKSRC_MASK 0x2000u

    typedef struct {
        volatile uint32_t PCR[32];
    } PORT_Type;
    extern PORT_Type g_mock_portc;
    #define PORTC (&g_mock_portc)
    #define PORT_PCR_MUX(x) (((uint32_t)(x)) << 8)
    #define PORT_PCR_PE_MASK 0x02u
    #define PORT_PCR_PS_MASK 0x01u

    typedef struct {
        volatile uint32_t PDOR;
        volatile uint32_t PSOR;
        volatile uint32_t PCOR;
        volatile uint32_t PTOR;
        volatile uint32_t PDIR;
        volatile uint32_t PDDR;
    } GPIO_Type;
    extern GPIO_Type g_mock_ptc;
    #define PTC (&g_mock_ptc)
#endif

#include "devassert.h"

#endif /* DEVICE_REGISTERS_H */

/*******************************************************************************
 * EOF
 ******************************************************************************/
