# 🚀 Gestor Hídrico Inteligente (GHI) 🚀
> *“Monitore e automatize o nível de água de reservatórios com precisão, display OLED e interface web.”*

## 📝 Descrição Breve
Este projeto implementa um sistema de controle de nível de água utilizando um **Raspberry Pi Pico W**. O sistema lê a porcentagem de água de um sensor de nível analógico, aciona um **relé para controlar uma bomba d'água** com lógica de histerese, e fornece feedback em tempo real através de um **display OLED**, **LEDs de status** e um **alarme sonoro (buzzer)**.

Adicionalmente, o GHI hospeda um **servidor web** com um dashboard interativo que exibe o nível atual, o estado da bomba e permite ao usuário **ajustar os níveis mínimo e máximo de operação remotamente** pela rede Wi-Fi.

## ✨ Funcionalidades Principais
*   💧 **Leitura de Nível de Água:** Aquisição de dados de um sensor de nível analógico (via ADC) com média de leituras para estabilidade.
*   ポンプ **Controle Automático de Bomba:** Acionamento de um relé para ligar/desligar uma bomba d'água, utilizando uma faixa de histerese para evitar acionamentos excessivos.
*   🌐 **Interface Web (Dashboard):** Servidor HTTP na porta 80 com uma página que se atualiza automaticamente, exibindo o nível em uma barra de progresso e permitindo a configuração remota dos pontos de operação (nível mínimo e máximo).
*   🖥️ **Display OLED:** Dashboard local mostrando em tempo real o nível percentual, estado da bomba, leituras do sensor, e o endereço IP do dispositivo.
*   🚨 **Alertas Visuais e Sonoros:**
    *   **LED Verde:** Nível normal, bomba desligada.
    *   **LED Amarelo:** Nível baixo, bomba ligada (enchendo).
    *   **LED Vermelho:** Nível crítico (muito alto), com alarme sonoro.
*   ⚙️ **Configuração por Hardware:** Botões físicos para resetar as configurações para o padrão e para entrar em modo bootloader (BOOTSEL).

## ⚙️ Pré-requisitos / Hardware & Software
### Hardware
| Componente | Quant. | Observações |
|------------|--------|-------------|
| Raspberry Pi **Pico W** | 1 | MCU RP2040 + Wi-Fi |
| Sensor de Nível de Água Analógico | 1 | Conectado ao pino 28 (ADC2) |
| **OLED SSD1306** 128×64 (I²C) | 1 | SDA: GP14 / SCL: GP15 |
| Módulo Relé 5V | 1 | Para acionar a bomba d'água (Pino 16) |
| LEDs (Vermelho, Amarelo, Verde) | 3 | Pinos 8, 4, 9 respectivamente |
| Botões (Push Button) | 2 | Pinos 5 e 6 |
| **Buzzer Piezo** | 1 | Pino 21 (PWM) |
| Fonte 5 V ≥ 1 A | 1 | Para o Pico e componentes |

### Software / Ferramentas
| Ferramenta/Software | Versão Mínima | Observações |
|------------|----------------|-------------|
| **Pico SDK** | >= 1.5 | Toolchain C/C++ para Raspberry Pi Pico |
| **CMake** | >= 3.13 | Sistema de build |
| **ARM GCC Compiler** | | Parte da toolchain do Pico SDK |
| Navegador Web Moderno | | Para acessar o dashboard |

## 🔌 Conexões / Configuração Inicial
### Pinagem Resumida
- **Sensor de Nível** -> GP28 (ADC2)
- **Botão A (Reset Níveis)** -> GP5
- **Botão B (BOOTSEL)** -> GP6
- **LED Vermelho** -> GP8
- **LED Amarelo** -> GP4
- **LED Verde** -> GP9
- **Buzzer** -> GP21 (PWM)
- **Módulo Relé** -> GP16
- **OLED SDA/SCL** -> GP14 / GP15 (I2C1)

> *Lembre-se de ter um GND comum entre todos os dispositivos. O sensor de nível e o relé podem necessitar de VCC de 3.3V ou 5V, conforme a especificação.*

### ▶️ Como Compilar e Executar

```bash
# A partir do diretório raiz do projeto
mkdir build && cd build

# Exporte o caminho para o Pico SDK (ajuste conforme seu ambiente)
export PICO_SDK_PATH=/caminho/para/pico-sdk

# Gere os arquivos de build com CMake
cmake ..

# Compile o projeto (o número após -j é a quantidade de núcleos do seu processador)
make -j4

# Coloque o Pico em modo BOOTSEL (segure o botão BOOTSEL e conecte o USB)
# Copie o arquivo .uf2 gerado em `build/` para a unidade montada do Pico.
```

Após carregar o firmware:
- O display OLED mostrará "Iniciando Wi-Fi...", depois "WiFi => OK" e o endereço IP.
- Acesse `http://<IP_DO_PICO>` no seu navegador para ver o dashboard.
- Os LEDs e o display indicarão o status atual do sistema.

### 📁 Estrutura do Projeto:
```bash
.
├── lib/                      # Opcional: para bibliotecas como ssd1306
│   └── ...
├── main.c                    # Código fonte principal com toda a lógica
├── CMakeLists.txt            # Script de build
└── README.md                 # Este arquivo
```

## 🐛 Debugging / Solução de Problemas

*   **Não conecta ao Wi-Fi?** Verifique se o `WIFI_SSID` e `WIFI_PASS` estão corretos em `main.c`. Confira a intensidade do sinal.
*   **OLED apagado?** Confirme o endereço I²C (`0x3C` no código) e a fiação dos pinos SDA (GP14) e SCL (GP15).
*   **Leitura de nível incorreta?** O sensor analógico pode variar. Calibre os valores de `VOLT_MIN` e `VOLT_MAX` no código para corresponder ao seu hardware específico. Use o monitor serial para ver as leituras de tensão.
*   **Bomba não aciona?** Verifique a fiação do módulo relé (GND, VCC, IN no pino GP16). Confirme se o relé está recebendo a tensão correta.
*   **Página web não carrega?** Verifique se o dispositivo está na mesma rede que o Pico W. Confira o endereço IP exibido no OLED e verifique se não há um firewall bloqueando o acesso na porta 80.

## 👤 Autor / Contato

**Autores:** Jeová Cosme de Jesus Pinheiro, Jonas Souza Pinto, Leonardo Rodrigues Luz e Wilton Lacerda Silva Junior.
**Contato:**  Jonassouza871@hotmail.com
