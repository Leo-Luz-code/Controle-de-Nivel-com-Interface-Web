#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"

#include "pico/bootrom.h"

// Definições de pinos para botões e LEDs
#define BOTAO_B 6
#define LED_AMARELO 4
#define LED_VERDE 9
#define LED_VERMELHO 8
#define BOTAO_A 5
#define BOTAO_JOY 22
#define POT_PIN 28
#define RELAY_PIN 16
#define ADC_INPUT 2     // ADC2 (GPIO28) para leitura do sensor de nível
#define VREF 3.3f       // Tensão de referência do ADC (3.3V)
#define ADC_MAX 4095.0f // Resolução do ADC de 12 bits

// Configurações do buzzer
const uint BUZZER_A = 21;                      // Pino do buzzer
volatile bool botao_pressionado = false;        // Estado do botão pressionado
uint64_t this_current_time;                    // Tempo atual em microssegundos
uint32_t last_buzzer_time = 0;                 // Último momento que o buzzer foi acionado
uint32_t buzzer_interval = 250000;             // Intervalo para desligar o buzzer (em microssegundos)
bool buzzer_state = false;                     // Estado atual do buzzer (ligado/desligado)
const float DIVIDER_PWM = 16.0;                // Divisor de clock para PWM do buzzer
const uint16_t PERIOD = 4096;                  // Período do PWM para o buzzer
uint slice_buzzer;                             // Slice PWM associado ao buzzer

// Limites de tensão para cálculo do nível de água
#define VOLT_MIN 2.6f  // Tensão correspondente a 100% do nível
#define VOLT_MAX 3.13f // Tensão correspondente a 0% do nível
#define HISTERESE 2.0f // Margem de 2% para evitar oscilações na lógica de controle

// Configurações de Wi-Fi
#define WIFI_SSID "Seu_usuario_wifi"
#define WIFI_PASS "Sua_senha_wifi"

// Configurações do display OLED I2C
#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C  // Endereço I2C do display SSD1306

// Variáveis para controle do nível de água
float nivel_minimo = 20.0f; // Nível mínimo de água (%)
float nivel_maximo = 80.0f; // Nível máximo de água (%)
float nivel_atual;          // Nível atual de água (%)
bool estado_bomba = false;  // Estado da bomba (ligada/desligada)

// Variáveis para debounce de botões
uint32_t current_time;       // Tempo atual para verificação de debounce
static volatile uint32_t last_time_A = 0; // Último tempo de pressionamento do botão A
static volatile uint32_t last_time_B = 0; // Último tempo de pressionamento do botão B

// Página HTML para interface web
const char HTML_BODY[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Controle de Nível</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body { font-family: sans-serif; text-align: center; padding: 20px; margin: 0; background: #f5f5f5; }"
    ".container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
    "h1 { color: #2c3e50; margin-top: 0; }"
    ".barra-container { margin: 20px 0; }"
    ".barra { width: 100%; height: 30px; background: #ecf0f1; border-radius: 15px; overflow: hidden; }"
    "#barra_nivel_agua { height: 100%; background: #3498db; width: 0%; transition: width 0.5s ease; }"
    ".nivel-texto { margin: 10px 0; font-size: 18px; }"
    ".config-container { display: flex; justify-content: space-between; margin: 20px 0; }"
    ".config-input { width: 45%; }"
    "input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; }"
    "button { background: #2ecc71; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; margin-top: 10px; }"
    "button:hover { background: #27ae60; }"
    ".status { margin-top: 20px; padding: 10px; border-radius: 4px; }"
    ".bomba-ligada { background: #e74c3c; color: white; }"
    ".bomba-desligada { background: #2ecc71; color: white; }"
    "</style>"
    "<script>"
    "function atualizarNiveis() {"
    "  const min = document.getElementById('nivel_minimo_input').value;"
    "  const max = document.getElementById('nivel_maximo_input').value;"
    "  if (parseInt(min) >= parseInt(max)) {"
    "    alert(\"O nível mínimo deve ser menor que o nível máximo.\");"
    "    return;"
    "  }"
    "     fetch('/set_niveis?min=' + min + '&max=' + max) "
    "    .then(response => response.text())"
    "    .then(data => console.log(data));"
    "}"
    "function atualizarDados() {"
    "  fetch('/estado').then(res => res.json()).then(data => {"
    "    document.getElementById('barra_nivel_agua').style.width = data.nivel + '%';"
    "    document.getElementById('nivel_atual').innerText = data.nivel + '%';"
    "    document.getElementById('nivel_minimo_text').innerText = data.min + '%';"
    "    document.getElementById('nivel_maximo_text').innerText = data.max + '%';"
    "    const statusBomba = document.getElementById('status_bomba');"
    "    statusBomba.innerText = data.bomba ? 'LIGADA' : 'DESLIGADA';"
    "    statusBomba.className = data.bomba ? 'status bomba-ligada' : 'status bomba-desligada';"
    "  });"
    "}"
    "setInterval(atualizarDados, 1000);"
    "</script></head><body>"
    "<div class='container'>"
    "<h1>Controle de Nível de Água</h1>"

    "<div class='barra-container'>"
    "<div class='nivel-texto'>Nível Atual: <span id='nivel_atual'>--</span></div>"
    "<div class='barra'><div id='barra_nivel_agua'></div></div>"
    "</div>"

    "<div class='config-container'>"
    "<div class='config-input'>"
    "<div class='nivel-texto'>Nível Mínimo: <span id='nivel_minimo_text'>--</span></div>"
    "<label for='nivel_minimo_input'>Nível Mínimo (0-100):</label>"
    "<input type='number' id='nivel_minimo_input' min='0' max='100' value='20'>"
    "</div>"

    "<div class='config-input'>"
    "<div class='nivel-texto'>Nível Máximo: <span id='nivel_maximo_text'>--</span></div>"
    "<label for='nivel_maximo_input'>Nível Máximo (0-100):</label>"
    "<input type='number' id='nivel_maximo_input' min='0' max='100' value='80'>"
    "</div>"
    "</div>"

    "<button onclick='atualizarNiveis()'>Salvar Configurações</button>"

    "<div id='status_bomba' class='status'>--</div>"
    "</div>"
    "</body></html>";

// Estrutura para gerenciar respostas HTTP
struct http_state
{
    char response[8192]; // Buffer para resposta HTTP
    size_t len;          // Tamanho da resposta
    size_t sent;         // Quantidade de dados enviados
};

// Manipulador de interrupção para botões
void gpio_irq_handler(uint gpio, uint32_t event)
{
    current_time = to_us_since_boot(get_absolute_time());

    // Botão A: Reseta níveis mínimo e máximo
    if (gpio == BOTAO_A && (current_time - last_time_A > 200000))
    {
        last_time_A = current_time;
        nivel_maximo = 80.0f;
        nivel_minimo = 20.0f;
    }

    // Botão B: Entra no modo bootloader
    if (gpio == BOTAO_B)
    {
        reset_usb_boot(0, 0);
    }
}

// Funções para controle dos LEDs
void ligar_led_amarelo() {
    gpio_put(LED_AMARELO, 1);
    gpio_put(LED_VERDE, 0);
    gpio_put(LED_VERMELHO, 0);
}

void ligar_led_verde() {
    gpio_put(LED_VERDE, 1);
    gpio_put(LED_AMARELO, 0);
    gpio_put(LED_VERMELHO, 0);
}

void ligar_led_vermelho() {
    gpio_put(LED_VERDE, 0);
    gpio_put(LED_AMARELO, 0);
    gpio_put(LED_VERMELHO, 1);
}

// Funções para controle do buzzer
void buzzer_on()
{
    pwm_set_gpio_level(BUZZER_A, 300); // Define nível PWM para ligar o buzzer
    buzzer_state = true;
    last_buzzer_time = time_us_64();
}

void buzzer_off()
{
    pwm_set_gpio_level(BUZZER_A, 0); // Desliga o buzzer
    buzzer_state = false;
}

// Converte leitura do ADC em tensão
float read_voltage(uint16_t adc_val)
{
    return (adc_val * VREF) / ADC_MAX;
}

// Calcula média de 16 leituras do ADC para maior precisão
uint16_t read_adc_avg()
{
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++)
    {
        sum += adc_read();
        sleep_ms(1);
    }
    return sum / 16;
}

// Calcula o nível de água com base na tensão
float calculate_level(float voltage)
{
    float level = ((VOLT_MAX - voltage) / (VOLT_MAX - VOLT_MIN)) * 100.0f;
    if (level < 0.0f)
        level = 0.0f;
    else if (level > 100.0f)
        level = 100.0f;
    return level;
}

// Callback para envio de dados HTTP
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb); // Fecha conexão após envio completo
        free(hs);
    }
    return ERR_OK;
}

// Manipula requisições HTTP recebidas
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    // Responde com estado atual do sistema em JSON
    if (strstr(req, "GET /estado"))
    {
        char json_payload[128];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"nivel\":%.2f,\"bomba\":%d,\"min\":%.2f,\"max\":%.2f}",
                                nivel_atual, estado_bomba, nivel_minimo, nivel_maximo);

        printf("[DEBUG] JSON: %s\n", json_payload);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    // Atualiza níveis mínimo e máximo via requisição HTTP
    else if (strstr(req, "GET /set_niveis"))
    {
        float min, max;
        sscanf(req, "GET /set_niveis?min=%f&max=%f", &min, &max);
        nivel_minimo = min;
        nivel_maximo = max;

        const char *txt = "Níveis atualizados";

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    // Envia página HTML padrão
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

// Callback para novas conexões TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Inicia o servidor HTTP na porta 80
static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

int main()
{
    // Configuração inicial dos botões
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled(BOTAO_A, GPIO_IRQ_EDGE_FALL, true);

    // Inicializa comunicação serial
    stdio_init_all();
    sleep_ms(2000);

    // Configura LEDs como saídas
    gpio_init(LED_AMARELO);
    gpio_set_dir(LED_AMARELO, GPIO_OUT);
    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERDE, GPIO_OUT);
    gpio_init(LED_VERMELHO);
    gpio_set_dir(LED_VERMELHO, GPIO_OUT);
    ligar_led_verde(); // Inicia com LED verde ligado

    // Configura relé da bomba
    gpio_init(RELAY_PIN);
    gpio_set_dir(RELAY_PIN, GPIO_OUT);
    gpio_put(RELAY_PIN, 0); // Bomba inicia desligada

    // Configura botões como entradas com pull-up
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_init(BOTAO_JOY);
    gpio_set_dir(BOTAO_JOY, GPIO_IN);
    gpio_pull_up(BOTAO_JOY);

    // Configura PWM para o buzzer
    gpio_set_function(BUZZER_A, GPIO_FUNC_PWM);
    slice_buzzer = pwm_gpio_to_slice_num(BUZZER_A);
    pwm_set_clkdiv(slice_buzzer, DIVIDER_PWM);
    pwm_set_wrap(slice_buzzer, PERIOD);
    pwm_set_gpio_level(BUZZER_A, 0);
    pwm_set_enabled(slice_buzzer, true);

    // Inicializa ADC para leitura do sensor
    adc_init();
    adc_gpio_init(POT_PIN);
    adc_select_input(2);

    // Configura I2C para o display OLED
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    // Inicializa display SSD1306
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

    // Inicializa Wi-Fi
    if (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => FALHA", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "WiFi => ERRO", 0, 0);
        ssd1306_send_data(&ssd);
        return 1;
    }

    // Exibe endereço IP no display
    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server(); // Inicia servidor HTTP

    // Buffer para exibição no display
    char buffer[20];
    while (true)
    {
        cyw43_arch_poll(); // Atualiza estado da rede Wi-Fi

        // Lê nível de água
        uint16_t adc_raw = read_adc_avg();
        float voltage = read_voltage(adc_raw);
        float level = calculate_level(voltage);
        nivel_atual = level;

        this_current_time = time_us_64();

        // Verifica alarme de nível alto
        if (nivel_atual > (nivel_maximo + HISTERESE + 3))
        {
            ligar_led_vermelho(); // Liga LED vermelho para alarme
            if (!buzzer_state) {
                buzzer_on(); // Ativa buzzer se não estiver ligado
            }
        }
        // Verifica condição de nível baixo (bomba ligada)
        else if (nivel_atual <= (nivel_minimo - HISTERESE))
        {
            gpio_put(RELAY_PIN, 1); // Liga bomba
            estado_bomba = true;
            ligar_led_amarelo();    // Liga LED amarelo
            buzzer_off();           // Desliga buzzer
            printf("Bomba LIGADA - Nível: %.2f (abaixo de %.2f)\n", nivel_atual, nivel_minimo);
        }
        // Verifica condição de nível alto (bomba desligada)
        else if (nivel_atual >= (nivel_maximo + HISTERESE))
        {
            gpio_put(RELAY_PIN, 0); // Desliga bomba
            estado_bomba = false;
            ligar_led_verde();      // Liga LED verde
            buzzer_off();           // Desliga buzzer
            printf("Bomba DESLIGADA - Nível: %.2f (acima de %.2f)\n", nivel_atual, nivel_maximo);
        }
        else // Nível dentro da faixa normal
        {
            if (!estado_bomba) {
                ligar_led_verde(); // Mantém LED verde se bomba está desligada
            }
            buzzer_off(); // Garante que buzzer está desligado
        }

        // Atualiza informações no display
        ssd1306_fill(&ssd, false);
        snprintf(buffer, sizeof(buffer), "ADC: %d", adc_raw);
        ssd1306_draw_string(&ssd, buffer, 10, 5);
        snprintf(buffer, sizeof(buffer), "BOMBA: %s", estado_bomba ? "LIGADA" : "DESL.");
        ssd1306_draw_string(&ssd, buffer, 10, 15);
        snprintf(buffer, sizeof(buffer), "%%: %.2f", level);
        ssd1306_draw_string(&ssd, buffer, 10, 25);
        snprintf(buffer, sizeof(buffer), "MAX: %.2f", nivel_maximo);
        ssd1306_draw_string(&ssd, buffer, 10, 35);
        snprintf(buffer, sizeof(buffer), "MIN: %.2f", nivel_minimo);
        ssd1306_draw_string(&ssd, buffer, 10, 45);
        ssd1306_draw_string(&ssd, ip_str, 10, 55);
        ssd1306_send_data(&ssd);

        sleep_ms(100); // Aguarda 100ms antes da próxima iteração
    }

    cyw43_arch_deinit(); // Finaliza Wi-Fi
}
 