/* USER CODE BEGIN Header */
// FAILSAFE BLDC 6-Step Kommutierung für IR2104
// Poti-Steuerung (0-100%), Hardware-Interrupts (Hall & Analog Watchdog), 170MHz G4
/* USER CODE END Header */

#include "main.h"

ADC_HandleTypeDef hadc1;
TIM_HandleTypeDef htim1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);

/* ====================================================================== */
/* GLOBALE VARIABLEN                                                      */
/* ====================================================================== */
uint32_t current_pwm = 0;
uint32_t target_pwm = 0; // Vom Poti geforderter Wert

/* ====================================================================== */
/* DIE KOMMUTIERUNGS-LOGIK (FAILSAFE)                                     */
/* ====================================================================== */
void Bldc_Commutate(void)
{
    // SICHERHEITS-ABSCHALTUNG: Wenn Poti unter 5% ist, alles hart ausschalten!
    if (current_pwm < 425) {
        // Alle Treiber deaktivieren (Floating / Ausrollen)
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
        // Alle PWM Signale auf 0
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
        return;
    }

    // Hall-Sensoren auslesen (Angenommen auf PB4, PB5, PB6)
    uint8_t h1 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_4);
    uint8_t h2 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5);
    uint8_t h3 = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6);
    uint8_t hall_state = (h3 << 2) | (h2 << 1) | h1;

    // Harte 6-Schritt-Kommutierung
    // PA4, PA5, PA6 sind die SD (Shutdown) Pins der Treiber
    switch (hall_state) {
        case 5: // Phase A High, Phase B Low, Phase C Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); // C aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0); // B permanent Low-Side ON
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5, GPIO_PIN_SET);
            break;
        case 1: // Phase A High, Phase C Low, Phase B Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // B aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0); // C permanent Low-Side ON
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_6, GPIO_PIN_SET);
            break;
        case 3: // Phase B High, Phase C Low, Phase A Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); // A aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_SET);
            break;
        case 2: // Phase B High, Phase A Low, Phase C Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); // C aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5 | GPIO_PIN_4, GPIO_PIN_SET);
            break;
        case 6: // Phase C High, Phase A Low, Phase B Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // B aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_4, GPIO_PIN_SET);
            break;
        case 4: // Phase C High, Phase B Low, Phase A Aus
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); // A aus
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, current_pwm);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6 | GPIO_PIN_5, GPIO_PIN_SET);
            break;
        default:
            // Fehlerfall (z.B. Kabelbruch): SOFORT AUS!
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 0);
            break;
    }
}

/* ====================================================================== */
/* INTERRUPTS: HALL-SENSOREN & ANALOG WATCHDOG (STROMBEGRENZUNG)          */
/* ====================================================================== */

// Wird getriggert bei jeder Änderung der Hall-Sensoren
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_4 || GPIO_Pin == GPIO_PIN_5 || GPIO_Pin == GPIO_PIN_6)
    {
        Bldc_Commutate();
    }
}

// Wird getriggert, wenn der Shunt-Strom > 42,4 A steigt (Analog Watchdog)
void HAL_ADC_LevelOutOfWindowCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
        // Strom ist zu hoch! PWM sofort abregeln (Cycle-by-Cycle Limiting)
        if (current_pwm > 400) {
            current_pwm -= 400; // Harter Drop der PWM
        } else {
            current_pwm = 0;
        }
        // Neue (niedrigere) PWM direkt in die Hardware schreiben
        Bldc_Commutate(); 
    }
}

/* ====================================================================== */
/* MAIN SCHLEIFE                                                          */
/* ====================================================================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_TIM1_Init();

    // Timer PWM Ausgänge starten
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    __HAL_TIM_MOE_ENABLE(&htim1);

    // Kontinuierliche ADC Konvertierung für Poti und Shunt starten
    HAL_ADC_Start_IT(&hadc1); 

    while (1)
    {
        // Einfaches Polling des Poti-Werts für die Soll-Vorgabe
        // Hinweis: In einer echten Anwendung mit Shunt-Messung sollte ADC über DMA laufen.
        // Für diesen Prototyp fangen wir den Wert aus dem regulären Zyklus ab.
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        {
            uint32_t poti_wert = HAL_ADC_GetValue(&hadc1);

            // 4095 (Max Poti) -> 8075 (Max 95% Duty Cycle für Bootstrap Ladezeit)
            target_pwm = (poti_wert * 8075) / 4095;

            // Sanftes Nachziehen der PWM auf den Zielwert (Recovery nach Strombegrenzung)
            if (current_pwm < target_pwm) {
                current_pwm += 10; // Langsam wieder ansteigen lassen
            } else {
                current_pwm = target_pwm; 
            }

            Bldc_Commutate();
        }
        HAL_Delay(2); // Entlastet die CPU, ADC scannt im Hintergrund weiter
    }
}

/* ====================================================================== */
/* HARDWARE KONFIGURATIONEN                                               */
/* ====================================================================== */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) { Error_Handler(); }
}

static void MX_ADC1_Init(void) {
    ADC_MultiModeTypeDef multimode = {0};
    ADC_ChannelConfTypeDef sConfig = {0};
    ADC_AnalogWDGConfTypeDef AnalogWDGConfig = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation = 0;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE; // Scan Mode für Poti + Shunt
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;    // Kontinuierliche Wandlung!
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

    multimode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) { Error_Handler(); }

    // Channel 2 Config (Poti)
    sConfig.Channel = ADC_CHANNEL_2;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

    // Analog Watchdog Config (Strombegrenzung am Shunt)
    // Rechnung: 42.4A * 0.001Ohm * 50 Gain = 2.12V. 
    // ADC (12 Bit, 3.3V Ref) -> (2.12V / 3.3V) * 4095 = ~2630
    AnalogWDGConfig.WatchdogMode = ADC_ANALOGWATCHDOG_SINGLE_REG;
    AnalogWDGConfig.HighThreshold = 2630; // Abschaltgrenze 42,4 A!
    AnalogWDGConfig.LowThreshold = 0;
    AnalogWDGConfig.Channel = ADC_CHANNEL_1; // Annahme: Shunt auf CH1 (PA0)
    AnalogWDGConfig.ITMode = ENABLE;         // Interrupt erzwingen
    if (HAL_ADC_AnalogWDGConfig(&hadc1, &AnalogWDGConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM1_Init(void) {
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
    __HAL_RCC_TIM1_CLK_ENABLE();
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 8499;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK) { Error_Handler(); }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) { Error_Handler(); }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK) { Error_Handler(); }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) { Error_Handler(); }
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK) { Error_Handler(); }
    
    // Break & Deadtime deaktiviert, da IR2104 die Totzeit hardwareseitig macht
    sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
    sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
    sBreakDeadTimeConfig.DeadTime = 0; 
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // SD Pins der 3 IR2104 Treiber (Initial Aus)
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // PWM Pins für TIM1 CH1-CH3
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Hall Sensor Eingänge
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Interrupts freischalten
    HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0); // Hall
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0); // Hall
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    
    // ADC Interrupt für den Analog Watchdog freischalten!
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 0, 0); // Höchste Priorität für Stromschutz
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
}

void Error_Handler(void) { while (1) {} }