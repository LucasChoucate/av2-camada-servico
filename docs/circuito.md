# Circuito

Os dois controladores são ESP32-S3. Os números abaixo são GPIOs.

## ESP32 chão de fábrica

| Componente | Pino do componente | GPIO | Observação |
|---|---|---|---|
| HC-SR04 (ultrassônico) | TRIG | 4 | |
| HC-SR04 (ultrassônico) | ECHO | 5 | O ECHO sai em 5 V. Use divisor resistivo (ex.: 1 kΩ e 2 kΩ) para não passar de 3,3 V no GPIO |
| DHT22 | DATA | 6 | Resistor de pull-up de 10 kΩ entre DATA e 3,3 V (alguns módulos já trazem) |
| LDR | divisor de tensão | 7 | LDR + resistor de 10 kΩ; o ponto do meio vai no GPIO 7 (ADC) |
| LED verde | ânodo | 15 | Resistor de 220 Ω a 330 Ω em série |
| LED vermelho | ânodo | 16 | Resistor de 220 Ω a 330 Ω em série |

Alimentação: HC-SR04 em 5 V, DHT22 e LDR em 3,3 V, todos com GND comum.

O ultrassônico fica em cima do tanque, apontando para baixo. 100 cm de distância equivale a 0% e 10 cm a 100% (constantes `DIST_VAZIO` e `DIST_CHEIO` no firmware). Abaixo de 20% o LED vermelho acende e o verde apaga.

## ESP32 de monitoramento

| Componente | Pino do componente | GPIO | Observação |
|---|---|---|---|
| Matriz 8x8 MAX7219 (1088AS) | DIN | 13 | |
| Matriz 8x8 MAX7219 (1088AS) | CS | 14 | |
| Matriz 8x8 MAX7219 (1088AS) | CLK | 15 | |

A matriz é alimentada em 5 V com GND comum ao ESP32. Se o texto aparecer embaralhado, troque `GENERIC_HW` por `PAROLA_HW` no firmware.

## Fotos do circuito

[PREENCHER: adicionar fotos da montagem em `docs/img/` e referenciar aqui, por exemplo `![Chão de fábrica](img/chao-de-fabrica.jpg)`]
