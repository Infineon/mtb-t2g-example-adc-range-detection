/**********************************************************************************************************************
 * \file main.c
 * \copyright Copyright (C) Infineon Technologies AG 2019
 *
 * Use of this file is subject to the terms of use agreed between (i) you or the company in which ordinary course of
 * business you are acting and (ii) Infineon Technologies AG or its licensees. If and as long as no such terms of use
 * are agreed, use of this file is subject to following:
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization obtaining a copy of the software and
 * accompanying documentation covered by this license (the "Software") to use, reproduce, display, distribute, execute,
 * and transmit the Software, and to prepare derivative works of the Software, and to permit third-parties to whom the
 * Software is furnished to do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including the above license grant, this restriction
 * and the following disclaimer, must be included in all copies of the Software, in whole or in part, and all
 * derivative works of the Software, unless such copies or derivative works are solely in the form of
 * machine-executable object code generated by a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *********************************************************************************************************************/
/*********************************************************************************************************************/
/*-----------------------------------------------------Includes------------------------------------------------------*/
/*********************************************************************************************************************/
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/*********************************************************************************************************************/
/*------------------------------------------------------Macros-------------------------------------------------------*/
/*********************************************************************************************************************/
/* ADC interrupt priority */
#define IRQ_PRIORITY            2

/* Shift value for CPU IRQ number ('intSrc' of cy_stc_sysint_t consists of CPU IRQ number and system IRQ number) */
#define CPU_IRQ_NUMBER_SHIFT    16

/* ADC voltage levels */
#define ADC_4_5V_LEVEL          ((uint16_t) (4095 * 4.5 / 5))
#define ADC_3_5V_LEVEL          ((uint16_t) (4095 * 3.5 / 5))
#define ADC_2_5V_LEVEL          ((uint16_t) (4095 * 2.5 / 5))
#define ADC_1_5V_LEVEL          ((uint16_t) (4095 * 1.5 / 5))
#define ADC_0_5V_LEVEL          ((uint16_t) (4095 * 0.5 / 5))

/*********************************************************************************************************************/
/*-------------------------------------------------Global variables--------------------------------------------------*/
/*********************************************************************************************************************/
/* Flag set when the ADC output voltage is out of range */
bool g_rangeDetected = false;

/*********************************************************************************************************************/
/*---------------------------------------------Function Implementations----------------------------------------------*/
/*********************************************************************************************************************/
/**********************************************************************************************************************
 * Function Name: handleAdcIrq
 * Summary:
 *  ADC Interrupt Handler
 * Parameters:
 *  none
 * Return:
 *  none
 **********************************************************************************************************************
 */
void handleAdcIrq(void)
{
    /* Get interrupt source */
    uint32_t irqStatus = Cy_SAR2_Channel_GetInterruptStatusMasked(SARADC_HW, SARADC_RangeDetectionChannel_IDX);

    if (irqStatus == CY_SAR2_INT_CH_RANGE)
    {
        /* Set a flag to inform the main loop of the event */
        g_rangeDetected = true;

        /* Clear interrupt source */
        Cy_SAR2_Channel_ClearInterrupt(SARADC_HW, SARADC_RangeDetectionChannel_IDX, CY_SAR2_INT_CH_RANGE);
    }
    else
    {
        /* Unexpected interrupt */
        CY_ASSERT(false);
    }
}

/**********************************************************************************************************************
 * Function Name: configureAdcRangeDetectionMode
 * Summary:
 *  Function to configure ADC range detection parameters, initialize SAR ADC module, enable interrupt and start
 *  conversion.
 * Parameters:
 *  detectionMode - range detection mode selection
 *  upperThreshold - value for upper threshold
 *  lowerThreshold - value for lower threshold
 * Return:
 *  none
 **********************************************************************************************************************
 */
static void configureAdcRangeDetectionMode(cy_en_sar2_range_detection_mode_t detectionMode,
                                            uint16_t upperThreshold, uint16_t lowerThreshold)
{
    /* SAR block is disabled for setting up new range detection parameters */
    Cy_SAR2_Disable(SARADC_HW);
    
    SARADC_RangeDetectionChannel_config.rangeDetectionMode = detectionMode;
    SARADC_RangeDetectionChannel_config.rangeDetectionHiThreshold = upperThreshold;
    SARADC_RangeDetectionChannel_config.rangeDetectionLoThreshold = lowerThreshold;
    
    /* Initialize the SAR module */
    Cy_SAR2_Init(SARADC_HW, &SARADC_config);
    
    /* (Re-)Enable SAR block */
    Cy_SAR2_Enable(SARADC_HW);

    /* Set up the interrupt processing for ADC */
    cy_stc_sysint_t irqCfg =
    {
        .intrSrc = ((NvicMux3_IRQn << CPU_IRQ_NUMBER_SHIFT) | SARADC_RangeDetectionChannel_IRQ),
        .intrPriority = IRQ_PRIORITY,
    };
    if (Cy_SysInt_Init(&irqCfg, &handleAdcIrq) != CY_SYSINT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    NVIC_EnableIRQ((IRQn_Type) NvicMux3_IRQn);    

    Cy_SAR2_Channel_SetInterruptMask(SARADC_HW, SARADC_RangeDetectionChannel_IDX, CY_SAR2_INT_CH_RANGE);
    
    /* Start first ADC conversion, the ADC will then be re-triggered by hardware in continuous mode */
    Cy_SAR2_Channel_SoftwareTrigger(SARADC_HW, SARADC_RangeDetectionChannel_IDX);
}

/**********************************************************************************************************************
 * Function Name: main
 * Summary:
 *  This is the main function. It initializes the BSP and the retarget-io library. 
 *  Later, it shows a menu and prompts the user to select a test case to run.
 * Parameters:
 *  none
 * Return:
 *  int
 **********************************************************************************************************************
 */
int main(void)
{   
    /* Variable to store the received character through terminal */ 
    uint8_t uartReadValue;   
    
    #if defined (CY_DEVICE_SECURE)
    cyhal_wdt_t wdt_obj;

    /* Clear watchdog timer so that it doesn't trigger a reset */
    result = cyhal_wdt_init(&wdt_obj, cyhal_wdt_get_max_timeout_ms());
    CY_ASSERT(CY_RSLT_SUCCESS == result);
    cyhal_wdt_free(&wdt_obj);
    #endif /* #if defined (CY_DEVICE_SECURE) */

    /* Initialize the device and board peripherals */
    if (cybsp_init() != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize retarget-io to use the debug UART port */
    if (cy_retarget_io_init(CYBSP_DEBUG_UART_TX, CYBSP_DEBUG_UART_RX, CY_RETARGET_IO_BAUDRATE) != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }
    
    /* Initialize the SAR module */
    uint16_t upperThreshold = ADC_3_5V_LEVEL;
    uint16_t lowerThreshold = ADC_1_5V_LEVEL;
    cy_en_sar2_range_detection_mode_t rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_OUTSIDE_RANGE;
    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
    
    /* Enable global interrupts */
    __enable_irq();
    
    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("***********************************************************\r\n");
    printf("SAR ADC Range Detection\r\n");
    printf("***********************************************************\r\n");
    printf(">> Select one of the options as listed below\r\n");
    printf(">> 0 : Upper Range Threshold : 4.5V / Lower Range Threshold : 3.5V\r\n");
    printf(">> 1 : Upper Range Threshold : 4.5V / Lower Range Threshold : 2.5V\r\n");
    printf(">> 2 : Upper Range Threshold : 4.5V / Lower Range Threshold : 1.5V\r\n");
    printf(">> 3 : Upper Range Threshold : 4.5V / Lower Range Threshold : 0.5V\r\n");
    printf(">> 4 : Upper Range Threshold : 3.5V / Lower Range Threshold : 1.5V\r\n");
    printf(">> 5 : Upper Range Threshold : 2.5V / Lower Range Threshold : 0.5V\r\n");
    printf(">> a : Above Upper Threshold Detection\r\n");
    printf(">> b : Below Lower Threshold Detection\r\n");
    printf(">> i : Inside Range Detection\r\n");
    printf(">> o : Outside Range Detection\r\n\r\n");

    for (;;)
    {
        /* Set user led port pin to high when range condition was detected in the ADC interrupt handler */
        if (g_rangeDetected)
        {
            g_rangeDetected = false;
            Cy_GPIO_Set(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
        }
        /* Set user led port pin to low when no range condition was detected */
        else
        {
            Cy_GPIO_Clr(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
        }
        
        /* Check if key was pressed and change range detection parameter accordingly */
        if (cyhal_uart_getc(&cy_retarget_io_uart_obj, &uartReadValue, 1) == CY_RSLT_SUCCESS)
        {
            switch (uartReadValue)
            {
                /* Change upper and lower threshold of range detection */
                case '0':
                    printf("Upper Threshold Set to 4.5V - Lower Threshold Set to 3.5V\r\n");
                    upperThreshold = ADC_4_5V_LEVEL;
                    lowerThreshold = ADC_3_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                case '1':
                    printf("Upper Threshold Set to 4.5V - Lower Threshold Set to 2.5V\r\n");
                    upperThreshold = ADC_4_5V_LEVEL;
                    lowerThreshold = ADC_2_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                case '2':
                    printf("Upper Threshold Set to 4.5V - Lower Threshold Set to 1.5V\r\n");
                    upperThreshold = ADC_4_5V_LEVEL;
                    lowerThreshold = ADC_1_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                case '3':
                    printf("Upper Threshold Set to 4.5V - Lower Threshold Set to 0.5V\r\n");
                    upperThreshold = ADC_4_5V_LEVEL;
                    lowerThreshold = ADC_0_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                case '4':
                    printf("Upper Threshold Set to 3.5V - Lower Threshold Set to 1.5V\r\n");
                    upperThreshold = ADC_3_5V_LEVEL;
                    lowerThreshold = ADC_1_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                case '5':
                    printf("Upper Threshold Set to 2.5V - Lower Threshold Set to 0.5V\r\n");
                    upperThreshold = ADC_2_5V_LEVEL;
                    lowerThreshold = ADC_0_5V_LEVEL;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;

                /* Change range detection mode */
                case 'a':
                    printf("Above Upper Threshold Detection activated\r\n");
                    rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_ABOVE_HI;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;
                
                case 'b':
                    printf("Below Lower Threshold Detection activated\r\n");
                    rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_BELOW_LO;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;
                
                case 'i':
                    printf("Inside Range Detection activated\r\n");
                    rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_INSIDE_RANGE;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;
                
                case 'o':
                    printf("Outside Range Detection activated\r\n");
                    rangeDetectionMode = CY_SAR2_RANGE_DETECTION_MODE_OUTSIDE_RANGE;
                    configureAdcRangeDetectionMode(rangeDetectionMode, upperThreshold, lowerThreshold);
                    break;
                
                default:
                    break;                    
            }
        }        
    }
}

/* [] END OF FILE */