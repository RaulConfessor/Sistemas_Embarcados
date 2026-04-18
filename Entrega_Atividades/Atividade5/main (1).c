#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Configuração de Hardware
#define LED_PIN         GPIO_NUM_4
#define BUTTON_PIN      GPIO_NUM_15


// Parâmetros de Temporização
#define DEBOUNCE_MS     50
#define AUTO_OFF_MS     10000
#define POLL_INTERVAL   10

// Máquina de Estados do LED
typedef enum {
    LED_OFF,
    LED_ON,
    LED_TIMED_OUT,
} led_state_t;

// Contexto global do sistema
typedef struct {
    led_state_t led;
    int         btn_stable;       // Nível estabilizado após debounce
    int         btn_last_raw;     // Última leitura crua do pino
    uint32_t    debounce_ts;      // Timestamp da última variação detectada
    uint32_t    led_on_ts;        // Timestamp em que o LED foi ligado
} system_ctx_t;

// Utilitários
static inline uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static inline uint32_t elapsed_ms(uint32_t since)
{
    return now_ms() - since;
}

// Inicialização de Periféricos
static void peripherals_init(void)
{
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 0);

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
}

// Controle do LED
static void led_turn_on(system_ctx_t *ctx)
{
    ctx->led       = LED_ON;
    ctx->led_on_ts = now_ms();
    gpio_set_level(LED_PIN, 1);
    printf("LED Ligado. Temporizador de 10s iniciado.\n");
}

static void led_turn_off(system_ctx_t *ctx, const char *reason)
{
    ctx->led = LED_OFF;
    gpio_set_level(LED_PIN, 0);
    printf("LED Desligado: %s\n", reason);
}

// Lógica de Debounce — retorna true quando um novo clique é confirmado
static bool debounce_tick(system_ctx_t *ctx)
{
    int raw = gpio_get_level(BUTTON_PIN);
    uint32_t t = now_ms();

    if (raw != ctx->btn_last_raw) {
        ctx->debounce_ts  = t;
        ctx->btn_last_raw = raw;
    }

    bool stable_period  = (t - ctx->debounce_ts) > DEBOUNCE_MS;
    bool level_changed  = (raw != ctx->btn_stable);

    if (stable_period && level_changed) {
        ctx->btn_stable = raw;
        return (raw == 0); // Pull-up: clique válido = nível baixo
    }

    return false;
}

// Processamento da Máquina de Estados
static void state_machine_run(system_ctx_t *ctx)
{
    bool button_pressed = debounce_tick(ctx);

    switch (ctx->led) {
        case LED_OFF:
            if (button_pressed) {
                led_turn_on(ctx);
            }
            break;

        case LED_ON:
            if (button_pressed) {
                led_turn_off(ctx, "manual");
            } else if (elapsed_ms(ctx->led_on_ts) >= AUTO_OFF_MS) {
                ctx->led = LED_TIMED_OUT;
                led_turn_off(ctx, "tempo limite atingido");
            }
            break;

        case LED_TIMED_OUT:
            // Aguarda o botão ser solto antes de aceitar novo acionamento
            if (ctx->btn_stable == 1) {
                ctx->led = LED_OFF;
            }
            break;
    }
}

// Entry Point
void app_main(void)
{
    peripherals_init();

    system_ctx_t ctx = {
        .led          = LED_OFF,
        .btn_stable   = 1,
        .btn_last_raw = 1,
        .debounce_ts  = 0,
        .led_on_ts    = 0,
    };

    while (1) {
        state_machine_run(&ctx);
        vTaskDelay(POLL_INTERVAL / portTICK_PERIOD_MS);
    }
}
