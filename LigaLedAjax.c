#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ssd1306.h"
#include "font.h"

#define LED_PIN 12
#define BOTAO_A 5
#define BOTAO_JOY 22
#define JOYSTICK_X 26
#define JOYSTICK_Y 27
#define POT_PIN 28
#define RELAY_PIN 16
#define ADC_INPUT 2     // ADC2 (GPIO28)
#define VREF 3.3f       // Tensão de referência
#define ADC_MAX 4095.0f // ADC de 12 bits

#define VOLT_MIN 2.6f  // Corresponde a 100%
#define VOLT_MAX 3.13f // Corresponde a 0%

#define HISTERESE 2.0f // Margem de 2% para evitar oscilação

#define WIFI_SSID "Familia Luz"
#define WIFI_PASS "65327890"

#define I2C_PORT_DISP i2c1
#define I2C_SDA_DISP 14
#define I2C_SCL_DISP 15
#define endereco 0x3C

float nivel_minimo = 20.0f; // Nível mínimo de água
float nivel_maximo = 80.0f; // Nível máximo de água
float nivel_atual;          // Nível atual de água
bool estado_bomba = false;  // Estado da bomba (ligada/desligada)

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

struct http_state
{
    char response[8192];
    size_t len;
    size_t sent;
};

float read_voltage(uint16_t adc_val)
{
    return (adc_val * VREF) / ADC_MAX;
}

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

float calculate_level(float voltage)
{
    float level = ((VOLT_MAX - voltage) / (VOLT_MAX - VOLT_MIN)) * 100.0f;

    if (level < 0.0f)
        level = 0.0f;
    else if (level > 100.0f)
        level = 100.0f;

    return level;
}

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

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

    if (strstr(req, "GET /led/on"))
    {
        gpio_put(LED_PIN, 1);
        const char *txt = "Ligado";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    else if (strstr(req, "GET /led/off"))
    {
        gpio_put(LED_PIN, 0);
        const char *txt = "Desligado";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(txt), txt);
    }
    else if (strstr(req, "GET /estado"))
    {
        uint16_t adc_raw = read_adc_avg();
        float voltage = read_voltage(adc_raw);
        float level = calculate_level(voltage);

        nivel_atual = level;

        // Verifica se deve LIGAR a bomba (nível abaixo do mínimo - histerese)
        if (nivel_atual <= (nivel_minimo - HISTERESE) && !estado_bomba)
        {
            gpio_put(RELAY_PIN, 1);
            estado_bomba = true;
            printf("Bomba LIGADA - Nível: %.2f (abaixo de %.2f)\n", nivel_atual, nivel_minimo);
        }

        // Verifica se deve DESLIGAR a bomba (nível acima do máximo + histerese)
        if (nivel_atual >= (nivel_maximo + HISTERESE) && estado_bomba)
        {
            gpio_put(RELAY_PIN, 0);
            estado_bomba = false;
            printf("Bomba DESLIGADA - Nível: %.2f (acima de %.2f)\n", nivel_atual, nivel_maximo);
        }

        // adc_select_input(0);
        // uint16_t x = adc_read();
        // adc_select_input(1);
        // uint16_t y = adc_read();
        // int botao = !gpio_get(BOTAO_A);
        // int joy = !gpio_get(BOTAO_JOY);

        // char json_payload[96];
        // int json_len = snprintf(json_payload, sizeof(json_payload),
        //                         "{\"led\":%d,\"x\":%d,\"y\":%d,\"botao\":%d,\"joy\":%d}\r\n",
        //                         gpio_get(LED_PIN), x, y, botao, joy);

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

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

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

#include "pico/bootrom.h"
#define BOTAO_B 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

int main()
{
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    stdio_init_all();
    sleep_ms(2000);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    gpio_init(BOTAO_JOY);
    gpio_set_dir(BOTAO_JOY, GPIO_IN);
    gpio_pull_up(BOTAO_JOY);

    // gpio_init(RELAY_PIN);
    // gpio_set_dir(RELAY_PIN, GPIO_OUT);
    // gpio_put(RELAY_PIN, 0);

    adc_init();
    adc_gpio_init(POT_PIN);
    adc_select_input(2);

    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(I2C_SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_DISP);
    gpio_pull_up(I2C_SCL_DISP);

    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT_DISP);
    ssd1306_config(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Iniciando Wi-Fi", 0, 0);
    ssd1306_draw_string(&ssd, "Aguarde...", 0, 30);
    ssd1306_send_data(&ssd);

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

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    char ip_str[24];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "WiFi => OK", 0, 0);
    ssd1306_draw_string(&ssd, ip_str, 0, 10);
    ssd1306_send_data(&ssd);

    start_http_server();
    // char str_x[5]; // Buffer para armazenar a string
    // char str_y[5]; // Buffer para armazenar a string
    // bool cor = true;

    char buffer[20];
    while (true)
    {
        cyw43_arch_poll();

        uint16_t adc_raw = read_adc_avg();
        float voltage = read_voltage(adc_raw);
        float level = calculate_level(voltage);

        ssd1306_fill(&ssd, false);

        snprintf(buffer, sizeof(buffer), "ADC: %d", adc_raw);
        ssd1306_draw_string(&ssd, buffer, 10, 5);

        snprintf(buffer, sizeof(buffer), "V: %.2f", voltage);
        ssd1306_draw_string(&ssd, buffer, 10, 15);

        snprintf(buffer, sizeof(buffer), "%%: %.2f", level);
        ssd1306_draw_string(&ssd, buffer, 10, 25);

        snprintf(buffer, sizeof(buffer), "MAX: %.2f", nivel_maximo);
        ssd1306_draw_string(&ssd, buffer, 10, 35);

        snprintf(buffer, sizeof(buffer), "MIN: %.2f", nivel_minimo);
        ssd1306_draw_string(&ssd, buffer, 10, 45);

        ssd1306_draw_string(&ssd, ip_str, 10, 55);

        ssd1306_send_data(&ssd);

        sleep_ms(100);

        // // Leitura dos valores analógicos
        // adc_select_input(0);
        // uint16_t adc_value_x = adc_read();
        // adc_select_input(1);
        // uint16_t adc_value_y = adc_read();

        // sprintf(str_x, "%d", adc_value_x);            // Converte o inteiro em string
        // sprintf(str_y, "%d", adc_value_y);            // Converte o inteiro em string
        // ssd1306_fill(&ssd, !cor);                     // Limpa o display
        // ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor); // Desenha um retângulo
        // ssd1306_line(&ssd, 3, 25, 123, 25, cor);      // Desenha uma linha
        // ssd1306_line(&ssd, 3, 37, 123, 37, cor);      // Desenha uma linha

        // ssd1306_draw_string(&ssd, "CEPEDI   TIC37", 8, 6); // Desenha uma string
        // ssd1306_draw_string(&ssd, "EMBARCATECH", 20, 16);  // Desenha uma string
        // ssd1306_draw_string(&ssd, ip_str, 10, 28);
        // ssd1306_draw_string(&ssd, "X    Y    PB", 20, 41);           // Desenha uma string
        // ssd1306_line(&ssd, 44, 37, 44, 60, cor);                     // Desenha uma linha vertical
        // ssd1306_draw_string(&ssd, str_x, 8, 52);                     // Desenha uma string
        // ssd1306_line(&ssd, 84, 37, 84, 60, cor);                     // Desenha uma linha vertical
        // ssd1306_draw_string(&ssd, str_y, 49, 52);                    // Desenha uma string
        // ssd1306_rect(&ssd, 52, 90, 8, 8, cor, !gpio_get(BOTAO_JOY)); // Desenha um retângulo
        // ssd1306_rect(&ssd, 52, 102, 8, 8, cor, !gpio_get(BOTAO_A));  // Desenha um retângulo
        // ssd1306_rect(&ssd, 52, 114, 8, 8, cor, !cor);                // Desenha um retângulo
        // ssd1306_send_data(&ssd);                                     // Atualiza o display

        // sleep_ms(300);
    }

    cyw43_arch_deinit();
    return 0;
}
